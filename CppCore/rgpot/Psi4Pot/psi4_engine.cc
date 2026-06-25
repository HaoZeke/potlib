// MIT License
// Copyright 2023--present rgpot developers
//
// Pure C++ Psi4 engine implementing rgpot_psi4_abi.h.
// NO Python runtime. Uses Psi4 C++ APIs only via runtime-linked libpsi4.

#include "rgpot_psi4_abi.h"

#include <psi4/libfock/jk.h>
#include <psi4/libfock/v.h>
#include <psi4/libfunctional/superfunctional.h>
#include <psi4/libmints/basisset.h>
#include <psi4/libmints/deriv.h>
#include <psi4/libmints/gshell.h>
#include <psi4/libmints/matrix.h>
#include <psi4/libmints/molecule.h>
#include <psi4/libmints/pointgrp.h>
#include <psi4/libmints/wavefunction.h>
#include <psi4/liboptions/liboptions.h>
#include <psi4/libpsi4util/PsiOutStream.h>
#include <psi4/libpsi4util/process.h>
#include <psi4/libpsio/psio.hpp>
#include <psi4/libqt/qt.h>
#include <psi4/libscf_solver/rhf.h>
#include <psi4/psi4-dec.h>
// Compile-only stub; never initializes Python.
#include <psi4/pybind11.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace psi;

namespace {

constexpr const char *ENGINE_VERSION = "rgpot-psi4-engine/1.0.0-cpp";

void set_result_msg(RgpotPsi4Result &r, const char *msg) {
  if (!msg)
    msg = "";
  std::snprintf(r.message, sizeof(r.message), "%s", msg);
}

const char *elem_sym(int Z) {
  static const char *sym[] = {
      "X",  "H",  "He", "Li", "Be", "B",  "C",  "N",  "O",  "F",  "Ne",
      "Na", "Mg", "Al", "Si", "P",  "S",  "Cl", "Ar", "K",  "Ca", "Sc",
      "Ti", "V",  "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge",
      "As", "Se", "Br", "Kr"};
  if (Z < 0 || Z > 36)
    return "X";
  return sym[Z];
}

std::string sanitize_basis_name(const std::string &name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) || c == '-' || c == '_' || c == '*')
      out.push_back(static_cast<char>(std::tolower(uc)));
    else if (c == ' ' || c == '/')
      out.push_back('-');
  }
  // Common aliases
  if (out == "sto3g")
    out = "sto-3g";
  return out;
}

// ---- Embedded STO-3G (from sto-3g.gbs; He uses correct exponents, NOT H) ----

void push_s_shell(std::vector<ShellInfo> &shells, GaussianType gt,
                  const std::vector<double> &exp,
                  const std::vector<double> &coef) {
  shells.emplace_back(0, coef, exp, gt, Unnormalized);
}

void push_sp_shell(std::vector<ShellInfo> &shells, GaussianType gt,
                   const std::vector<double> &exp,
                   const std::vector<double> &coefs,
                   const std::vector<double> &coefp) {
  // SP: add S then P shells with shared exponents
  shells.emplace_back(0, coefs, exp, gt, Unnormalized);
  shells.emplace_back(1, coefp, exp, gt, Unnormalized);
}

