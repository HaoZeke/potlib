/**
 * @file nwchem_c_abi.h
 * @brief Stable C ABI between rgpot NWChemPot frontend and libnwchem_engine.
 *
 * Frontend (NWChemPot.cc) dlopens libnwchem_engine and resolves these symbols.
 * The engine may embed NWChem via Fortran (nwchem_embed.F) when built with
 * RGPOT_HAS_NWCHEM; otherwise a stub implementation returns not-available.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Result of an energy+gradient evaluation (atomic units in, caller converts). */
typedef struct RgpotNWChemResult {
  int ok;              /**< 1 on success, 0 on failure */
  double energy_h;     /**< Total energy in Hartree */
  char message[512];   /**< Error / status message (NUL-terminated) */
} RgpotNWChemResult;

/**
 * NWChem SCF (or configured theory) energy + nuclear gradient.
 *
 * Positions are Angstrom (rgpot convention); engine converts internally.
 * Gradient output is Hartree/Bohr (row-major xyz).
 *
 * @param n_atoms          Number of atoms
 * @param positions_ang    n_atoms*3 positions in Angstrom (row-major xyz)
 * @param atomic_numbers   n_atoms atomic numbers (Z)
 * @param charge           Molecular charge
 * @param multiplicity     Spin multiplicity (2S+1)
 * @param basis            Basis set name (e.g. "sto-3g"); may be NULL
 * @param theory           Theory level (e.g. "scf", "dft"); may be NULL
 * @param scf_type         SCF type (e.g. "rhf", "uhf"); may be NULL
 * @param grad_h_bohr      Output n_atoms*3 gradient in Hartree/Bohr (row-major)
 */
RgpotNWChemResult rgpot_nwchem_energy_grad(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    int charge, int multiplicity, const char *basis, const char *theory,
    const char *scf_type, double *grad_h_bohr);

/**
 * Apply runtime configuration before the first energy/grad call.
 * Optional; engines may ignore keys they do not understand.
 *
 * @param basis      Basis set name
 * @param theory     Theory level string
 * @param scf_type   SCF type string
 * @param charge     Molecular charge
 * @param mult       Spin multiplicity
 * @return 0 on success, non-zero on failure
 */
int rgpot_nwchem_set_config(const char *basis, const char *theory,
                            const char *scf_type, int charge, int mult);

/** Engine build / version string (NUL-terminated static buffer). */
const char *rgpot_nwchem_engine_version(void);

/**
 * Probe whether the engine has a real NWChem embed (not stub-only).
 * @return 1 if real embed is available, 0 if stub/unavailable
 */
int rgpot_nwchem_abi_available(void);

#ifdef __cplusplus
}
#endif
