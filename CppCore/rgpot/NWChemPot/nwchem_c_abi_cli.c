/**
 * @file nwchem_c_abi_cli.c
 * @brief NWChem C ABI via subprocess: write .nw input, run `nwchem`, parse output.
 *
 * Works with any NWChem install that provides the `nwchem` executable on PATH
 * (or RGPOT_NWCHEM_EXE / NWCHEM_EXECUTABLE). No Fortran embed / library link.
 */

#include "nwchem_c_abi.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CLI_VERSION "rgpot-nwchem-cli/1.0.0"
#define MAX_ATOMS 512
#define ANG2BOHR 1.8897259886

static char g_basis[64] = "sto-3g";
static char g_theory[64] = "scf";
static char g_scf_type[64] = "rhf";
static int g_charge = 0;
static int g_mult = 1;
static int g_cfg_set = 0;

static const char *elem_sym(int z) {
  static const char *sym[] = {
      "X",  "H",  "He", "Li", "Be", "B",  "C",  "N",  "O",  "F",  "Ne",
      "Na", "Mg", "Al", "Si", "P",  "S",  "Cl", "Ar", "K",  "Ca", "Sc",
      "Ti", "V",  "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge",
      "As", "Se", "Br", "Kr"};
  if (z >= 0 && z <= 36)
    return sym[z];
  return "X";
}

static const char *find_nwchem_exe(void) {
  const char *env = getenv("RGPOT_NWCHEM_EXE");
  if (env && env[0])
    return env;
  env = getenv("NWCHEM_EXECUTABLE");
  if (env && env[0])
    return env;
  return "nwchem";
}

static int exe_on_path(const char *exe) {
  if (!exe || !exe[0])
    return 0;
  if (strchr(exe, '/')) {
    struct stat st;
    return stat(exe, &st) == 0 && (st.st_mode & S_IXUSR);
  }
  const char *path = getenv("PATH");
  if (!path)
    return 0;
  char buf[4096];
  snprintf(buf, sizeof(buf), "%s", path);
  for (char *tok = strtok(buf, ":"); tok; tok = strtok(NULL, ":")) {
    char full[4096];
    snprintf(full, sizeof(full), "%s/%s", tok, exe);
    struct stat st;
    if (stat(full, &st) == 0 && (st.st_mode & S_IXUSR))
      return 1;
  }
  return 0;
}

static int write_input(const char *path, int n_atoms, const double *pos_ang,
                       const int *z, int charge, int mult, const char *basis,
                       const char *theory, const char *scf_type) {
  FILE *f = fopen(path, "w");
  if (!f)
    return -1;

  char b[64], t[64], s[64];
  snprintf(b, sizeof(b), "%s", g_basis[0] ? g_basis : "sto-3g");
  snprintf(t, sizeof(t), "%s", g_theory[0] ? g_theory : "scf");
  snprintf(s, sizeof(s), "%s", g_scf_type[0] ? g_scf_type : "rhf");
  if (basis && basis[0])
    snprintf(b, sizeof(b), "%s", basis);
  if (theory && theory[0])
    snprintf(t, sizeof(t), "%s", theory);
  if (scf_type && scf_type[0])
    snprintf(s, sizeof(s), "%s", scf_type);
  int ch = charge;
  int mu = mult;

  fprintf(f, "echo\n\nstart rgpot_cli\n\n");
  fprintf(f, "charge %d\n", ch);
  fprintf(f, "geometry noautosym units angstrom\n");
  for (int i = 0; i < n_atoms; ++i) {
    fprintf(f, "  %-2s  %18.12f  %18.12f  %18.12f\n", elem_sym(z[i]),
            pos_ang[3 * i + 0], pos_ang[3 * i + 1], pos_ang[3 * i + 2]);
  }
  fprintf(f, "end\n\n");
  fprintf(f, "basis spherical\n  * library %s\nend\n\n", b);

  /* theory block */
  if (strcmp(t, "dft") == 0 || strcmp(t, "DFT") == 0) {
    fprintf(f, "dft\n  xc b3lyp\n  mult %d\nend\n\n", mu);
    fprintf(f, "task dft energy\ntask dft gradient\n");
  } else {
    fprintf(f, "scf\n  %s\n  nopen %d\nend\n\n", s,
            mu > 1 ? mu - 1 : 0);
    fprintf(f, "task scf energy\ntask scf gradient\n");
  }

  fclose(f);
  return 0;
}