std::vector<ShellInfo> add_sto3g_shells(int Z, GaussianType gt) {
  std::vector<ShellInfo> shells;
  // Coefficients shared by many elements in STO-3G
  const std::vector<double> c_s_h = {0.15432897, 0.53532814, 0.44463454};
  const std::vector<double> c_sp_s = {-0.09996723, 0.39951283, 0.70011547};
  const std::vector<double> c_sp_p = {0.15591627, 0.60768372, 0.39195739};

  auto s3 = [&](double e0, double e1, double e2) -> std::vector<double> {
    return {e0, e1, e2};
  };

  switch (Z) {
  case 1: // H — sto-3g.gbs
    push_s_shell(shells, gt, s3(3.42525091, 0.62391373, 0.16885540), c_s_h);
    break;
  case 2: // He — MUST use He exponents from sto-3g.gbs (NOT H)
    push_s_shell(shells, gt, s3(6.36242139, 1.15892300, 0.31364979), c_s_h);
    break;
  case 3: // Li
    push_s_shell(shells, gt, s3(16.1195750, 2.9362007, 0.7946505), c_s_h);
    push_sp_shell(shells, gt, s3(0.6362897, 0.1478601, 0.0480887), c_sp_s,
                  c_sp_p);
    break;
  case 4: // Be
    push_s_shell(shells, gt, s3(30.1678710, 5.4951153, 1.4871927), c_s_h);
    push_sp_shell(shells, gt, s3(1.3148331, 0.3055389, 0.0993707), c_sp_s,
                  c_sp_p);
    break;
  case 5: // B
    push_s_shell(shells, gt, s3(48.7911130, 8.8873622, 2.4052670), c_s_h);
    push_sp_shell(shells, gt, s3(2.2369561, 0.5198205, 0.1690618), c_sp_s,
                  c_sp_p);
    break;
  case 6: // C
    push_s_shell(shells, gt, s3(71.6168370, 13.0450960, 3.5305122), c_s_h);
    push_sp_shell(shells, gt, s3(2.9412494, 0.6834831, 0.2222899), c_sp_s,
                  c_sp_p);
    break;
  case 7: // N
    push_s_shell(shells, gt, s3(99.1061690, 18.0523120, 4.8856602), c_s_h);
    push_sp_shell(shells, gt, s3(3.7804559, 0.8784966, 0.2857144), c_sp_s,
                  c_sp_p);
    break;
  case 8: // O
    push_s_shell(shells, gt, s3(130.7093200, 23.8088610, 6.4436083), c_s_h);
    push_sp_shell(shells, gt, s3(5.0331513, 1.1695961, 0.3803890), c_sp_s,
                  c_sp_p);
    break;
  case 9: // F
    push_s_shell(shells, gt, s3(166.6791300, 30.3608120, 8.2168207), c_s_h);
    push_sp_shell(shells, gt, s3(6.4648032, 1.5022812, 0.4885885), c_sp_s,
                  c_sp_p);
    break;
  case 10: // Ne
    push_s_shell(shells, gt, s3(207.0156100, 37.7081510, 10.2052970), c_s_h);
    push_sp_shell(shells, gt, s3(8.2463151, 1.9162662, 0.6232293), c_sp_s,
                  c_sp_p);
    break;
  default:
    // Fallback: minimal H-like for unsupported Z in embedded path
    push_s_shell(shells, gt, s3(3.42525091, 0.62391373, 0.16885540), c_s_h);
    break;
  }
  return shells;
}

// ---- Gaussian94 .gbs loader (PSIDATADIR/basis/<name>.gbs) ----

int am_from_token(const std::string &tok) {
  if (tok == "S")
    return 0;
  if (tok == "P")
    return 1;
  if (tok == "D")
    return 2;
  if (tok == "F")
    return 3;
  if (tok == "G")
    return 4;
  if (tok == "SP" || tok == "L")
    return -1; // special
  return -99;
}

