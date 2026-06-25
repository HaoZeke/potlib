// MIT License
// Copyright 2023--present rgpot developers

#include <capnp/message.h>

#include "rgpot/NWChemPot/DynLib.hpp"
#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/NWChemPot/nwchem_c_abi.h"
#include "rgpot/types/adapters/capnp/nwchem_capnp_map.hpp"
#include "rgpot/units.hpp"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace rgpot {

using units::HARTREE_TO_EV;
using units::NEG_GRAD_TO_FORCE;

namespace {

using ParamsDefaultFn = void (*)(RgpotNWChemParams *);
using SetParamsFn = int (*)(const RgpotNWChemParams *);
using EnergyGradFn = RgpotNWChemResult (*)(int, const double *, const int *,
                                           const RgpotNWChemParams *, double *);
using SetConfigFn = int (*)(const char *, const char *, const char *, int, int);
using VersionFn = const char *(*)(void);
using AbiAvailFn = int (*)(void);

std::vector<std::string> engine_lib_candidates(const char *explicit_path) {
  std::vector<std::string> out;
  if (explicit_path && explicit_path[0])
    out.emplace_back(explicit_path);
  if (const char *e = std::getenv("RGPOT_NWCHEM_ENGINE"))
    out.emplace_back(e);
  out.emplace_back("libnwchem_engine.so");
  out.emplace_back("./libnwchem_engine.so");
  out.emplace_back("libnwchem_engine.dylib");
  out.emplace_back("./libnwchem_engine.dylib");
  out.emplace_back("nwchem_engine.dll");
  return out;
}

void apply_env_hints(const RgpotNWChemParams &p) {
  if (p.nwchem_root[0]) {
#if !defined(_WIN32)
    setenv("NWCHEM_TOP", p.nwchem_root, 1);
    if (!std::getenv("NWCHEM_BASIS_LIBRARY") ||
        !std::getenv("NWCHEM_BASIS_LIBRARY")[0]) {
      std::string bas = std::string(p.nwchem_root) + "/src/basis/libraries/";
      setenv("NWCHEM_BASIS_LIBRARY", bas.c_str(), 0);
    }
#endif
  }
}

struct EngineBundle {
  DynLib engine_lib;
  ParamsDefaultFn params_default = nullptr;
  SetParamsFn set_params = nullptr;
  EnergyGradFn energy_grad = nullptr;
  SetConfigFn set_config = nullptr; // compat only
  VersionFn version = nullptr;
  AbiAvailFn abi_available = nullptr;
  std::string load_error;
  bool loaded = false;
};

bool try_load_engine(EngineBundle &b, const char *engine_path) {
  b.load_error.clear();
  b.loaded = false;
  b.params_default = nullptr;
  b.set_params = nullptr;
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

  b.params_default =
      b.engine_lib.sym_optional<ParamsDefaultFn>("rgpot_nwchem_params_default");
  b.set_params =
      b.engine_lib.sym_optional<SetParamsFn>("rgpot_nwchem_set_params");
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

bool push_params_to_engine(EngineBundle &b, const RgpotNWChemParams &p) {
  if (b.set_params)
    return b.set_params(&p) == 0;
  if (b.set_config)
    return b.set_config(p.basis, p.theory, p.scf_type, p.charge,
                        p.multiplicity) == 0;
  return false;
}

/** Apply Cap'n Proto reader into stored POD, load engine, push embed. */
bool apply_abi_params(EngineBundle &bundle, RgpotNWChemParams &stored,
                      const RgpotNWChemParams &incoming) {
  const bool need_reload =
      !bundle.loaded ||
      (incoming.engine_path[0] &&
       std::strcmp(incoming.engine_path, stored.engine_path) != 0);

  stored = incoming;
  apply_env_hints(stored);

  if (need_reload) {
    bundle = EngineBundle{};
    if (!try_load_engine(bundle, stored.engine_path))
      return false;
  } else if (!bundle.loaded) {
    if (!try_load_engine(bundle, stored.engine_path))
      return false;
  }

  if (!bundle.loaded)
    return false;
  return push_params_to_engine(bundle, stored);
}

std::mutex g_probe_mu;
bool g_probe_done = false;
bool g_probe_ok = false;
bool g_abi_probe_done = false;
bool g_abi_probe_ok = false;

} // namespace

struct NWChemPot::Impl {
  EngineBundle bundle;
};

NWChemPot::NWChemPot() : Potential(PotType::NWChem), impl_(new Impl) {
  types::adapt::capnp::nwchemAbiDefaults(&abi_params_);
  apply_env_hints(abi_params_);
  if (try_load_engine(impl_->bundle, abi_params_.engine_path))
    (void)push_params_to_engine(impl_->bundle, abi_params_);
}

NWChemPot::NWChemPot(const ::NWChemParams::Reader &params)
    : Potential(PotType::NWChem), impl_(new Impl) {
  types::adapt::capnp::nwchemParamsToAbi(params, &abi_params_);
  apply_env_hints(abi_params_);
  if (try_load_engine(impl_->bundle, abi_params_.engine_path))
    (void)push_params_to_engine(impl_->bundle, abi_params_);
}

NWChemPot::~NWChemPot() { delete impl_; }

bool NWChemPot::setParams(const ::NWChemParams::Reader &params) {
  // Always store Cap'n Proto → embed POD; engine push may fail without embed.
  RgpotNWChemParams incoming;
  types::adapt::capnp::nwchemParamsToAbi(params, &incoming);
  if (!impl_)
    impl_ = new Impl;
  const bool need_reload =
      !impl_->bundle.loaded ||
      (incoming.engine_path[0] &&
       std::strcmp(incoming.engine_path, abi_params_.engine_path) != 0);
  abi_params_ = incoming;
  apply_env_hints(abi_params_);
  if (need_reload) {
    impl_->bundle = EngineBundle{};
    if (!try_load_engine(impl_->bundle, abi_params_.engine_path))
      return false;
  } else if (!impl_->bundle.loaded) {
    if (!try_load_engine(impl_->bundle, abi_params_.engine_path))
      return false;
  }
  if (!impl_->bundle.loaded)
    return false;
  return push_params_to_engine(impl_->bundle, abi_params_);
}

void NWChemPot::getParams(::NWChemParams::Builder out) const {
  types::adapt::capnp::nwchemAbiToParams(abi_params_, out);
}

bool NWChemPot::setPotentialConfig(const ::PotentialConfig::Reader &cfg,
                                   std::string *message_out) {
  return types::adapt::capnp::applyPotentialConfig(*this, cfg, message_out);
}

bool NWChemPot::available() const {
  return impl_ && impl_->bundle.loaded && impl_->bundle.energy_grad;
}

bool NWChemPot::probe_available() {
  std::lock_guard<std::mutex> lock(g_probe_mu);
  if (g_probe_done)
    return g_probe_ok;
  EngineBundle tmp;
  g_probe_ok = try_load_engine(tmp, nullptr);
  g_probe_done = true;
  return g_probe_ok;
}

bool NWChemPot::abi_available() {
  std::lock_guard<std::mutex> lock(g_probe_mu);
  if (g_abi_probe_done)
    return g_abi_probe_ok;
  EngineBundle tmp;
  if (!try_load_engine(tmp, nullptr)) {
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
  if (n <= 0)
    throw std::runtime_error("NWChemPot: nAtoms must be positive");
  if (!in.pos || !in.atmnrs || !out || !out->F)
    throw std::runtime_error("NWChemPot: null positions/atmnrs/forces buffer");

  // Per-call full params from last configure (Cap'n Proto → stored POD).
  std::vector<double> grad(static_cast<size_t>(n) * 3u, 0.0);
  RgpotNWChemResult res = impl_->bundle.energy_grad(
      n, in.pos, in.atmnrs, &abi_params_, grad.data());

  if (!res.ok) {
    throw std::runtime_error(std::string("NWChem engine failed: ") +
                             res.message);
  }

  out->energy = res.energy_h * HARTREE_TO_EV;
  out->variance = 0.0;
  for (int i = 0; i < n * 3; ++i)
    out->F[static_cast<size_t>(i)] =
        grad[static_cast<size_t>(i)] * NEG_GRAD_TO_FORCE;
}

} // namespace rgpot
