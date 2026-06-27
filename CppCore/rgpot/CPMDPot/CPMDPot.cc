// MIT License
// Copyright 2023--present rgpot developers

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/array.h>

#include "rgpot/CPMDPot/CPMDPot.hpp"
#include "rgpot/CPMDPot/cpmd_c_abi.h"
#include "rgpot/NWChemPot/DynLib.hpp"
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

using EnergyGradientFn = CPMDCResult (*)(int, const double *, const int *,
                                         const void *, size_t, double *);
using SetParamsFn = int (*)(const void *, size_t);
using VersionFn = const char *(*)(void);
using AvailableFn = int (*)(void);

struct ParamsView {
  const void *data = nullptr;
  size_t size = 0;
};

std::vector<std::string> engine_lib_candidates(const std::string &explicit_path) {
  std::vector<std::string> out;
  if (!explicit_path.empty())
    out.emplace_back(explicit_path);
  if (const char *e = std::getenv("CPMDC_LIBRARY"))
    out.emplace_back(e);
  if (const char *e = std::getenv("RGPOT_CPMDC_ENGINE"))
    out.emplace_back(e);
  if (const char *e = std::getenv("RGPOT_CPMD_ENGINE"))
    out.emplace_back(e);
  out.emplace_back("libcpmdc.so");
  out.emplace_back("./libcpmdc.so");
  out.emplace_back("libcpmdc.dylib");
  out.emplace_back("./libcpmdc.dylib");
  out.emplace_back("cpmdc.dll");
  return out;
}

void apply_env_hints(const std::string &cpmd_root) {
  if (!cpmd_root.empty()) {
#if !defined(_WIN32)
    setenv("CPMD_ROOT", cpmd_root.c_str(), 1);
#endif
  }
}

struct EngineBundle {
  DynLib engine_lib;
  EnergyGradientFn energy_gradient = nullptr;
  SetParamsFn set_params = nullptr;
  VersionFn version = nullptr;
  AvailableFn available = nullptr;
  std::string load_error;
  bool loaded = false;
};

bool try_load_engine(EngineBundle &b, const std::string &engine_path) {
  b.load_error.clear();
  b.loaded = false;
  b.energy_gradient = nullptr;
  b.set_params = nullptr;
  b.version = nullptr;
  b.available = nullptr;

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
    b.load_error = "libcpmdc not loaded: " + eng_err;
    return false;
  }

  b.energy_gradient =
      b.engine_lib.sym_optional<EnergyGradientFn>("cpmdc_energy_gradient");
  b.set_params = b.engine_lib.sym_optional<SetParamsFn>("cpmdc_set_params");
  b.version = b.engine_lib.sym_optional<VersionFn>("cpmdc_version");
  b.available = b.engine_lib.sym_optional<AvailableFn>("cpmdc_available");

  if (!b.energy_gradient) {
    b.load_error = "engine missing cpmdc_energy_gradient";
    return false;
  }
  if (!b.set_params) {
    b.load_error = "engine missing cpmdc_set_params";
    return false;
  }
  b.loaded = true;
  return true;
}

std::vector<::capnp::word> serialize_params(const ::CPMDParams::Reader &params) {
  ::capnp::MallocMessageBuilder msg;
  msg.setRoot(params);
  auto words = ::capnp::messageToFlatArray(msg);
  std::vector<::capnp::word> out(words.size());
  std::memcpy(out.data(), words.begin(), words.size() * sizeof(::capnp::word));
  return out;
}

std::vector<::capnp::word> default_params() {
  ::capnp::MallocMessageBuilder msg;
  auto params = msg.initRoot<::CPMDParams>();
  return serialize_params(params.asReader());
}

ParamsView params_view(const std::vector<::capnp::word> &words) {
  ParamsView view;
  if (!words.empty()) {
    view.data = words.data();
    view.size = words.size() * sizeof(::capnp::word);
  }
  return view;
}

bool push_params_to_engine(EngineBundle &b,
                           const std::vector<::capnp::word> &params_words) {
  if (!b.set_params)
    return false;
  const ParamsView view = params_view(params_words);
  return b.set_params(view.data, view.size) == 0;
}

