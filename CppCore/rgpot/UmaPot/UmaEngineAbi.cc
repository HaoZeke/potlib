// MIT License — UMA engine behind the generic rgpot engine C ABI
// (torch linked into this .so only).
#define RGPOT_ENGINE_BUILD
#include "rgpot/engine_c_abi.h"

#include "rgpot/ForceStructs.hpp"
#include "rgpot/UmaPot/UmaPot.hpp"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <memory>
#include <string>
#include <vector>

// When this engine is called from a Python host (pyeonclient / nanobind) the
// GIL is often still held. Torch autograd refuses that. Soft-resolve CPython
// thread APIs so pure C++ hosts (eonclient binary) need no libpython link.
//
// Only SaveThread when PyGILState_Check() is true — nested release (Job.run
// already dropped the GIL) must not call SaveThread again.
namespace {
struct SoftGilRelease {
  using CheckFn = int (*)();
  using SaveFn = void *(*)();
  using RestoreFn = void (*)(void *);
  void *tstate = nullptr;
  RestoreFn restore = nullptr;
  SoftGilRelease() {
    auto *check =
        reinterpret_cast<CheckFn>(dlsym(RTLD_DEFAULT, "PyGILState_Check"));
    auto *save =
        reinterpret_cast<SaveFn>(dlsym(RTLD_DEFAULT, "PyEval_SaveThread"));
    restore = reinterpret_cast<RestoreFn>(
        dlsym(RTLD_DEFAULT, "PyEval_RestoreThread"));
    if (check && check() && save && restore) {
      tstate = save();
    }
  }
  ~SoftGilRelease() {
    if (tstate && restore) {
      restore(tstate);
    }
  }
  SoftGilRelease(const SoftGilRelease &) = delete;
  SoftGilRelease &operator=(const SoftGilRelease &) = delete;
};

// Flat-JSON key lookups, matching the sidecar reader's tolerance: a
// missing or malformed key keeps the fallback.
double json_number(const std::string &text, const char *key, double fallback) {
  const std::string pat = std::string("\"") + key + "\"";
  const auto pos = text.find(pat);
  if (pos == std::string::npos)
    return fallback;
  const auto colon = text.find(':', pos + pat.size());
  if (colon == std::string::npos)
    return fallback;
  try {
    return std::stod(text.substr(colon + 1));
  } catch (...) {
    return fallback;
  }
}

std::string json_string(const std::string &text, const char *key,
                        const std::string &fallback) {
  const std::string pat = std::string("\"") + key + "\"";
  const auto pos = text.find(pat);
  if (pos == std::string::npos)
    return fallback;
  const auto colon = text.find(':', pos + pat.size());
  if (colon == std::string::npos)
    return fallback;
  const auto open = text.find('"', colon + 1);
  if (open == std::string::npos)
    return fallback;
  const auto close = text.find('"', open + 1);
  if (close == std::string::npos)
    return fallback;
  return text.substr(open + 1, close - open - 1);
}
} // namespace

struct RgpotEnginePot {
  std::unique_ptr<rgpot::UmaPot> impl;
};

static void set_err(char *errbuf, size_t errlen, const char *msg) {
  if (!errbuf || errlen == 0)
    return;
  std::snprintf(errbuf, errlen, "%s", msg ? msg : "unknown");
}

extern "C" {

int rgpot_engine_abi_version(void) { return RGPOT_ENGINE_ABI_VERSION; }

int rgpot_engine_available(void) { return 1; }

RgpotEnginePot *rgpot_engine_create(const char *config_json, char *errbuf,
                                    size_t errlen) {
  if (!config_json || config_json[0] == '\0') {
    set_err(errbuf, errlen, "rgpot_engine_create(uma): config required");
    return nullptr;
  }
  const std::string text(config_json);
  try {
    rgpot::UmaConfig c;
    c.model_path = json_string(text, "model_path", "");
    if (c.model_path.empty()) {
      set_err(errbuf, errlen, "rgpot_engine_create(uma): model_path required");
      return nullptr;
    }
    c.task_name = json_string(text, "task_name", c.task_name);
    c.device = json_string(text, "device", c.device);
    c.charge = static_cast<int>(
        json_number(text, "charge", static_cast<double>(c.charge)));
    c.spin = static_cast<int>(
        json_number(text, "spin", static_cast<double>(c.spin)));
    if (c.spin <= 0)
      c.spin = 1;
    const double cutoff = json_number(text, "cutoff", 0.0);
    if (cutoff > 0.0)
      c.cutoff = cutoff;
    const int max_neighbors =
        static_cast<int>(json_number(text, "max_neighbors", 0.0));
    if (max_neighbors > 0)
      c.max_neighbors = max_neighbors;
    auto pot = std::make_unique<RgpotEnginePot>();
    pot->impl = std::make_unique<rgpot::UmaPot>(c);
    return pot.release();
  } catch (const std::exception &e) {
    set_err(errbuf, errlen, e.what());
    return nullptr;
  } catch (...) {
    set_err(errbuf, errlen, "rgpot_engine_create(uma): unknown exception");
    return nullptr;
  }
}

void rgpot_engine_destroy(RgpotEnginePot *pot) { delete pot; }

int rgpot_engine_force(RgpotEnginePot *pot, long nAtoms,
                       const double *positions, const int *atomicNrs,
                       double *forces, double *energy, double *variance,
                       const double *box,
                       rgpot_engine_coord_transform transform,
                       void *transform_user) {
  if (!pot || !pot->impl || !positions || !atomicNrs || !forces || !energy ||
      !box || nAtoms <= 0)
    return 1;
  try {
    SoftGilRelease no_gil;
    // Scratch copies: a host transform must never mutate caller buffers.
    std::vector<double> R(positions, positions + 3 * nAtoms);
    std::vector<double> cell(box, box + 9);
    if (transform) {
      const int rc = transform(transform_user, nAtoms, R.data(), cell.data());
      if (rc != 0)
        return rc;
    }
    std::vector<double> Fbuf(static_cast<size_t>(nAtoms) * 3);
    rgpot::ForceOut out{Fbuf.data(), 0.0, 0.0};
    rgpot::ForceInput in{static_cast<size_t>(nAtoms), R.data(), atomicNrs,
                         cell.data()};
    pot->impl->forceImpl(in, &out);
    *energy = out.energy;
    if (variance)
      *variance = out.variance;
    std::memcpy(forces, Fbuf.data(), Fbuf.size() * sizeof(double));
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "rgpot_engine_force(uma): %s\n", e.what());
    return 2;
  } catch (...) {
    std::fprintf(stderr, "rgpot_engine_force(uma): unknown exception\n");
    return 2;
  }
}

} // extern "C"
