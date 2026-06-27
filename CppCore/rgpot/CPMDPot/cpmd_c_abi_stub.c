/*
 * @file cpmd_c_abi_stub.c
 * @brief No-op cpmdc ABI for link-time conformance tests.
 */
#include "cpmd_c_abi.h"

#include <stdio.h>

static const char *STUB_VERSION = "cpmdc-stub/1.0.0";

int cpmdc_set_params(const void *params_capnp, size_t params_capnp_size_bytes) {
  (void)params_capnp;
  (void)params_capnp_size_bytes;
  return -1;
}

CPMDCResult cpmdc_energy_gradient(int n_atoms, const double *positions_ang,
                                  const int *atomic_numbers,
                                  const void *params_capnp,
                                  size_t params_capnp_size_bytes,
                                  double *grad_h_bohr) {
  (void)n_atoms;
  (void)positions_ang;
  (void)atomic_numbers;
  (void)params_capnp;
  (void)params_capnp_size_bytes;
  (void)grad_h_bohr;
  CPMDCResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  snprintf(r.message, sizeof(r.message),
           "CPMD embed not available (stub). Provide the split cpmdc engine "
           "(libcpmdc) on the dlopen path.");
  return r;
}

CPMDCSession *cpmdc_session_create(const void *params_capnp,
                                   size_t params_capnp_size_bytes) {
  (void)params_capnp;
  (void)params_capnp_size_bytes;
  return NULL;
}

void cpmdc_session_destroy(CPMDCSession *session) { (void)session; }

size_t cpmdc_potential_result_size_for_force_input(
    const void *force_input_capnp, size_t force_input_capnp_size_bytes) {
  (void)force_input_capnp;
  (void)force_input_capnp_size_bytes;
  return 0;
}

CPMDCResult cpmdc_session_calculate_result(
    CPMDCSession *session, const void *force_input_capnp,
    size_t force_input_capnp_size_bytes, void *potential_result_capnp,
    size_t potential_result_capnp_capacity_bytes,
    size_t *potential_result_capnp_size_bytes) {
  (void)session;
  (void)force_input_capnp;
  (void)force_input_capnp_size_bytes;
  (void)potential_result_capnp;
  (void)potential_result_capnp_capacity_bytes;
  (void)potential_result_capnp_size_bytes;
  CPMDCResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  snprintf(r.message, sizeof(r.message),
           "CPMD embed not available (stub). Provide the split cpmdc engine "
           "(libcpmdc) on the dlopen path.");
  return r;
}

const char *cpmdc_version(void) { return STUB_VERSION; }

int cpmdc_available(void) { return 0; }
