/**
 * @file cpmd_c_abi.h
 * @brief C ABI between CPMDPot frontend and libcpmdc.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CPMDCResult {
  int ok;
  double energy_h;
  char message[512];
} CPMDCResult;

int cpmdc_set_params(const void *params_capnp, size_t params_capnp_size_bytes);

CPMDCResult cpmdc_energy_gradient(int n_atoms, const double *positions_ang,
                                  const int *atomic_numbers,
                                  const void *params_capnp,
                                  size_t params_capnp_size_bytes,
                                  double *grad_h_bohr);

const char *cpmdc_version(void);

int cpmdc_available(void);

#ifdef __cplusplus
}
#endif