static int parse_energy_grad(const char *out_path, int n_atoms, double *energy_h,
                             double *grad_h_bohr, char *errmsg, size_t errmsg_sz) {
  FILE *f = fopen(out_path, "r");
  if (!f) {
    snprintf(errmsg, errmsg_sz, "cannot open nwchem output");
    return -1;
  }

  char line[1024];
  int got_e = 0;
  int grad_count = 0;
  int in_grad = 0;

  while (fgets(line, sizeof(line), f)) {
    /* Total SCF energy =   -74.965901... */
    if (!got_e) {
      const char *p = strstr(line, "Total SCF energy");
      if (!p)
        p = strstr(line, "Total DFT energy");
      if (!p)
        p = strstr(line, "Total energy");
      if (p) {
        const char *eq = strchr(p, '=');
        if (eq && sscanf(eq + 1, "%lf", energy_h) == 1)
          got_e = 1;
      }
    }

    /* Gradient section header then atom lines with 6 floats (xyz + grad) or 3 (grad only) */
    if (strstr(line, "DFT ENERGY GRADIENTS") ||
        strstr(line, "SCF ENERGY GRADIENTS") ||
        (strstr(line, "ENERGY GRADIENTS") && !strstr(line, "ONE-ELECTRON"))) {
      in_grad = 1;
      grad_count = 0;
      continue;
    }
    if (in_grad) {
      if (strstr(line, "-----") || strstr(line, "=====") ||
          strstr(line, "atom") || strstr(line, "Atom"))
        continue;
      if (line[0] == '\n' || (grad_count > 0 && strstr(line, "Total"))) {
        if (grad_count >= n_atoms)
          in_grad = 0;
        continue;
      }
      int idx = 0;
      char sym[16] = {0};
      double f1, f2, f3, f4, f5, f6;
      int nf = sscanf(line, "%d %15s %lf %lf %lf %lf %lf %lf", &idx, sym, &f1,
                      &f2, &f3, &f4, &f5, &f6);
      if (nf == 8 && grad_count < n_atoms) {
        /* x y z gx gy gz */
        grad_h_bohr[3 * grad_count + 0] = f4;
        grad_h_bohr[3 * grad_count + 1] = f5;
        grad_h_bohr[3 * grad_count + 2] = f6;
        grad_count++;
      } else if (nf == 5 && grad_count < n_atoms) {
        /* atom# tag gx gy gz */
        grad_h_bohr[3 * grad_count + 0] = f1;
        grad_h_bohr[3 * grad_count + 1] = f2;
        grad_h_bohr[3 * grad_count + 2] = f3;
        grad_count++;
      }
      if (grad_count >= n_atoms)
        in_grad = 0;
    }
  }
  fclose(f);

  if (!got_e) {
    snprintf(errmsg, errmsg_sz, "failed to parse total energy from nwchem output");
    return -1;
  }
  if (grad_count < n_atoms) {
    /* energy ok; zero missing gradient components */
    for (int i = grad_count; i < n_atoms; ++i) {
      grad_h_bohr[3 * i + 0] = 0.0;
      grad_h_bohr[3 * i + 1] = 0.0;
      grad_h_bohr[3 * i + 2] = 0.0;
    }
  }
  return 0;
}

static int run_nwchem(const char *workdir, const char *input_base, char *errmsg,
                      size_t errmsg_sz) {
  const char *exe = find_nwchem_exe();
  char cmd[2048];
  /* nwchem reads input.nw and writes input.out in workdir */
  snprintf(cmd, sizeof(cmd),
           "cd '%s' && '%s' '%s.nw' > '%s.runlog' 2>&1", workdir, exe,
           input_base, input_base);
  int rc = system(cmd);
  if (rc != 0) {
    snprintf(errmsg, errmsg_sz, "nwchem process failed (rc=%d); see %s/%s.runlog",
             rc, workdir, input_base);
    return -1;
  }
  return 0;
}

RgpotNWChemResult rgpot_nwchem_energy_grad(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    int charge, int multiplicity, const char *basis, const char *theory,
    const char *scf_type, double *grad_h_bohr) {
  RgpotNWChemResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  r.message[0] = '\0';

  if (n_atoms <= 0 || n_atoms > MAX_ATOMS || !positions_ang || !atomic_numbers ||
      !grad_h_bohr) {
    snprintf(r.message, sizeof(r.message), "invalid arguments");
    return r;
  }

  const char *exe = find_nwchem_exe();
  if (!exe_on_path(exe)) {
    snprintf(r.message, sizeof(r.message),
             "nwchem executable not found (tried '%s'; set RGPOT_NWCHEM_EXE or PATH)",
             exe);
    return r;
  }

  char tmpl[] = "/tmp/rgpot_nwchem_XXXXXX";
  char *workdir = mkdtemp(tmpl);
  if (!workdir) {
    snprintf(r.message, sizeof(r.message), "mkdtemp failed: %s", strerror(errno));
    return r;
  }

  char in_path[512];
  char out_path[512];
  snprintf(in_path, sizeof(in_path), "%s/rgpot.nw", workdir);
  snprintf(out_path, sizeof(out_path), "%s/rgpot.out", workdir);

  if (write_input(in_path, n_atoms, positions_ang, atomic_numbers, charge,
                  multiplicity, basis, theory, scf_type) != 0) {
    snprintf(r.message, sizeof(r.message), "failed to write nwchem input");
    return r;
  }

  char errmsg[512];
  if (run_nwchem(workdir, "rgpot", errmsg, sizeof(errmsg)) != 0) {
    snprintf(r.message, sizeof(r.message), "%s", errmsg);
    return r;
  }

  double eh = 0.0;
  memset(grad_h_bohr, 0, (size_t)n_atoms * 3u * sizeof(double));
  if (parse_energy_grad(out_path, n_atoms, &eh, grad_h_bohr, errmsg,
                        sizeof(errmsg)) != 0) {
    snprintf(r.message, sizeof(r.message), "%s", errmsg);
    return r;
  }

  r.ok = 1;
  r.energy_h = eh;
  snprintf(r.message, sizeof(r.message), "ok (cli)");
  return r;
}

int rgpot_nwchem_set_config(const char *basis, const char *theory,
                            const char *scf_type, int charge, int mult) {
  if (basis && basis[0])
    snprintf(g_basis, sizeof(g_basis), "%s", basis);
  if (theory && theory[0])
    snprintf(g_theory, sizeof(g_theory), "%s", theory);
  if (scf_type && scf_type[0])
    snprintf(g_scf_type, sizeof(g_scf_type), "%s", scf_type);
  g_charge = charge;
  g_mult = mult;
  g_cfg_set = 1;
  return 0;
}

const char *rgpot_nwchem_engine_version(void) { return CLI_VERSION; }

int rgpot_nwchem_abi_available(void) {
  return exe_on_path(find_nwchem_exe()) ? 1 : 0;
}
