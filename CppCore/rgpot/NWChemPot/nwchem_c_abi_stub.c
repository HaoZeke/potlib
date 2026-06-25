/**
 * @file nwchem_c_abi_stub.c
 * @brief Stub NWChem C ABI (always built; no NWChem link).
 */

#include "nwchem_c_abi.h"

#include <stdio.h>
#include <string.h>

static const char *STUB_VERSION = "rgpot-nwchem-stub/1.0.0";

void rgpot_nwchem_params_default(RgpotNWChemParams *p) {
  if (!p)
    return;
  memset(p, 0, sizeof(*p));
  snprintf(p->basis, sizeof(p->basis), "sto-3g");
  snprintf(p->theory, sizeof(p->theory), "scf");
  snprintf(p->scf_type, sizeof(p->scf_type), "rhf");
  p->charge = 0;
  p->multiplicity = 1;
}

int rgpot_nwchem_set_params(const RgpotNWChemParams *params) {
  (void)params;
  return -1;
}

RgpotNWChemResult rgpot_nwchem_energy_grad(int n_atoms,
                                           const double *positions_ang,
                                           const int *atomic_numbers,
                                           const RgpotNWChemParams *params,
                                           double *grad_h_bohr) {
  (void)n_atoms;
  (void)positions_ang;
  (void)atomic_numbers;
  (void)params;
  (void)grad_h_bohr;
  RgpotNWChemResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  snprintf(r.message, sizeof(r.message),
           "NWChem embed not available (stub). Build libnwchem_engine with "
           "-Dwith_nwchem=true -Dnwchem_root=<NWCHEM_TOP>.");
  return r;
}

int rgpot_nwchem_set_config(const char *basis, const char *theory,
                            const char *scf_type, int charge, int mult) {
  (void)basis;
  (void)theory;
  (void)scf_type;
  (void)charge;
  (void)mult;
  return -1;
}

const char *rgpot_nwchem_engine_version(void) { return STUB_VERSION; }

int rgpot_nwchem_abi_available(void) { return 0; }
