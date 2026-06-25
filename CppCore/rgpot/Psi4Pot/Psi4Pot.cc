// MIT License
// Copyright 2023--present rgpot developers

#include "rgpot/Psi4Pot/Psi4Pot.hpp"
#include "rgpot/Psi4Pot/DynLib.hpp"
#include "rgpot/Psi4Pot/rgpot_psi4_abi.h"
#include "rgpot/units.hpp"

#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace rgpot {

using units::ANGSTROM_TO_BOHR;
using units::HARTREE_TO_EV;
using units::NEG_GRAD_TO_FORCE;

namespace {

using EngineFn = RgpotPsi4Result (*)(int, const double *, const int *, int, int,
                                     double *, const char *);
using EngineBasisFn = RgpotPsi4Result (*)(int, const double *, const int *, int,
                                          int, double *, const char *,
                                          const char *);
using VersionFn = const char *(*)(void);

std::vector<std::string> psi4_lib_candidates(const std::string &explicit_path) {
  std::vector<std::string> out;
  if (!explicit_path.empty())
    out.push_back(explicit_path);
  if (const char *e = std::getenv("RGPOT_PSI4_SO"))
    out.emplace_back(e);
  out.emplace_back("libpsi4.so");
  out.emplace_back("./libpsi4.so");
  out.emplace_back("libpsi4.dylib");
  out.emplace_back("./libpsi4.dylib");
  out.emplace_back("psi4.dll");
  return out;
}

std::vector<std::string> engine_lib_candidates() {
  std::vector<std::string> out;
  if (const char *e = std::getenv("RGPOT_PSI4_ENGINE"))
    out.emplace_back(e);
  out.emplace_back("libpsi4_engine.so");
  out.emplace_back("./libpsi4_engine.so");
  out.emplace_back("libpsi4_engine.dylib");
  out.emplace_back("./libpsi4_engine.dylib");
  out.emplace_back("psi4_engine.dll");
  return out;
}

std::string resolve_data_dir(const Psi4Config &cfg) {
  if (!cfg.data_dir.empty())
    return cfg.data_dir;
  if (const char *e = std::getenv("RGPOT_PSI4_DATADIR"))
    return e;
  if (const char *e = std::getenv("PSIDATADIR"))
    return e;
  return {};
}

struct EngineBundle {
  DynLib psi4_lib;
  DynLib engine_lib;
  EngineFn energy_grad = nullptr;
  EngineBasisFn energy_grad_basis = nullptr;
  VersionFn version = nullptr;
  std::string load_error;
  bool loaded = false;
};

bool try_load_engine(EngineBundle &b, const std::string &lib_path) {
  b.load_error.clear();
  b.loaded = false;
  b.energy_grad = nullptr;
  b.energy_grad_basis = nullptr;
  b.version = nullptr;

  // 1) Load libpsi4 with RTLD_GLOBAL so engine can resolve Psi4 C++ symbols.
  bool psi4_ok = false;
  std::string psi4_err;
  for (const auto &cand : psi4_lib_candidates(lib_path)) {
    try {
      b.psi4_lib.open(cand);
      psi4_ok = true;
      break;
    } catch (const std::exception &ex) {
      psi4_err = ex.what();
    }
  }
  if (!psi4_ok) {
    // Engine may still resolve via LD_LIBRARY_PATH / rpath; continue.
    b.load_error = "libpsi4 not loaded (" + psi4_err + "); trying engine only";
  }

  // 2) Load engine
  bool eng_ok = false;
  std::string eng_err;
  for (const auto &cand : engine_lib_candidates()) {
    try {
      b.engine_lib.open(cand);
      eng_ok = true;
      break;
    } catch (const std::exception &ex) {
      eng_err = ex.what();
    }
  }
  if (!eng_ok) {
    b.load_error = "libpsi4_engine not loaded: " + eng_err;
    return false;
  }

  b.energy_grad =
      b.engine_lib.sym_optional<EngineFn>("rgpot_psi4_blyp_energy_grad");
  b.energy_grad_basis = b.engine_lib.sym_optional<EngineBasisFn>(
      "rgpot_psi4_blyp_energy_grad_basis");
  b.version =
      b.engine_lib.sym_optional<VersionFn>("rgpot_psi4_engine_version");

  if (!b.energy_grad && !b.energy_grad_basis) {
    b.load_error = "engine missing rgpot_psi4_blyp_energy_grad[_basis]";
    return false;
  }
  b.loaded = true;
  return true;
}

// Process-wide probe cache (probe_available / construction).
std::mutex g_probe_mu;
bool g_probe_done = false;
bool g_probe_ok = false;

} // namespace

struct Psi4Pot::Impl {
  EngineBundle bundle;
};

Psi4Pot::Psi4Pot() : Psi4Pot(Psi4Config{}) {}

Psi4Pot::Psi4Pot(const Psi4Config &config)
    : Potential(PotType::Psi4), impl_(new Impl), config_(config) {
  if (!try_load_engine(impl_->bundle, config_.library_path)) {
    // Defer hard failure to forceImpl; probe_available reports false.
  }
}

Psi4Pot::~Psi4Pot() { delete impl_; }

bool Psi4Pot::available() const {
  return impl_ && impl_->bundle.loaded &&
         (impl_->bundle.energy_grad || impl_->bundle.energy_grad_basis);
}

bool Psi4Pot::probe_available() {
  std::lock_guard<std::mutex> lock(g_probe_mu);
  if (g_probe_done)
    return g_probe_ok;
  EngineBundle tmp;
  g_probe_ok = try_load_engine(tmp, {});
  g_probe_done = true;
  return g_probe_ok;
}

void Psi4Pot::forceImpl(const ForceInput &in, ForceOut *out) const {
  if (!available()) {
    throw std::runtime_error(
        std::string("Psi4 engine (libpsi4_engine) not loaded: ") +
        (impl_ ? impl_->bundle.load_error : "no impl"));
  }

  const int n = static_cast<int>(in.nAtoms);
  if (n <= 0) {
    throw std::runtime_error("Psi4Pot: nAtoms must be positive");
  }
  if (!in.pos || !in.atmnrs || !out || !out->F) {
    throw std::runtime_error("Psi4Pot: null positions/atmnrs/forces buffer");
  }

  std::vector<double> grad(static_cast<size_t>(n) * 3u, 0.0);

  // Positions are Angstrom in ForceInput (rgpot convention).
  const std::string datadir = resolve_data_dir(config_);
  const char *dd = datadir.empty() ? nullptr : datadir.c_str();

  RgpotPsi4Result res{};
  if (impl_->bundle.energy_grad_basis) {
    res = impl_->bundle.energy_grad_basis(n, in.pos, in.atmnrs, config_.charge,
                                          config_.multiplicity, grad.data(), dd,
                                          config_.basis.c_str());
  } else {
    res = impl_->bundle.energy_grad(n, in.pos, in.atmnrs, config_.charge,
                                    config_.multiplicity, grad.data(), dd);
  }

  if (!res.ok) {
    throw std::runtime_error(std::string("Psi4 engine failed: ") + res.message);
  }

  // Energy: Hartree -> eV
  out->energy = res.energy_h * HARTREE_TO_EV;
  out->variance = 0.0;

  // Gradient Hartree/Bohr -> forces eV/Angstrom (F = -grad with unit conversion)
  for (int i = 0; i < n * 3; ++i) {
    out->F[static_cast<size_t>(i)] =
        grad[static_cast<size_t>(i)] * NEG_GRAD_TO_FORCE;
  }
}

} // namespace rgpot
