/**
 * @file nwchem_c_abi.h
 * @brief Embed-only C ABI between NWChemPot frontend and libnwchem_engine.so.
 *
 * **Not** the user configuration format. Users pass options as Cap'n Proto
 * `NWChemParams` (Potentials.capnp); the frontend copies fields into this POD
 * only at the embed boundary (fixed buffers for Fortran/C embed code).
 *
 * Build engine: -Dwith_nwchem=true -Dnwchem_root=<NWCHEM_TOP> (nwchem_c_abi.c +
 * nwchem_embed.F). Without embed, only the stub exists (abi_available == 0).
 *
 * theory / scf_type examples:
 *   theory="scf",  scf_type="rhf"|"uhf"     — Hartree–Fock
 *   theory="dft",  scf_type="blyp"|"b3lyp"  — DFT + XC
 *   theory="blyp"  (scf_type ignored/copied) — embed maps to dft + xc=blyp
 *
 * Units at ABI: energy Hartree, gradient Hartree/Bohr. Frontend -> eV, eV/Å.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fixed-size string slots in RgpotNWChemParams (NUL-terminated C strings). */
#define RGPOT_NWCHEM_STR 64
#define RGPOT_NWCHEM_PATH 512

/**
 * @brief All engine/runtime options in one POD struct (the C-side "params").
 *
 * Every field is meaningful to the embed except engine_path / nwchem_root which
 * are consumed by the C++ frontend for dlopen + NWCHEM_TOP before calling in.
 * Still included here so one params object carries the full option set.
 */
typedef struct RgpotNWChemParams {
  char basis[RGPOT_NWCHEM_STR];       /**< e.g. "sto-3g", "6-31g*" */
  char theory[RGPOT_NWCHEM_STR];      /**< "scf", "dft", "blyp", ... */
  char scf_type[RGPOT_NWCHEM_STR];    /**< "rhf"/"uhf" or DFT xc name */
  int charge;                         /**< molecular charge */
  int multiplicity;                   /**< 2S+1 */
  char engine_path[RGPOT_NWCHEM_PATH];/**< optional libnwchem_engine.so path */
  char nwchem_root[RGPOT_NWCHEM_PATH];/**< optional NWCHEM_TOP */
} RgpotNWChemParams;

/** Result of energy+gradient (atomic units). */
typedef struct RgpotNWChemResult {
  int ok;            /**< 1 success, 0 failure */
  double energy_h;   /**< total energy Hartree */
  char message[512]; /**< status / error, NUL-terminated */
} RgpotNWChemResult;

/** Fill params with defaults (sto-3g / scf / rhf / charge 0 / mult 1 / empty paths). */
void rgpot_nwchem_params_default(RgpotNWChemParams *p);

/**
 * Apply full params to embed (basis, theory, scf_type, charge, mult).
 * engine_path / nwchem_root are ignored inside the engine .so (frontend only).
 * @return 0 on success, non-zero if embed unavailable or rtdb update failed
 */
int rgpot_nwchem_set_params(const RgpotNWChemParams *params);

/**
 * Energy + nuclear gradient. If params is non-NULL, applies it first (same as
 * set_params then compute). If NULL, uses last successful set_params / defaults.
 *
 * @param n_atoms        atom count
 * @param positions_ang  n_atoms*3 Angstrom, row-major xyz
 * @param atomic_numbers n_atoms Z values
 * @param params         full option set, or NULL for sticky engine state
 * @param grad_h_bohr    out n_atoms*3 Hartree/Bohr, row-major
 */
RgpotNWChemResult rgpot_nwchem_energy_grad(int n_atoms,
                                           const double *positions_ang,
                                           const int *atomic_numbers,
                                           const RgpotNWChemParams *params,
                                           double *grad_h_bohr);

/** Compatibility: set only the five embed fields (empty paths). */
int rgpot_nwchem_set_config(const char *basis, const char *theory,
                            const char *scf_type, int charge, int mult);

/** Engine version string (static buffer). */
const char *rgpot_nwchem_engine_version(void);

/** 1 if real embed (RGPOT_HAS_NWCHEM), 0 if stub. */
int rgpot_nwchem_abi_available(void);

#ifdef __cplusplus
}
#endif
