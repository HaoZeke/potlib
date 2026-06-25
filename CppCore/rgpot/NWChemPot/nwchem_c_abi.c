/**
 * @file nwchem_c_abi.c
 * @brief Real NWChem C ABI (embed); built only with RGPOT_HAS_NWCHEM.
 */

#include "nwchem_c_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RGPOT_HAS_NWCHEM

/* Fortran bind(C, name=...) — stable symbols, no compiler underscore mangling. */
extern void rgpot_nwchem_embed_init(void);
extern int rgpot_nwchem_embed_available(void);
extern int rgpot_nwchem_embed_set_config(const char *basis, int basis_len,
                                         const char *theory, int theory_len,
                                         const char *scf_type, int scf_len,
                                         const int *charge, const int *mult);
extern int rgpot_nwchem_embed_energy_grad(const int *n_atoms,
                                          const double *positions_ang,
                                          const int *atomic_numbers,
                                          const int *charge,
                                          const int *multiplicity,
                                          double *energy_h,
                                          double *grad_h_bohr, char *errmsg,
                                          int errmsg_len);

static int g_initialized = 0;
static RgpotNWChemParams g_params;

/** Propagate NWCHEM_TOP / basis library from params or existing process env. */
static void apply_runtime_env(const RgpotNWChemParams *p) {
  const char *top = NULL;
  if (p && p->nwchem_root[0])
    top = p->nwchem_root;
  else
    top = getenv("NWCHEM_TOP");
  if (top && top[0]) {
#if !defined(_WIN32)
    setenv("NWCHEM_TOP", top, 1);
#endif
    if (!getenv("NWCHEM_BASIS_LIBRARY") || !getenv("NWCHEM_BASIS_LIBRARY")[0]) {
      char baslib[RGPOT_NWCHEM_PATH + 64];
      snprintf(baslib, sizeof(baslib), "%s/src/basis/libraries/", top);
#if !defined(_WIN32)
      setenv("NWCHEM_BASIS_LIBRARY", baslib, 0);
#endif
    }
  }
}

static void ensure_init(void) {
  if (!g_initialized) {
    apply_runtime_env(NULL);
    rgpot_nwchem_embed_init();
    rgpot_nwchem_params_default(&g_params);
    g_initialized = 1;
  }
}

static int cstr_len(const char *s) {
  if (!s)
    return 0;
  return (int)strlen(s);
}

static void copy_str(char *dst, size_t n, const char *src) {
  if (!dst || n == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, n, "%s", src);
}

void rgpot_nwchem_params_default(RgpotNWChemParams *p) {
  if (!p)
    return;
  memset(p, 0, sizeof(*p));
  snprintf(p->basis, sizeof(p->basis), "sto-3g");
  snprintf(p->theory, sizeof(p->theory), "scf");
  snprintf(p->scf_type, sizeof(p->scf_type), "rhf");
  p->charge = 0;
  p->multiplicity = 1;
}

static int apply_params_to_embed(const RgpotNWChemParams *p) {
  if (!p)
    return -1;
  apply_runtime_env(p);
  ensure_init();
  int ch = p->charge;
  int mult = p->multiplicity > 0 ? p->multiplicity : 1;
  return rgpot_nwchem_embed_set_config(
      p->basis, cstr_len(p->basis), p->theory, cstr_len(p->theory),
      p->scf_type, cstr_len(p->scf_type), &ch, &mult);
}

int rgpot_nwchem_set_params(const RgpotNWChemParams *params) {
  if (!params)
    return -1;
  ensure_init();
  g_params = *params;
  return apply_params_to_embed(&g_params);
}

RgpotNWChemResult rgpot_nwchem_energy_grad(int n_atoms,
                                           const double *positions_ang,
                                           const int *atomic_numbers,
                                           const RgpotNWChemParams *params,
                                           double *grad_h_bohr) {
  RgpotNWChemResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  r.message[0] = '\0';

  if (n_atoms <= 0 || !positions_ang || !atomic_numbers || !grad_h_bohr) {
    snprintf(r.message, sizeof(r.message), "invalid arguments");
    return r;
  }

  ensure_init();

  if (params) {
    g_params = *params;
    if (apply_params_to_embed(&g_params) != 0) {
      snprintf(r.message, sizeof(r.message), "set_params / embed config failed");
      return r;
    }
  }

  char errmsg[512];
  memset(errmsg, 0, sizeof(errmsg));
  int n = n_atoms;
  int ch = g_params.charge;
  int mult = g_params.multiplicity > 0 ? g_params.multiplicity : 1;
  double eh = 0.0;
  int rc = rgpot_nwchem_embed_energy_grad(
      &n, positions_ang, atomic_numbers, &ch, &mult, &eh, grad_h_bohr, errmsg,
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
  RgpotNWChemParams p;
  rgpot_nwchem_params_default(&p);
  if (basis && basis[0])
    copy_str(p.basis, sizeof(p.basis), basis);
  if (theory && theory[0])
    copy_str(p.theory, sizeof(p.theory), theory);
  if (scf_type && scf_type[0])
    copy_str(p.scf_type, sizeof(p.scf_type), scf_type);
  p.charge = charge;
  p.multiplicity = mult;
  return rgpot_nwchem_set_params(&p);
}

const char *rgpot_nwchem_engine_version(void) {
  return "rgpot-nwchem-engine/1.1.0";
}

int rgpot_nwchem_abi_available(void) {
  ensure_init();
  return rgpot_nwchem_embed_available() ? 1 : 0;
}

#else /* !RGPOT_HAS_NWCHEM */

void rgpot_nwchem_params_default(RgpotNWChemParams *p) {
  if (!p)
    return;
  memset(p, 0, sizeof(*p));
  snprintf(p->basis, sizeof(p->basis), "sto-3g");
  snprintf(p->theory, sizeof(p->theory), "scf");
  snprintf(p->scf_type, sizeof(p->scf_type), "rhf");
  p->charge = 0;
  p->multiplicity = 1;
}

int rgpot_nwchem_set_params(const RgpotNWChemParams *params) {
  (void)params;
  return -1;
}

RgpotNWChemResult rgpot_nwchem_energy_grad(int n_atoms,
                                           const double *positions_ang,
                                           const int *atomic_numbers,
                                           const RgpotNWChemParams *params,
                                           double *grad_h_bohr) {
  (void)n_atoms;
  (void)positions_ang;
  (void)atomic_numbers;
  (void)params;
  (void)grad_h_bohr;
  RgpotNWChemResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  snprintf(r.message, sizeof(r.message), "compiled without RGPOT_HAS_NWCHEM");
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
