/**
 * @file nwchem_c_abi_stub.c
 * @brief Stub implementation of the NWChem C ABI (no NWChem link required).
 *
 * Always compiled into libnwchem_abi_stub.a. Used when with_nwchem is off or
 * when the full engine is not built. Frontend always links this for probe
 * fallbacks; the optional libnwchem_engine.so overrides at dlopen time.
 */

#include "nwchem_c_abi.h"

#include <stdio.h>
#include <string.h>

static const char *STUB_VERSION = "rgpot-nwchem-stub/1.0.0";

RgpotNWChemResult rgpot_nwchem_energy_grad(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    int charge, int multiplicity, const char *basis, const char *theory,
    const char *scf_type, double *grad_h_bohr) {
  (void)n_atoms;
  (void)positions_ang;
  (void)atomic_numbers;
  (void)charge;
  (void)multiplicity;
  (void)basis;
  (void)theory;
  (void)scf_type;
  (void)grad_h_bohr;
  RgpotNWChemResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  snprintf(r.message, sizeof(r.message),
           "NWChem embed not available (stub only). Build libnwchem_engine with "
           "-Dwith_nwchem=true -Dnwchem_root=<NWCHEM_TOP> and set "
           "RGPOT_NWCHEM_ENGINE to that .so (in-process C ABI, no CLI).");
  return r;
}

int rgpot_nwchem_set_config(const char *basis, const char *theory,
                            const char *scf_type, int charge, int mult) {
  (void)basis;
  (void)theory;
  (void)scf_type;
  (void)charge;
  (void)mult;
  return -1; /* stub: config not applied */
}

const char *rgpot_nwchem_engine_version(void) { return STUB_VERSION; }

int rgpot_nwchem_abi_available(void) { return 0; }