bool load_gbs_shells(const std::string &datadir, const std::string &basis_name,
                     const std::string &symbol,
                     std::vector<ShellInfo> &out_shells, GaussianType &gt_out) {
  out_shells.clear();
  if (datadir.empty())
    return false;

  std::string bkey = sanitize_basis_name(basis_name);
  std::string path = datadir + "/basis/" + bkey + ".gbs";
  std::ifstream in(path);
  if (!in)
    return false;

  gt_out = Pure; // default; header may set spherical/cartesian
  std::string line;
  std::string target = symbol;
  // Case-insensitive symbol match on first token of element block
  auto sym_eq = [](const std::string &a, const std::string &b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i])))
        return false;
    }
    return true;
  };

  bool in_block = false;
  while (std::getline(in, line)) {
    // trim leading whitespace
    size_t i0 = 0;
    while (i0 < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i0])))
      ++i0;
    if (i0 >= line.size())
      continue;
    if (line[i0] == '!' || line[i0] == '#')
      continue;

    std::string trimmed = line.substr(i0);
    if (trimmed == "spherical") {
      gt_out = Pure;
      continue;
    }
    if (trimmed == "cartesian") {
      gt_out = Cartesian;
      continue;
    }
    if (trimmed.size() >= 4 && trimmed.substr(0, 4) == "****") {
      if (in_block)
        break; // end of our element block
      continue;
    }

    std::istringstream iss(trimmed);
    std::string tok0;
    iss >> tok0;
    if (tok0.empty())
      continue;

    // Element header: "O 0" or "He 0"
    int dummy_z = -1;
    if (!in_block && sym_eq(tok0, target)) {
      // verify optional charge/index token
      std::string rest;
      if (iss >> rest) { /* ignore */
      }
      in_block = true;
      continue;
    }

    if (!in_block)
      continue;

    // Shell line: "S 3 1.00" or "SP 3 1.00"
    int am = am_from_token(tok0);
    if (am == -99)
      continue;

    int nprim = 0;
    double scale = 1.0;
    iss >> nprim >> scale;
    if (nprim <= 0)
      continue;

    std::vector<double> exps(static_cast<size_t>(nprim));
    std::vector<double> coefs(static_cast<size_t>(nprim));
    std::vector<double> coefp(static_cast<size_t>(nprim));

    for (int p = 0; p < nprim; ++p) {
      if (!std::getline(in, line))
        return false;
      std::istringstream ps(line);
      double e = 0, c0 = 0, c1 = 0;
      ps >> e >> c0;
      if (am == -1)
        ps >> c1; // SP has two coefficients
      exps[static_cast<size_t>(p)] = e;
      coefs[static_cast<size_t>(p)] = c0;
      coefp[static_cast<size_t>(p)] = (am == -1) ? c1 : c0;
    }

    // G94 file shells: Unnormalized (matches construct_basisset_from_pydict)
    if (am == -1) {
      out_shells.emplace_back(0, coefs, exps, gt_out, Unnormalized);
      out_shells.emplace_back(1, coefp, exps, gt_out, Unnormalized);
    } else {
      out_shells.emplace_back(am, coefs, exps, gt_out, Unnormalized);
    }
  }
  return !out_shells.empty();
}

// ---- Molecule + basis construction ----

std::shared_ptr<Molecule> make_molecule(int n_atoms, const double *pos_ang,
                                        const int *atmnrs, int charge,
                                        int multiplicity) {
  auto mol = std::make_shared<Molecule>();
  for (int i = 0; i < n_atoms; ++i) {
    int Z = atmnrs[i];
    double x = pos_ang[3 * i + 0];
    double y = pos_ang[3 * i + 1];
    double z = pos_ang[3 * i + 2];
    // add_atom expects Bohr; convert Angstrom -> Bohr
    constexpr double A2B = 1.0 / 0.529177210903;
    mol->add_atom(static_cast<double>(Z), x * A2B, y * A2B, z * A2B,
                  elem_sym(Z));
  }
  mol->set_molecular_charge(charge);
  mol->set_multiplicity(multiplicity);
  mol->reset_point_group("c1");
  mol->update_geometry();
  return mol;
}

std::shared_ptr<BasisSet>
make_basis(const std::shared_ptr<Molecule> &mol, const std::string &basis_key,
           const int *atmnrs, int n_atoms, const std::string &datadir) {
  typedef std::map<std::string, std::map<std::string, std::vector<ShellInfo>>>
      map_ssv;
  map_ssv smap;
  map_ssv emap; // empty ECP map

  const std::string name = sanitize_basis_name(basis_key);
  mol->set_basis_all_atoms(name, "BASIS");

  // Unique atom labels (element symbols as used in add_atom)
  std::set<std::string> labels;
  for (int i = 0; i < n_atoms; ++i)
    labels.insert(elem_sym(atmnrs[i]));

  GaussianType gt_file = Pure;
  for (const auto &label : labels) {
    std::vector<ShellInfo> shells;
    bool from_gbs = load_gbs_shells(datadir, name, label, shells, gt_file);
    if (!from_gbs) {
      // Embedded fallback — Pure/Unnormalized matching export_mints style
      int Z = 0;
      for (int z = 1; z <= 36; ++z) {
        if (label == elem_sym(z)) {
          Z = z;
          break;
        }
      }
      shells = add_sto3g_shells(Z, Pure);
    }
    // Hash placeholder (psi4 uses basis hash; empty ok for single basis)
    std::string hash = name + ":" + label;
    mol->set_shell_by_label(label, hash, "BASIS");
    // shell_map keys: [basis_name][atom_label] — matches construct_basisset_from_pydict
    smap[name][label] = shells;
  }

  mol->update_geometry();
  // BasisSet ctor takes non-const maps by reference (mutates internally).
  return std::make_shared<BasisSet>("BASIS", mol, smap, emap);
}

