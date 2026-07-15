// MIT License — Metatomic engine C ABI (torch linked into this .so only)
#define RGPOT_MTA_ENGINE_BUILD
#include "metatomic_c_abi.h"

#include "rgpot/MetatomicPot/MetatomicPot.hpp"
#include "rgpot/ForceStructs.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

struct RgpotMtaPot {
  std::unique_ptr<rgpot::MetatomicPot> impl;
};

static const char *nz(const char *s) { return s ? s : ""; }

static void set_err(char *errbuf, size_t errlen, const char *msg) {
  if (!errbuf || errlen == 0)
    return;
  std::snprintf(errbuf, errlen, "%s", msg ? msg : "unknown");
}

extern "C" {

int rgpot_mta_abi_version(void) { return RGPOT_MTA_ABI_VERSION; }

int rgpot_mta_available(void) { return 1; }

RgpotMtaPot *rgpot_mta_create(const RgpotMtaConfig *cfg, char *errbuf,
                              size_t errlen) {
  if (!cfg || !cfg->model_path || cfg->model_path[0] == '\0') {
    set_err(errbuf, errlen, "rgpot_mta_create: model_path required");
    return nullptr;
  }
  try {
    rgpot::MetatomicConfig c;
    c.model_path = cfg->model_path;
    c.device = nz(cfg->device)[0] ? cfg->device : "cpu";
    c.length_unit = nz(cfg->length_unit)[0] ? cfg->length_unit : "angstrom";
    c.extensions_directory = nz(cfg->extensions_directory);
    c.check_consistency = cfg->check_consistency != 0;
    c.uncertainty_threshold = cfg->uncertainty_threshold;
    c.dtype_override = nz(cfg->dtype_override);
    c.random_rotation = cfg->random_rotation != 0;
    c.n_symmetry_rotations = cfg->n_symmetry_rotations;
    c.so3_probe_scatter = cfg->so3_probe_scatter != 0;
    c.torch_determinism = cfg->torch_determinism_strict
                              ? rgpot::TorchDeterminismPolicy::Strict
                              : rgpot::TorchDeterminismPolicy::Fast;
    auto pot = std::make_unique<RgpotMtaPot>();
    pot->impl = std::make_unique<rgpot::MetatomicPot>(c);
    return pot.release();
  } catch (const std::exception &e) {
    set_err(errbuf, errlen, e.what());
    return nullptr;
  } catch (...) {
    set_err(errbuf, errlen, "rgpot_mta_create: unknown exception");
    return nullptr;
  }
}

void rgpot_mta_destroy(RgpotMtaPot *pot) { delete pot; }

int rgpot_mta_force(RgpotMtaPot *pot, long nAtoms, const double *positions,
                    const int *atomicNrs, double *forces, double *energy,
                    double *variance, const double *box) {
  if (!pot || !pot->impl || !positions || !atomicNrs || !forces || !energy ||
      !box || nAtoms <= 0)
    return 1;
  try {
    std::vector<double> Fbuf(static_cast<size_t>(nAtoms) * 3);
    rgpot::ForceOut out{Fbuf.data(), 0.0, 0.0};
    rgpot::ForceInput in{static_cast<size_t>(nAtoms), positions, atomicNrs,
                         box};
    pot->impl->forceImpl(in, &out);
    *energy = out.energy;
    if (variance)
      *variance = out.variance;
    std::memcpy(forces, Fbuf.data(), Fbuf.size() * sizeof(double));
    return 0;
  } catch (const std::exception &e) {
    // Best-effort: stderr so nanobind/dlopen callers can diagnose.
    std::fprintf(stderr, "rgpot_mta_force: %s\n", e.what());
    return 2;
  } catch (...) {
    std::fprintf(stderr, "rgpot_mta_force: unknown exception\n");
    return 2;
  }
}

} // extern "C"
