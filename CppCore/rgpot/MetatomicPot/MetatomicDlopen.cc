// MIT License — dlopen frontend for libmetatomic_engine.so
#include "rgpot/MetatomicPot/MetatomicDlopen.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

namespace rgpot {

namespace {

void *open_lib(const char *path) {
#ifdef _WIN32
  return reinterpret_cast<void *>(LoadLibraryA(path));
#else
  return dlopen(path, RTLD_NOW | RTLD_GLOBAL);
#endif
}

void close_lib(void *h) {
  if (!h)
    return;
#ifdef _WIN32
  FreeLibrary(reinterpret_cast<HMODULE>(h));
#else
  dlclose(h);
#endif
}

void *sym(void *h, const char *name) {
#ifdef _WIN32
  return reinterpret_cast<void *>(
      GetProcAddress(reinterpret_cast<HMODULE>(h), name));
#else
  return dlsym(h, name);
#endif
}

std::vector<std::string> candidate_paths(const MetatomicConfig &config) {
  std::vector<std::string> out;
  if (!config.engine_path.empty())
    out.push_back(config.engine_path);
  if (const char *e = std::getenv("RGPOT_METATOMIC_ENGINE"))
    if (e && *e)
      out.emplace_back(e);
  if (const char *e = std::getenv("METATOMIC_ENGINE"))
    if (e && *e)
      out.emplace_back(e);
#ifdef _WIN32
  out.emplace_back("metatomic_engine.dll");
  out.emplace_back("libmetatomic_engine.dll");
#elif defined(__APPLE__)
  out.emplace_back("libmetatomic_engine.dylib");
  out.emplace_back("libmetatomic_engine.so");
#else
  out.emplace_back("libmetatomic_engine.so");
#endif
  // Directory search via EON_POTENTIALS_PATH / RGPOT_ENGINE_PATH
  auto append_dirs = [&](const char *env) {
    if (!env || !*env)
      return;
    std::string s(env);
    size_t start = 0;
    while (start < s.size()) {
      auto pos = s.find(':', start);
      if (pos == std::string::npos)
        pos = s.size();
      if (pos > start) {
        std::string dir = s.substr(start, pos - start);
        if (!dir.empty() && dir.back() != '/')
          dir += '/';
#ifdef __APPLE__
        out.push_back(dir + "libmetatomic_engine.dylib");
#endif
        out.push_back(dir + "libmetatomic_engine.so");
      }
      start = pos + 1;
    }
  };
  append_dirs(std::getenv("EON_POTENTIALS_PATH"));
  append_dirs(std::getenv("RGPOT_ENGINE_PATH"));
  return out;
}

RgpotMtaConfig to_c_config(const MetatomicConfig &c) {
  RgpotMtaConfig out{};
  out.model_path = c.model_path.c_str();
  out.device = c.device.c_str();
  out.length_unit = c.length_unit.c_str();
  out.extensions_directory = c.extensions_directory.c_str();
  out.check_consistency = c.check_consistency ? 1 : 0;
  out.uncertainty_threshold = c.uncertainty_threshold;
  out.dtype_override = c.dtype_override.c_str();
  out.random_rotation = c.random_rotation ? 1 : 0;
  out.n_symmetry_rotations = c.n_symmetry_rotations;
  out.so3_probe_scatter = c.so3_probe_scatter ? 1 : 0;
  out.torch_determinism_strict =
      (c.torch_determinism == TorchDeterminismPolicy::Strict) ? 1 : 0;
  return out;
}

} // namespace

MetatomicDlopen::MetatomicDlopen(const MetatomicConfig &config) {
  for (const auto &path : candidate_paths(config)) {
    m_lib = open_lib(path.c_str());
    if (m_lib)
      break;
  }
  if (!m_lib)
    throw std::runtime_error(
        "MetatomicDlopen: libmetatomic_engine.so not found "
        "(set RGPOT_METATOMIC_ENGINE / engine_path / EON_POTENTIALS_PATH)");

  auto abi = reinterpret_cast<int (*)(void)>(sym(m_lib, "rgpot_mta_abi_version"));
  m_create = reinterpret_cast<create_fn>(sym(m_lib, "rgpot_mta_create"));
  m_destroy = reinterpret_cast<destroy_fn>(sym(m_lib, "rgpot_mta_destroy"));
  m_force = reinterpret_cast<force_fn>(sym(m_lib, "rgpot_mta_force"));
  if (!abi || !m_create || !m_destroy || !m_force) {
    close_lib(m_lib);
    m_lib = nullptr;
    throw std::runtime_error(
        "MetatomicDlopen: missing C ABI symbols in engine library");
  }
  if (abi() != RGPOT_MTA_ABI_VERSION) {
    close_lib(m_lib);
    m_lib = nullptr;
    throw std::runtime_error("MetatomicDlopen: ABI version mismatch");
  }
  char err[1024]{};
  auto cfg = to_c_config(config);
  m_pot = m_create(&cfg, err, sizeof err);
  if (!m_pot) {
    close_lib(m_lib);
    m_lib = nullptr;
    throw std::runtime_error(std::string("MetatomicDlopen: create failed: ") +
                             err);
  }
}

MetatomicDlopen::~MetatomicDlopen() {
  // Keep the engine mapped: torch/metatomic static teardown after dlclose
  // routinely SEGV at process exit.
  if (m_pot && m_destroy)
    m_destroy(m_pot);
  m_pot = nullptr;
  m_lib = nullptr;
}

void MetatomicDlopen::forceImpl(const ForceInput &in, ForceOut *out) const {
  if (!m_pot || !m_force)
    throw std::runtime_error("MetatomicDlopen: engine not available");
  const int rc =
      m_force(m_pot, static_cast<long>(in.nAtoms), in.pos, in.atmnrs, out->F,
              &out->energy, &out->variance, in.box);
  if (rc != 0)
    throw std::runtime_error("MetatomicDlopen: force failed rc=" +
                             std::to_string(rc));
}

} // namespace rgpot
