// MIT License
// Copyright 2023--present rgpot developers

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/NWChemPot/DynLib.hpp"
#include "rgpot/NWChemPot/nwchem_c_abi.h"
#include "rgpot/units.hpp"

#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace rgpot {

using units::HARTREE_TO_EV;
using units::NEG_GRAD_TO_FORCE;

namespace {

using EnergyGradFn = RgpotNWChemResult (*)(int, const double *, const int *,
                                           int, int, const char *, const char *,
                                           const char *, double *);
using SetConfigFn = int (*)(const char *, const char *, const char *, int, int);
using VersionFn = const char *(*)(void);
using AbiAvailFn = int (*)(void);

std::vector<std::string> engine_lib_candidates(const std::string &explicit_path) {
  std::vector<std::string> out;
  if (!explicit_path.empty())
    out.push_back(explicit_path);
  if (const char *e = std::getenv("RGPOT_NWCHEM_ENGINE"))
    out.emplace_back(e);
  out.emplace_back("libnwchem_engine.so");
  out.emplace_back("./libnwchem_engine.so");
  out.emplace_back("libnwchem_engine.dylib");
  out.emplace_back("./libnwchem_engine.dylib");
  out.emplace_back("nwchem_engine.dll");
  return out;
}

void apply_env_hints(const NWChemConfig &cfg) {
  if (!cfg.nwchem_root.empty()) {
#if !defined(_WIN32)
    setenv("NWCHEM_TOP", cfg.nwchem_root.c_str(), 0);
#endif
  }
  if (const char *top = std::getenv("NWCHEM_TOP")) {
    (void)top;
  }
}

struct EngineBundle {
  DynLib engine_lib;
  EnergyGradFn energy_grad = nullptr;
  SetConfigFn set_config = nullptr;
  VersionFn version = nullptr;
  AbiAvailFn abi_available = nullptr;
  std::string load_error;
  bool loaded = false;
};

bool try_load_engine(EngineBundle &b, const std::string &engine_path) {
  b.load_error.clear();
  b.loaded = false;
  b.energy_grad = nullptr;
  b.set_config = nullptr;
  b.version = nullptr;
  b.abi_available = nullptr;

  bool eng_ok = false;
  std::string eng_err;
  for (const auto &cand : engine_lib_candidates(engine_path)) {
    try {
      b.engine_lib.open(cand);
      eng_ok = true;
      break;
    } catch (const std::exception &ex) {
      eng_err = ex.what();
    }
  }
  if (!eng_ok) {
    b.load_error = "libnwchem_engine not loaded: " + eng_err;
    return false;
  }

  b.energy_grad =
      b.engine_lib.sym_optional<EnergyGradFn>("rgpot_nwchem_energy_grad");
  b.set_config =
      b.engine_lib.sym_optional<SetConfigFn>("rgpot_nwchem_set_config");
  b.version =
      b.engine_lib.sym_optional<VersionFn>("rgpot_nwchem_engine_version");
  b.abi_available =
      b.engine_lib.sym_optional<AbiAvailFn>("rgpot_nwchem_abi_available");

  if (!b.energy_grad) {
    b.load_error = "engine missing rgpot_nwchem_energy_grad";
    return false;
  }
  b.loaded = true;
  return true;
}

// Process-wide probe cache.
std::mutex g_probe_mu;
bool g_probe_done = false;
bool g_probe_ok = false;
bool g_abi_probe_done = false;
bool g_abi_probe_ok = false;

} // namespace

struct NWChemPot::Impl {
  EngineBundle bundle;
};

NWChemPot::NWChemPot() : NWChemPot(NWChemConfig{}) {}

NWChemPot::NWChemPot(const NWChemConfig &config)
    : Potential(PotType::NWChem), impl_(new Impl), config_(config) {
  apply_env_hints(config_);
  if (!try_load_engine(impl_->bundle, config_.engine_path)) {
    // Defer hard failure to forceImpl; probe_available reports false.
  } else if (impl_->bundle.set_config) {
    impl_->bundle.set_config(config_.basis.c_str(), config_.theory.c_str(),
                             config_.scf_type.c_str(), config_.charge,
                             config_.multiplicity);
  }
}

NWChemPot::~NWChemPot() { delete impl_; }

bool NWChemPot::setConfig(const NWChemConfig &config) {
  config_ = config;
  apply_env_hints(config_);
  if (!impl_ || !impl_->bundle.loaded || !impl_->bundle.set_config)
    return false;
  return impl_->bundle.set_config(config_.basis.c_str(), config_.theory.c_str(),
                                  config_.scf_type.c_str(), config_.charge,
                                  config_.multiplicity) == 0;
}

bool NWChemPot::available() const {
  return impl_ && impl_->bundle.loaded && impl_->bundle.energy_grad;
}

bool NWChemPot::probe_available() {
  std::lock_guard<std::mutex> lock(g_probe_mu);
  if (g_probe_done)
    return g_probe_ok;
  EngineBundle tmp;
  g_probe_ok = try_load_engine(tmp, {});
  g_probe_done = true;
  return g_probe_ok;
}

bool NWChemPot::abi_available() {
  std::lock_guard<std::mutex> lock(g_probe_mu);
  if (g_abi_probe_done)
    return g_abi_probe_ok;
  EngineBundle tmp;
  if (!try_load_engine(tmp, {})) {
    g_abi_probe_ok = false;
  } else if (tmp.abi_available) {
    g_abi_probe_ok = tmp.abi_available() != 0;
  } else {
    g_abi_probe_ok = false;
  }
  g_abi_probe_done = true;
  return g_abi_probe_ok;
}

void NWChemPot::forceImpl(const ForceInput &in, ForceOut *out) const {
  if (!available()) {
    throw std::runtime_error(
        std::string("NWChem engine (libnwchem_engine) not loaded: ") +
        (impl_ ? impl_->bundle.load_error : "no impl"));
  }

  const int n = static_cast<int>(in.nAtoms);
  if (n <= 0) {
    throw std::runtime_error("NWChemPot: nAtoms must be positive");
  }
  if (!in.pos || !in.atmnrs || !out || !out->F) {
    throw std::runtime_error("NWChemPot: null positions/atmnrs/forces buffer");
  }

  std::vector<double> grad(static_cast<size_t>(n) * 3u, 0.0);

  RgpotNWChemResult res = impl_->bundle.energy_grad(
      n, in.pos, in.atmnrs, config_.charge, config_.multiplicity,
      config_.basis.c_str(), config_.theory.c_str(), config_.scf_type.c_str(),
      grad.data());

  if (!res.ok) {
    throw std::runtime_error(std::string("NWChem engine failed: ") +
                             res.message);
  }

  out->energy = res.energy_h * HARTREE_TO_EV;
  out->variance = 0.0;

  for (int i = 0; i < n * 3; ++i) {
    out->F[static_cast<size_t>(i)] =
        grad[static_cast<size_t>(i)] * NEG_GRAD_TO_FORCE;
  }
}

} // namespace rgpot
