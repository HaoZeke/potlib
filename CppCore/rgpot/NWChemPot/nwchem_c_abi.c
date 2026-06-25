/**
 * @file nwchem_c_abi.c
 * @brief Real NWChem C ABI (embed); built only with RGPOT_HAS_NWCHEM.
 */

#include "nwchem_c_abi.h"

#include <stdio.h>
#include <string.h>

#ifdef RGPOT_HAS_NWCHEM

/* gfortran adds an extra trailing underscore on symbols that already end in _. */
#if defined(__GFORTRAN__) || defined(GFORTRAN)
#define RGPOT_F(name) name##_
#else
#define RGPOT_F(name) name
#endif

extern void RGPOT_F(rgpot_nwchem_embed_init_)(void);
extern int RGPOT_F(rgpot_nwchem_embed_set_config_)(const char *basis,
                                                   int basis_len,
                                                   const char *theory,
                                                   int theory_len,
                                                   const char *scf_type,
                                                   int scf_len, int *charge,
                                                   int *mult);
extern int RGPOT_F(rgpot_nwchem_embed_energy_grad_)(
    int *n_atoms, const double *positions_ang, const int *atomic_numbers,
    int *charge, int *multiplicity, double *energy_h, double *grad_h_bohr,
    char *errmsg, int errmsg_len);
extern int RGPOT_F(rgpot_nwchem_embed_available_)(void);

static int g_initialized = 0;
static RgpotNWChemParams g_params;

static void ensure_init(void) {
  if (!g_initialized) {
    RGPOT_F(rgpot_nwchem_embed_init_)();
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
  ensure_init();
  int ch = p->charge;
  int mult = p->multiplicity > 0 ? p->multiplicity : 1;
  return RGPOT_F(rgpot_nwchem_embed_set_config_)(
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
  int rc = RGPOT_F(rgpot_nwchem_embed_energy_grad_)(
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
  return RGPOT_F(rgpot_nwchem_embed_available_)() ? 1 : 0;
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
