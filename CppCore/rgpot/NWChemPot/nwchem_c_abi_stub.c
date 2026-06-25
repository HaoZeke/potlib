/**
 * @file nwchem_c_abi_stub.c
 * @brief Stub NWChem C ABI (always built; no NWChem link).
 */

#include "nwchem_c_abi.h"

#include <stdio.h>

static const char *STUB_VERSION = "nwchemc-stub/1.0.0";

int nwchemc_set_params(const void *params_capnp,
                       size_t params_capnp_size_bytes) {
  (void)params_capnp;
  (void)params_capnp_size_bytes;
  return -1;
}

NWChemCResult nwchemc_energy_gradient(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    const void *params_capnp, size_t params_capnp_size_bytes,
    double *grad_h_bohr) {
  (void)n_atoms;
  (void)positions_ang;
  (void)atomic_numbers;
  (void)params_capnp;
  (void)params_capnp_size_bytes;
  (void)grad_h_bohr;
  NWChemCResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  snprintf(r.message, sizeof(r.message),
           "NWChem embed not available (stub). Build libnwchemc with "
           "-Dwith_nwchem=true -Dnwchem_root=<NWCHEM_TOP>.");
  return r;
}

const char *nwchemc_version(void) { return STUB_VERSION; }

int nwchemc_available(void) { return 0; }
