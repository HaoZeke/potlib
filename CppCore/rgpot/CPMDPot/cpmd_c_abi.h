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

typedef struct CPMDCSession CPMDCSession;

int cpmdc_set_params(const void *params_capnp, size_t params_capnp_size_bytes);

CPMDCResult cpmdc_energy_gradient(int n_atoms, const double *positions_ang,
                                  const int *atomic_numbers,
                                  const void *params_capnp,
                                  size_t params_capnp_size_bytes,
                                  double *grad_h_bohr);

CPMDCSession *cpmdc_session_create(const void *params_capnp,
                                   size_t params_capnp_size_bytes);

void cpmdc_session_destroy(CPMDCSession *session);

size_t cpmdc_potential_result_size_for_force_input(
    const void *force_input_capnp, size_t force_input_capnp_size_bytes);

CPMDCResult cpmdc_session_calculate_result(
    CPMDCSession *session, const void *force_input_capnp,
    size_t force_input_capnp_size_bytes, void *potential_result_capnp,
    size_t potential_result_capnp_capacity_bytes,
    size_t *potential_result_capnp_size_bytes);

const char *cpmdc_version(void);

int cpmdc_available(void);

#ifdef __cplusplus
}
#endif
