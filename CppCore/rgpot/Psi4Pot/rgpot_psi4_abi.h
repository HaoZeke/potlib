/**
 * @file rgpot_psi4_abi.h
 * @brief Stable C ABI between rgpot Psi4Pot frontend and libpsi4_engine.
 *
 * Frontend (Psi4Pot.cc) dlopens libpsi4 (RTLD_GLOBAL) then libpsi4_engine and
 * resolves these symbols. No Python runtime; engine uses Psi4 C++ APIs only.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Result of an energy+gradient evaluation (atomic units in, caller converts). */
typedef struct RgpotPsi4Result {
  int ok;              /**< 1 on success, 0 on failure */
  double energy_h;     /**< Total energy in Hartree */
  char message[512];   /**< Error / status message (NUL-terminated) */
} RgpotPsi4Result;

/**
 * BLYP energy + nuclear gradient (Hartree, Hartree/Bohr).
 *
 * @param n_atoms          Number of atoms
 * @param positions_ang    n_atoms*3 positions in Angstrom (row-major xyz)
 * @param atomic_numbers   n_atoms atomic numbers (Z)
 * @param charge           Molecular charge
 * @param multiplicity     Spin multiplicity (2S+1)
 * @param grad_h_bohr      Output n_atoms*3 gradient in Hartree/Bohr (row-major)
 * @param data_dir         PSIDATADIR (basis/*.gbs); may be NULL/empty for env/default
 */
RgpotPsi4Result rgpot_psi4_blyp_energy_grad(int n_atoms,
                                            const double *positions_ang,
                                            const int *atomic_numbers,
                                            int charge, int multiplicity,
                                            double *grad_h_bohr,
                                            const char *data_dir);

/**
 * Same as rgpot_psi4_blyp_energy_grad with explicit basis set name (e.g. "sto-3g").
 *
 * @param basis_name       Gaussian94 basis key (sanitized to basis/<name>.gbs)
 */
RgpotPsi4Result rgpot_psi4_blyp_energy_grad_basis(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    int charge, int multiplicity, double *grad_h_bohr, const char *data_dir,
    const char *basis_name);

/** Engine build / version string (NUL-terminated static or caller-owned buffer). */
const char *rgpot_psi4_engine_version(void);

#ifdef __cplusplus
}
#endif
