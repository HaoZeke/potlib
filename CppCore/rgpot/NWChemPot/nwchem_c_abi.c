/**
 * @file nwchem_c_abi.c
 * @brief Real NWChem C ABI implementation (built only when RGPOT_HAS_NWCHEM).
 *
 * Wraps Fortran embed symbols from nwchem_embed.F. Without RGPOT_HAS_NWCHEM
 * this file is not compiled; the stub (nwchem_c_abi_stub.c) is used instead.
 */

#include "nwchem_c_abi.h"

#include <stdio.h>
#include <string.h>

#ifdef RGPOT_HAS_NWCHEM

/* Fortran embed symbols (nwchem_embed.F, name-mangled as lowercase + underscore). */
extern void rgpot_nwchem_embed_init_(void);
extern int rgpot_nwchem_embed_set_config_(const char *basis, int basis_len,
                                          const char *theory, int theory_len,
                                          const char *scf_type, int scf_len,
                                          int *charge, int *mult);
extern int rgpot_nwchem_embed_energy_grad_(int *n_atoms,
                                           const double *positions_ang,
                                           const int *atomic_numbers,
                                           int *charge, int *multiplicity,
                                           double *energy_h,
                                           double *grad_h_bohr,
                                           char *errmsg, int errmsg_len);
extern int rgpot_nwchem_embed_available_(void);

static int g_initialized = 0;

static void ensure_init(void) {
  if (!g_initialized) {
    rgpot_nwchem_embed_init_();
    g_initialized = 1;
  }
}

static int cstr_len(const char *s) {
  if (!s)
    return 0;
  return (int)strlen(s);
}

RgpotNWChemResult rgpot_nwchem_energy_grad(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    int charge, int multiplicity, const char *basis, const char *theory,
    const char *scf_type, double *grad_h_bohr) {
  RgpotNWChemResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  r.message[0] = '\0';

  if (n_atoms <= 0 || !positions_ang || !atomic_numbers || !grad_h_bohr) {
    snprintf(r.message, sizeof(r.message), "invalid arguments");
    return r;
  }

  ensure_init();

  if (basis || theory || scf_type) {
    int ch = charge;
    int mult = multiplicity;
    (void)rgpot_nwchem_embed_set_config_(
        basis ? basis : "", cstr_len(basis), theory ? theory : "",
        cstr_len(theory), scf_type ? scf_type : "", cstr_len(scf_type), &ch,
        &mult);
  }

  char errmsg[512];
  memset(errmsg, 0, sizeof(errmsg));
  int n = n_atoms;
  int ch = charge;
  int mult = multiplicity;
  double eh = 0.0;
  int rc = rgpot_nwchem_embed_energy_grad_(&n, positions_ang, atomic_numbers,
                                           &ch, &mult, &eh, grad_h_bohr, errmsg,
                                           (int)sizeof(errmsg) - 1);
  if (rc != 0) {
    snprintf(r.message, sizeof(r.message), "%s",
             errmsg[0] ? errmsg : "nwchem embed energy/grad failed");
    return r;
  }
  r.ok = 1;
  r.energy_h = eh;
  snprintf(r.message, sizeof(r.message), "ok");
  return r;
}

int rgpot_nwchem_set_config(const char *basis, const char *theory,
                            const char *scf_type, int charge, int mult) {
  ensure_init();
  int ch = charge;
  int m = mult;
  return rgpot_nwchem_embed_set_config_(
      basis ? basis : "", cstr_len(basis), theory ? theory : "",
      cstr_len(theory), scf_type ? scf_type : "", cstr_len(scf_type), &ch, &m);
}

const char *rgpot_nwchem_engine_version(void) {
  return "rgpot-nwchem-engine/1.0.0";
}

int rgpot_nwchem_abi_available(void) {
  ensure_init();
  return rgpot_nwchem_embed_available_() ? 1 : 0;
}

#else /* !RGPOT_HAS_NWCHEM -- should not be compiled, but guard anyway */

RgpotNWChemResult rgpot_nwchem_energy_grad(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    int charge, int multiplicity, const char *basis, const char *theory,
    const char *scf_type, double *grad_h_bohr) {
  (void)n_atoms;
  (void)positions_ang;
  (void)atomic_numbers;
  (void)charge;
  (void)multiplicity;
  (void)basis;
  (void)theory;
  (void)scf_type;
  (void)grad_h_bohr;
  RgpotNWChemResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  snprintf(r.message, sizeof(r.message),
           "compiled without RGPOT_HAS_NWCHEM");
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

const char *rgpot_nwchem_engine_version(void) {
  return "rgpot-nwchem-engine/unavailable";
}

int rgpot_nwchem_abi_available(void) { return 0; }

#endif /* RGPOT_HAS_NWCHEM */