// ---- Options registration (minimal SCF/DFT set) ----

void reg_opts(Options &opt, const std::string &basis_upper) {
  auto add = [&]() {
    opt.add_str("BASIS", basis_upper);
    opt.add_str("SCF_TYPE", "PK");
    opt.add_str("REFERENCE", "RHF");
    opt.add_str("GUESS", "CORE");
    opt.add_str("SCF_SUBTYPE", "AUTO");
    opt.add_str("ORBITAL_OPTIMIZER_PACKAGE", "INTERNAL");
    opt.add_str("STABILITY_ANALYSIS", "NONE");
    opt.add_str("S_ORTHOGONALIZATION", "SYMMETRIC");
    opt.add_str("PUREAM", "TRUE");
    opt.add_bool("SOSCF", false);
    opt.add_bool("DIIS", false);
    opt.add_bool("DF_SCF_GUESS", false);
    opt.add_bool("PCM", false);
    opt.add_bool("DDX", false);
    opt.add_bool("PE", false);
    opt.add_bool("DFT_VV10_POSTSCF", false);
    opt.add_int("MAXITER", 100);
    opt.add_double("E_CONVERGENCE", 1.0e-8);
    opt.add_double("D_CONVERGENCE", 1.0e-8);
    opt.add_double("LEVEL_SHIFT", 0.0);
    opt.add_int("PRINT", 0);
    opt.add_int("DFT_SPHERICAL_POINTS", 74);
    opt.add_int("DFT_RADIAL_POINTS", 50);
  };
  opt.set_current_module("");
  add();
  opt.set_current_module("SCF");
  add();
  opt.set_global_str("BASIS", basis_upper);
  opt.set_global_str("SCF_TYPE", "PK");
  opt.set_global_str("REFERENCE", "RHF");
  opt.set_global_str("GUESS", "CORE");
  opt.set_str("SCF", "REFERENCE", "RHF");
  opt.set_str("SCF", "SCF_TYPE", "PK");
  opt.set_str("SCF", "BASIS", basis_upper);
  opt.set_int("SCF", "PRINT", 0);
  opt.set_int("SCF", "MAXITER", 100);
  opt.set_double("SCF", "E_CONVERGENCE", 1.0e-8);
  opt.set_bool("SCF", "DIIS", false);
}

// ---- SCF loop ----

// Stock Psi4 runs this loop from the Python driver; we own it in C++ only.
double scf_loop(const std::shared_ptr<scf::RHF> &scf, int maxiter,
                double e_conv) {
  scf->set_iteration(0);
  scf->form_H();
  scf->form_Shalf();
  scf->guess();
  double e_old = 0.0;
  for (int it = 1; it <= maxiter; ++it) {
    scf->set_iteration(it);
    scf->save_density_and_energy();
    scf->form_G();
    scf->form_F();
    const double e = scf->compute_E();
    scf->set_energies("Total Energy", e);
    if (it > 1 && std::fabs(e - e_old) < e_conv) {
      scf->form_C();
      scf->form_D();
      return e;
    }
    e_old = e;
    scf->form_C();
    scf->form_D();
  }
  throw std::runtime_error("SCF did not converge");
}

void set_datadir(const char *data_dir) {
  std::string dd;
  if (data_dir && data_dir[0] != '\0')
    dd = data_dir;
  else if (const char *e = std::getenv("RGPOT_PSI4_DATADIR"))
    dd = e;
  else if (const char *e = std::getenv("PSIDATADIR"))
    dd = e;
  if (!dd.empty())
    Process::environment.set_datadir(dd);
}

