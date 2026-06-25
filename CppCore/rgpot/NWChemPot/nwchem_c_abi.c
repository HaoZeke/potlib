/**
 * @file nwchem_c_abi.c
 * @brief Real NWChem C ABI (embed); built only with RGPOT_HAS_NWCHEM.
 */

#include "nwchem_c_abi.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RGPOT_HAS_NWCHEM

#define NWCHEMC_STR 64
#define NWCHEMC_PATH 512

/* Fortran bind(C, name=...) symbols; no compiler underscore mangling. */
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

typedef struct NWChemCParams {
  char basis[NWCHEMC_STR];
  char theory[NWCHEMC_STR];
  char scf_type[NWCHEMC_STR];
  int charge;
  int multiplicity;
} NWChemCParams;

static int g_initialized = 0;
static NWChemCParams g_params = {"sto-3g", "scf", "rhf", 0, 1};

static uint32_t load_u32_le(const unsigned char *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t load_u64_le(const unsigned char *p) {
  return ((uint64_t)load_u32_le(p)) | ((uint64_t)load_u32_le(p + 4) << 32);
}

static int32_t decode_i32(uint32_t stored, uint32_t default_value) {
  uint32_t value = stored ^ default_value;
  int32_t out;
  memcpy(&out, &value, sizeof(out));
  return out;
}

static int32_t sign_extend_30(uint32_t value) {
  if (value & 0x20000000u)
    return (int32_t)(value | 0xc0000000u);
  return (int32_t)value;
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

static void params_default(NWChemCParams *p) {
  copy_str(p->basis, sizeof(p->basis), "sto-3g");
  copy_str(p->theory, sizeof(p->theory), "scf");
  copy_str(p->scf_type, sizeof(p->scf_type), "rhf");
  p->charge = 0;
  p->multiplicity = 1;
}

static int parse_text_field(const unsigned char *segment, size_t segment_words,
                            size_t pointer_index, char *dst, size_t dst_size) {
  const uint64_t ptr = load_u64_le(segment + pointer_index * 8u);
  if (ptr == 0)
    return 0;
  if ((ptr & 3u) != 1u)
    return -1;

  const int32_t off = sign_extend_30((uint32_t)((ptr >> 2) & 0x3fffffffu));
  const uint32_t elem_size = (uint32_t)((ptr >> 32) & 7u);
  const uint64_t elem_count = ptr >> 35;
  if (elem_size != 2u || elem_count == 0)
    return -1;

  const int64_t start = (int64_t)pointer_index + 1 + (int64_t)off;
  if (start < 0)
    return -1;
  if ((uint64_t)start > (uint64_t)segment_words)
    return -1;
  if (elem_count > (uint64_t)segment_words * 8u)
    return -1;

  const uint64_t byte_start = (uint64_t)start * 8u;
  const uint64_t byte_end = byte_start + elem_count;
  if (byte_end > (uint64_t)segment_words * 8u)
    return -1;

  size_t n = (size_t)elem_count - 1u;
  if (n >= dst_size)
    n = dst_size - 1u;
  memcpy(dst, segment + byte_start, n);
  dst[n] = '\0';
  return 0;
}

static int parse_nwchem_params(const void *params_capnp,
                               size_t params_capnp_size_bytes,
                               NWChemCParams *out) {
  if (!params_capnp || params_capnp_size_bytes < 16u ||
      (params_capnp_size_bytes % 8u) != 0u || !out) {
    return -1;
  }

  const unsigned char *bytes = (const unsigned char *)params_capnp;
  const uint32_t segment_count = load_u32_le(bytes) + 1u;
  if (segment_count != 1u)
    return -1;

  const uint32_t segment_words = load_u32_le(bytes + 4u);
  const size_t header_bytes = 8u;
  if (segment_words == 0u)
    return -1;
  if (params_capnp_size_bytes < header_bytes + (size_t)segment_words * 8u)
    return -1;

  const unsigned char *segment = bytes + header_bytes;
  const uint64_t root = load_u64_le(segment);
  if ((root & 3u) != 0u)
    return -1;

  const int32_t off = sign_extend_30((uint32_t)((root >> 2) & 0x3fffffffu));
  const uint32_t data_words = (uint32_t)((root >> 32) & 0xffffu);
  const uint32_t pointer_count = (uint32_t)((root >> 48) & 0xffffu);
  const int64_t data_index_signed = 1 + (int64_t)off;
  if (data_index_signed < 0)
    return -1;
  const size_t data_index = (size_t)data_index_signed;
  if (data_index > segment_words)
    return -1;
  if ((uint64_t)data_index + data_words + pointer_count > segment_words)
    return -1;
  if (data_words < 1u || pointer_count < 5u)
    return -1;

  params_default(out);
  const uint64_t data = load_u64_le(segment + data_index * 8u);
  out->charge = (int)decode_i32((uint32_t)(data & 0xffffffffu), 0u);
  out->multiplicity = (int)decode_i32((uint32_t)(data >> 32), 1u);
  if (out->multiplicity <= 0)
    out->multiplicity = 1;

  const size_t pointer_index = data_index + data_words;
  if (parse_text_field(segment, segment_words, pointer_index + 0u, out->basis,
                       sizeof(out->basis)) != 0)
    return -1;
  if (parse_text_field(segment, segment_words, pointer_index + 1u, out->theory,
                       sizeof(out->theory)) != 0)
    return -1;
  if (parse_text_field(segment, segment_words, pointer_index + 2u,
                       out->scf_type, sizeof(out->scf_type)) != 0)
    return -1;

  if (out->basis[0] == '\0')
    copy_str(out->basis, sizeof(out->basis), "sto-3g");
  if (out->theory[0] == '\0')
    copy_str(out->theory, sizeof(out->theory), "scf");
  if (out->scf_type[0] == '\0')
    copy_str(out->scf_type, sizeof(out->scf_type), "rhf");
  return 0;
}

/** Propagate NWCHEM_TOP / basis library from existing process env. */
static void apply_runtime_env(void) {
  const char *top = getenv("NWCHEM_TOP");
  if (top && top[0]) {
#if !defined(_WIN32)
    setenv("NWCHEM_TOP", top, 1);
#endif
    if (!getenv("NWCHEM_BASIS_LIBRARY") || !getenv("NWCHEM_BASIS_LIBRARY")[0]) {
      char baslib[NWCHEMC_PATH + 64];
      snprintf(baslib, sizeof(baslib), "%s/src/basis/libraries/", top);
#if !defined(_WIN32)
      setenv("NWCHEM_BASIS_LIBRARY", baslib, 0);
#endif
    }
  }
}

static void ensure_init(void) {
  if (!g_initialized) {
    apply_runtime_env();
    rgpot_nwchem_embed_init();
    g_initialized = 1;
  }
}

static void normalize_params(NWChemCParams *p) {
  if (strncmp(p->theory, "blyp", 4) == 0 ||
      strncmp(p->theory, "b3lyp", 5) == 0 ||
      strncmp(p->theory, "pbe", 3) == 0) {
    copy_str(p->scf_type, sizeof(p->scf_type), p->theory);
    copy_str(p->theory, sizeof(p->theory), "dft");
  }
  if (strncmp(p->theory, "dft", 3) == 0 && p->scf_type[0] == '\0')
    copy_str(p->scf_type, sizeof(p->scf_type), "blyp");
}

static int apply_config_to_embed(const NWChemCParams *params) {
  apply_runtime_env();
  ensure_init();
  NWChemCParams p = *params;
  normalize_params(&p);
  int ch = p.charge;
  int mult = p.multiplicity > 0 ? p.multiplicity : 1;
  return rgpot_nwchem_embed_set_config(p.basis, cstr_len(p.basis), p.theory,
                                       cstr_len(p.theory), p.scf_type,
                                       cstr_len(p.scf_type), &ch, &mult);
}

int nwchemc_set_params(const void *params_capnp,
                       size_t params_capnp_size_bytes) {
  NWChemCParams parsed;
  if (parse_nwchem_params(params_capnp, params_capnp_size_bytes, &parsed) != 0)
    return -1;
  g_params = parsed;
  return apply_config_to_embed(&g_params);
}

NWChemCResult nwchemc_energy_gradient(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    const void *params_capnp, size_t params_capnp_size_bytes,
    double *grad_h_bohr) {
  NWChemCResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  r.message[0] = '\0';

  if (n_atoms <= 0 || !positions_ang || !atomic_numbers || !grad_h_bohr) {
    snprintf(r.message, sizeof(r.message), "invalid arguments");
    return r;
  }

  NWChemCParams parsed;
  if (parse_nwchem_params(params_capnp, params_capnp_size_bytes, &parsed) != 0) {
    snprintf(r.message, sizeof(r.message), "invalid NWChemParams message");
    return r;
  }
  g_params = parsed;

  ensure_init();
  if (apply_config_to_embed(&g_params) != 0) {
    snprintf(r.message, sizeof(r.message), "embed config failed");
    return r;
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

const char *nwchemc_version(void) { return "nwchemc/1.2.0"; }

int nwchemc_available(void) {
  ensure_init();
  return rgpot_nwchem_embed_available() ? 1 : 0;
}

#else /* !RGPOT_HAS_NWCHEM */

int nwchemc_set_params(const void *params_capnp,
                       size_t params_capnp_size_bytes) {
  (void)params_capnp;
  (void)params_capnp_size_bytes;
  return -1;
}

NWChemCResult nwchemc_energy_gradient(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    const void *params_capnp, size_t params_capnp_size_bytes,
    double *grad_h_bohr) {
  (void)n_atoms;
  (void)positions_ang;
  (void)atomic_numbers;
  (void)params_capnp;
  (void)params_capnp_size_bytes;
  (void)grad_h_bohr;
  NWChemCResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  snprintf(r.message, sizeof(r.message), "compiled without RGPOT_HAS_NWCHEM");
  return r;
}

const char *nwchemc_version(void) { return "nwchemc/unavailable"; }

int nwchemc_available(void) { return 0; }

#endif /* RGPOT_HAS_NWCHEM */
