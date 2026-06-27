/**
 * @file nwchem_c_abi.h
 * @brief C ABI between NWChemPot frontend and libnwchemc.so.
 *
 * Method options cross the ABI as an unpacked flat Cap'n Proto message whose
 * root is `NWChemParams` from Potentials.capnp. The buffer can come from
 * pycapnp, mmap, or another language binding that writes the standard flat
 * Cap'n Proto stream format.
 *
 * Units at ABI: geometry Angstrom, energy Hartree, gradient Hartree/Bohr.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Result of energy+gradient (atomic units). */
typedef struct NWChemCResult {
  int ok;            /**< 1 success, 0 failure */
  double energy_h;   /**< total energy Hartree */
  char message[512]; /**< status / error, NUL-terminated */
} NWChemCResult;

/**
 * Apply sticky method options from an unpacked flat `NWChemParams` message.
 *
 * @return 0 on success, non-zero if embed unavailable or params are invalid
 */
int nwchemc_set_params(const void *params_capnp,
                       size_t params_capnp_size_bytes);

/**
 * Energy + nuclear gradient.
 *
 * @param n_atoms                 atom count
 * @param positions_ang           n_atoms*3 Angstrom, row-major xyz
 * @param atomic_numbers          n_atoms Z values
 * @param params_capnp            unpacked flat `NWChemParams` message
 * @param params_capnp_size_bytes size of params_capnp in bytes
 * @param grad_h_bohr             out n_atoms*3 Hartree/Bohr, row-major
 */
NWChemCResult nwchemc_energy_gradient(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    const void *params_capnp, size_t params_capnp_size_bytes,
    double *grad_h_bohr);

/** Engine version string (static buffer). */
const char *nwchemc_version(void);

/** 1 if real embed (RGPOT_HAS_NWCHEM), 0 if stub. */
int nwchemc_available(void);

#ifdef __cplusplus
}
#endif