std::string current_datadir() {
  try {
    return Process::environment.get_datadir();
  } catch (...) {
    return {};
  }
}

RgpotPsi4Result run_blyp(int n_atoms, const double *positions_ang,
                         const int *atomic_numbers, int charge, int multiplicity,
                         double *grad_h_bohr, const char *data_dir,
                         const char *basis_name) {
  RgpotPsi4Result res{};
  res.ok = 0;
  res.energy_h = 0.0;
  res.message[0] = '\0';

  if (n_atoms <= 0 || !positions_ang || !atomic_numbers || !grad_h_bohr) {
    set_result_msg(res, "invalid arguments");
    return res;
  }

  try {
    set_datadir(data_dir);
    const std::string datadir = current_datadir();
    const std::string bname =
        (basis_name && basis_name[0]) ? basis_name : "sto-3g";

    // Quiet output
    if (!outfile)
      outfile = std::make_shared<PsiOutStream>();

    Process::environment.initialize();
    Options &options = Process::environment.options;
    const std::string basis_key = sanitize_basis_name(bname);
    std::string basis_upper = basis_key;
    for (auto &c : basis_upper)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    reg_opts(options, basis_upper);
    Process::environment.set_memory(static_cast<size_t>(2) << 30);

    auto mol = make_molecule(n_atoms, positions_ang, atomic_numbers, charge,
                             multiplicity);
    Process::environment.set_molecule(mol);

    auto basis = make_basis(mol, bname, atomic_numbers, n_atoms, datadir);

    // Reference wavefunction shell (molecule + basis + options)
    auto ref_wfn = std::make_shared<Wavefunction>(mol, basis, options);

    // BLYP functional (unpolarized RKS path via RHF+V)
    auto functional = SuperFunctional::XC_build("BLYP", true, std::nullopt);
    functional->set_max_points(5000);
    functional->set_deriv(1);

    auto psio = PSIO::shared_object();
    auto scf = std::make_shared<scf::RHF>(ref_wfn, functional, options, psio);

    // JK + XC potential (Python driver normally wires these before iterations)
    auto zero = BasisSet::zero_ao_basis_set();
    auto jk = JK::build_JK(basis, zero, options);
    jk->set_memory((static_cast<size_t>(1) << 29) / sizeof(double));
    jk->initialize();
    scf->set_jk(jk);
    if (auto v = scf->V_potential())
      v->initialize();

    const double E = scf_loop(scf, 100, 1.0e-8);
    res.energy_h = E;

    // Analytic gradient for C1 (no translation/rotation projection)
    Deriv deriv(scf, 'c', false, false);
    SharedMatrix gmat = deriv.compute();
    // gmat is natom x 3 in Hartree/Bohr
    for (int i = 0; i < n_atoms; ++i) {
      for (int j = 0; j < 3; ++j) {
        grad_h_bohr[3 * i + j] = gmat->get(i, j);
      }
    }

    res.ok = 1;
    set_result_msg(res, "ok");
  } catch (const PsiException &e) {
    set_result_msg(res, e.what());
  } catch (const std::exception &e) {
    set_result_msg(res, e.what());
  } catch (...) {
    set_result_msg(res, "unknown exception in psi4 engine");
  }
  return res;
}

} // namespace

// ---- C ABI ----

extern "C" RgpotPsi4Result rgpot_psi4_blyp_energy_grad_basis(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    int charge, int multiplicity, double *grad_h_bohr, const char *data_dir,
    const char *basis_name) {
  return run_blyp(n_atoms, positions_ang, atomic_numbers, charge, multiplicity,
                  grad_h_bohr, data_dir, basis_name);
}

extern "C" RgpotPsi4Result rgpot_psi4_blyp_energy_grad(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    int charge, int multiplicity, double *grad_h_bohr, const char *data_dir) {
  return run_blyp(n_atoms, positions_ang, atomic_numbers, charge, multiplicity,
                  grad_h_bohr, data_dir, "sto-3g");
}

extern "C" const char *rgpot_psi4_engine_version(void) { return ENGINE_VERSION; }