std::string params_summary(const ::CPMDParams::Reader &params) {
  std::string out = "functional=" + std::string(params.getFunctional().cStr()) +
                    " cutoffRy=" + std::to_string(params.getCutOffRy()) +
                    " charge=" + std::to_string(params.getCharge()) +
                    " mult=" + std::to_string(params.getMultiplicity());
  if (params.getEnginePath().size() > 0)
    out += " enginePath=" + std::string(params.getEnginePath().cStr());
  if (params.getCpmdRoot().size() > 0)
    out += " cpmdRoot=" + std::string(params.getCpmdRoot().cStr());
  return out;
}

void copy_params_to_builder(const ::CPMDParams::Reader &params,
                            ::CPMDParams::Builder out) {
  out.setFunctional(params.getFunctional());
  out.setCutOffRy(params.getCutOffRy());
  out.setCharge(params.getCharge());
  out.setMultiplicity(params.getMultiplicity());
  out.setTask(params.getTask());
  out.setTitle(params.getTitle());
  out.setMemoryMb(params.getMemoryMb());
  out.setScratchDir(params.getScratchDir());
  out.setPermanentDir(params.getPermanentDir());
  out.setCpmdRoot(params.getCpmdRoot());
  out.setEnginePath(params.getEnginePath());
  const auto blocks = params.getInputBlocks();
  auto out_blocks = out.initInputBlocks(blocks.size());
  for (unsigned int i = 0; i < blocks.size(); ++i)
    out_blocks.set(i, blocks[i]);
  out.setInputSections(params.getInputSections());
}

::CPMDParams::Reader read_params_words(
    const std::vector<::capnp::word> &params_words,
    ::capnp::FlatArrayMessageReader &reader) {
  (void)params_words;
  return reader.getRoot<::CPMDParams>();
}

std::mutex g_probe_mu;
bool g_probe_done = false;
bool g_probe_ok = false;
bool g_abi_probe_done = false;
bool g_abi_probe_ok = false;

} // namespace

struct CPMDPot::Impl {
  EngineBundle bundle;
  std::vector<::capnp::word> params_words;
  std::string engine_path;
  std::string cpmd_root;
  mutable std::vector<double> grad_scratch;
};

CPMDPot::CPMDPot() : Potential(PotType::CPMD), impl_(new Impl) {
  impl_->params_words = default_params();
  apply_env_hints(impl_->cpmd_root);
  if (try_load_engine(impl_->bundle, impl_->engine_path))
    (void)push_params_to_engine(impl_->bundle, impl_->params_words);
}

CPMDPot::CPMDPot(const ::CPMDParams::Reader &params)
    : Potential(PotType::CPMD), impl_(new Impl) {
  impl_->params_words = serialize_params(params);
  impl_->engine_path = params.getEnginePath().cStr();
  impl_->cpmd_root = params.getCpmdRoot().cStr();
  apply_env_hints(impl_->cpmd_root);
  if (try_load_engine(impl_->bundle, impl_->engine_path))
    (void)push_params_to_engine(impl_->bundle, impl_->params_words);
}

CPMDPot::~CPMDPot() { delete impl_; }

bool CPMDPot::setParams(const ::CPMDParams::Reader &params) {
  if (!impl_)
    impl_ = new Impl;

  const std::string next_engine_path = params.getEnginePath().cStr();
  const std::string next_cpmd_root = params.getCpmdRoot().cStr();
  const bool need_reload =
      !impl_->bundle.loaded || next_engine_path != impl_->engine_path;

  impl_->params_words = serialize_params(params);
  impl_->engine_path = next_engine_path;
  impl_->cpmd_root = next_cpmd_root;
  apply_env_hints(impl_->cpmd_root);

  if (need_reload) {
    impl_->bundle = EngineBundle{};
    if (!try_load_engine(impl_->bundle, impl_->engine_path))
      return false;
  } else if (!impl_->bundle.loaded) {
    if (!try_load_engine(impl_->bundle, impl_->engine_path))
      return false;
  }
  if (!impl_->bundle.loaded)
    return false;
  return push_params_to_engine(impl_->bundle, impl_->params_words);
}

