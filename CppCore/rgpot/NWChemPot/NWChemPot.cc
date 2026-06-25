// MIT License
// Copyright 2023--present rgpot developers

#include <capnp/message.h>

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/NWChemPot/DynLib.hpp"
#include "rgpot/NWChemPot/nwchem_c_abi.h"
#include "rgpot/types/adapters/capnp/nwchem_capnp_map.hpp"
#include "rgpot/units.hpp"

#include <cstdio>
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

static void copy_to_buf(char *dst, size_t n, const std::string &src) {
  if (!dst || n == 0)
    return;
  if (src.empty()) {
    dst[0] = '\0';
    return;
  }
  std::snprintf(dst, n, "%s", src.c_str());
}

using ParamsDefaultFn = void (*)(RgpotNWChemParams *);
using SetParamsFn = int (*)(const RgpotNWChemParams *);
using EnergyGradFn = RgpotNWChemResult (*)(int, const double *, const int *,
                                           const RgpotNWChemParams *, double *);
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
    setenv("NWCHEM_TOP", cfg.nwchem_root.c_str(), 1);
#endif
  }
}

struct EngineBundle {
  DynLib engine_lib;
  ParamsDefaultFn params_default = nullptr;
  SetParamsFn set_params = nullptr;
  EnergyGradFn energy_grad = nullptr;
  SetConfigFn set_config = nullptr; // legacy fallback
  VersionFn version = nullptr;
  AbiAvailFn abi_available = nullptr;
  std::string load_error;
  bool loaded = false;
};

bool try_load_engine(EngineBundle &b, const std::string &engine_path) {
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

bool push_params_to_engine(EngineBundle &b, const NWChemConfig &cfg) {
  RgpotNWChemParams p;
  cfg.toAbiParams(&p);
  if (b.set_params)
    return b.set_params(&p) == 0;
  if (b.set_config)
    return b.set_config(cfg.basis.c_str(), cfg.theory.c_str(),
                        cfg.scf_type.c_str(), cfg.charge,
                        cfg.multiplicity) == 0;
  return false;
}

std::mutex g_probe_mu;
bool g_probe_done = false;
bool g_probe_ok = false;
bool g_abi_probe_done = false;
bool g_abi_probe_ok = false;

} // namespace

void NWChemConfig::toAbiParams(RgpotNWChemParams *out) const {
  if (!out)
    return;
  std::memset(out, 0, sizeof(*out));
  copy_to_buf(out->basis, sizeof(out->basis), basis);
  copy_to_buf(out->theory, sizeof(out->theory), theory);
  copy_to_buf(out->scf_type, sizeof(out->scf_type), scf_type);
  out->charge = charge;
  out->multiplicity = multiplicity;
  copy_to_buf(out->engine_path, sizeof(out->engine_path), engine_path);
  copy_to_buf(out->nwchem_root, sizeof(out->nwchem_root), nwchem_root);
}

NWChemConfig NWChemConfig::fromAbiParams(const RgpotNWChemParams &p) {
  NWChemConfig cfg;
  cfg.basis = p.basis;
  cfg.theory = p.theory;
  cfg.scf_type = p.scf_type;
  cfg.charge = p.charge;
  cfg.multiplicity = p.multiplicity;
  cfg.engine_path = p.engine_path;
  cfg.nwchem_root = p.nwchem_root;
  return cfg;
}

struct NWChemPot::Impl {
  EngineBundle bundle;
};

NWChemPot::NWChemPot() : Potential(PotType::NWChem), impl_(new Impl) {
  // Default user options = schema defaults on NWChemParams (empty reader path).
  ::capnp::MallocMessageBuilder msg;
  auto root = msg.initRoot<::NWChemParams>();
  (void)root; // schema defaults already applied by initRoot
  config_ = types::adapt::capnp::nwchemConfigFromCapnp(root.asReader());
  apply_env_hints(config_);
  if (try_load_engine(impl_->bundle, config_.engine_path))
    (void)push_params_to_engine(impl_->bundle, config_);
}

NWChemPot::NWChemPot(const ::NWChemParams::Reader &params)
    : Potential(PotType::NWChem), impl_(new Impl) {
  config_ = types::adapt::capnp::nwchemConfigFromCapnp(params);
  apply_env_hints(config_);
  if (try_load_engine(impl_->bundle, config_.engine_path))
    (void)push_params_to_engine(impl_->bundle, config_);
}

NWChemPot::NWChemPot(const NWChemConfig &config)
    : Potential(PotType::NWChem), impl_(new Impl), config_(config) {
  apply_env_hints(config_);
  if (try_load_engine(impl_->bundle, config_.engine_path))
    (void)push_params_to_engine(impl_->bundle, config_);
}

NWChemPot::~NWChemPot() { delete impl_; }

bool NWChemPot::setParams(const ::NWChemParams::Reader &params) {
  // NWChem arm payload of rgpot PotentialConfig; mirror then embed ABI.
  return setConfig(types::adapt::capnp::nwchemConfigFromCapnp(params));
}

void NWChemPot::getParams(::NWChemParams::Builder out) const {
  types::adapt::capnp::nwchemConfigToCapnp(out, config_);
}

bool NWChemPot::setPotentialConfig(const ::PotentialConfig::Reader &cfg,
                                   std::string *message_out) {
  return types::adapt::capnp::applyPotentialConfig(*this, cfg, message_out);
}

bool NWChemPot::setConfig(const NWChemConfig &config) {
  // Internal: env + dlopen, then rgpot_nwchem_set_params (embed-only C buffers).
  const bool need_reload =
      !impl_ || !impl_->bundle.loaded ||
      (!config.engine_path.empty() && config.engine_path != config_.engine_path);

  config_ = config;
  apply_env_hints(config_);

  if (!impl_)
    impl_ = new Impl;

  if (need_reload) {
    impl_->bundle = EngineBundle{};
    if (!try_load_engine(impl_->bundle, config_.engine_path))
      return false;
  }

  if (!impl_->bundle.loaded)
    return false;
  return push_params_to_engine(impl_->bundle, config_);
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
  if (n <= 0)
    throw std::runtime_error("NWChemPot: nAtoms must be positive");
  if (!in.pos || !in.atmnrs || !out || !out->F)
    throw std::runtime_error("NWChemPot: null positions/atmnrs/forces buffer");

  RgpotNWChemParams params;
  config_.toAbiParams(&params);

  std::vector<double> grad(static_cast<size_t>(n) * 3u, 0.0);
  RgpotNWChemResult res = impl_->bundle.energy_grad(
      n, in.pos, in.atmnrs, &params, grad.data());

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
