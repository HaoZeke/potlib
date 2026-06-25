/**
 * @file nwchem_c_abi.h
 * @brief Stable C ABI: rgpot NWChemPot frontend <-> libnwchem_engine (dlopen).
 *
 * Pattern mirrors runtime-loaded backends (e.g. XTBPot links xtb at build time;
 * here the *engine* is optional and resolved at runtime via RTLD_GLOBAL).
 *
 * Engine implementation: Fortran embed (nwchem_embed.F) + nwchem_c_abi.c, built
 * only when -Dwith_nwchem=true -Dnwchem_root=<NWCHEM_TOP> against a full NWChem
 * source/install tree. Without that build, only the stub (nwchem_c_abi_stub.c)
 * exists and rgpot_nwchem_abi_available() returns 0.
 *
 * Theory strings (examples):
 *   "scf" / "rhf" / "uhf"  — Hartree–Fock SCF
 *   "dft"                  — DFT (use scf_type / extra config for functional)
 *   "blyp" / "b3lyp"       — DFT with named XC (engine maps to NWChem dft block)
 *
 * Units at the ABI boundary: energy Hartree, gradient Hartree/Bohr.
 * Frontend converts to eV / eV/Å.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Result of an energy+gradient evaluation (atomic units). */
typedef struct RgpotNWChemResult {
  int ok;            /**< 1 on success, 0 on failure */
  double energy_h;   /**< Total energy in Hartree */
  char message[512]; /**< Error / status (NUL-terminated) */
} RgpotNWChemResult;

/**
 * Energy + nuclear gradient via embedded NWChem (task_energy / task_gradient).
 *
 * @param n_atoms          Atom count (>0)
 * @param positions_ang    n_atoms*3 positions in Angstrom (row-major xyz)
 * @param atomic_numbers   n_atoms atomic numbers (Z)
 * @param charge           Molecular charge
 * @param multiplicity     Spin multiplicity (2S+1)
 * @param basis            Basis set (e.g. "sto-3g", "6-31g*"); NULL => engine default
 * @param theory           Theory / method key (see file header); NULL => default
 * @param scf_type         SCF/DFT detail (e.g. "rhf", "uhf", "blyp"); NULL => default
 * @param grad_h_bohr      Out: n_atoms*3 gradient Hartree/Bohr (row-major)
 */
RgpotNWChemResult rgpot_nwchem_energy_grad(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    int charge, int multiplicity, const char *basis, const char *theory,
    const char *scf_type, double *grad_h_bohr);

/**
 * Apply configuration before energy/grad (basis, theory, scf/xc, charge, mult).
 * @return 0 on success, non-zero if embed not live or rtdb update failed
 */
int rgpot_nwchem_set_config(const char *basis, const char *theory,
                            const char *scf_type, int charge, int mult);

/** Engine build / version string (static buffer). */
const char *rgpot_nwchem_engine_version(void);

/**
 * 1 if this .so was built with a real NWChem embed (RGPOT_HAS_NWCHEM), else 0.
 * Stub always returns 0; frontend uses this to distinguish "engine loaded but
 * stub" vs "engine can compute".
 */
int rgpot_nwchem_abi_available(void);

#ifdef __cplusplus
}
#endif