void CPMDPot::getParams(::CPMDParams::Builder out) const {
  if (!impl_ || impl_->params_words.empty()) {
    ::capnp::MallocMessageBuilder msg;
    auto params = msg.initRoot<::CPMDParams>();
    copy_params_to_builder(params.asReader(), out);
    return;
  }

  auto words = kj::arrayPtr<const ::capnp::word>(impl_->params_words.data(),
                                                 impl_->params_words.size());
  ::capnp::FlatArrayMessageReader reader(words);
  auto params = read_params_words(impl_->params_words, reader);
  copy_params_to_builder(params, out);
}

bool CPMDPot::setPotentialConfig(const ::PotentialConfig::Reader &cfg,
                                 std::string *message_out) {
  switch (cfg.which()) {
  case ::PotentialConfig::NONE:
    if (message_out)
      *message_out = "no-op (rgpot params: none)";
    return true;
  case ::PotentialConfig::CPMD: {
    const auto cp = cfg.getCpmd();
    const bool ok = setParams(cp);
    if (message_out) {
      const std::string sum = params_summary(cp);
      if (ok)
        *message_out = "rgpot params applied (cpmd arm): " + sum;
      else if (!available())
        *message_out =
            "rgpot params cpmd arm failed (engine not loaded): " + sum;
      else
        *message_out = "rgpot params cpmd arm rejected by embed: " + sum;
    }
    return ok;
  }
  default:
    if (message_out)
      *message_out =
          "rgpot params arm not handled by CPMDPot (use matching backend pot)";
    return false;
  }
}

bool CPMDPot::available() const {
  return impl_ && impl_->bundle.loaded && impl_->bundle.energy_gradient &&
         impl_->bundle.set_params;
}

bool CPMDPot::probe_available() {
  std::lock_guard<std::mutex> lock(g_probe_mu);
  if (g_probe_done)
    return g_probe_ok;
  EngineBundle tmp;
  g_probe_ok = try_load_engine(tmp, "");
  g_probe_done = true;
  return g_probe_ok;
}

bool CPMDPot::abi_available() {
  std::lock_guard<std::mutex> lock(g_probe_mu);
  if (g_abi_probe_done)
    return g_abi_probe_ok;
  EngineBundle tmp;
  if (!try_load_engine(tmp, "")) {
    g_abi_probe_ok = false;
  } else if (tmp.available) {
    g_abi_probe_ok = tmp.available() != 0;
  } else {
    g_abi_probe_ok = false;
  }
  g_abi_probe_done = true;
  return g_abi_probe_ok;
}

void CPMDPot::forceImpl(const ForceInput &in, ForceOut *out) const {
  if (!available()) {
    throw std::runtime_error(
        std::string("CPMD engine (libcpmdc) not loaded: ") +
        (impl_ ? impl_->bundle.load_error : "no impl"));
  }

  const int n = static_cast<int>(in.nAtoms);
  if (n <= 0)
    throw std::runtime_error("CPMDPot: nAtoms must be positive");
  if (!in.pos || !in.atmnrs || !out || !out->F)
    throw std::runtime_error("CPMDPot: null positions/atmnrs/forces buffer");

  std::vector<double> &grad = impl_->grad_scratch;
  grad.assign(static_cast<size_t>(n) * 3u, 0.0);
  const ParamsView params = params_view(impl_->params_words);
  CPMDCResult res = impl_->bundle.energy_gradient(
      n, in.pos, in.atmnrs, params.data, params.size, grad.data());

  if (!res.ok) {
    throw std::runtime_error(std::string("CPMD engine failed: ") + res.message);
  }

  out->energy = res.energy_h * HARTREE_TO_EV;
  out->variance = 0.0;
  for (int i = 0; i < n * 3; ++i)
    out->F[static_cast<size_t>(i)] =
        grad[static_cast<size_t>(i)] * NEG_GRAD_TO_FORCE;
}

} // namespace rgpot
