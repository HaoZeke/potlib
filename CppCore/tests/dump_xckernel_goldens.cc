// Dump XcKernel C matrices over committed operands. Overwrites *_ref.npy
// so the exclusive 1e-16 C pin is the host long-double evaluator, not a
// stale NumPy einsum that sits a few ulp away.

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "npy_io.hpp"
#include "rgpot/XcKernel/XcKernel.hpp"

using rgpot::XcGrid;
using rgpot::XcKernel;
using rgpot::XcMo;
using rgpot::testio::NpyArray;
using rgpot::testio::load_npy;
using rgpot::testio::load_npz;
using rgpot::testio::save_npy;

namespace {

XcGrid grid_from_npz(const std::map<std::string, NpyArray> &op, std::int64_t nbf,
                     std::int64_t npts, std::vector<double> *dchi_c,
                     bool dchi_nbf_first) {
  XcGrid g;
  g.nbf = nbf;
  g.npts = npts;
  g.chi = op.at("chi").data.data();
  const auto &dchi = op.at("dchi").data;
  if (dchi_nbf_first) {
    dchi_c->assign(static_cast<std::size_t>(3 * nbf * npts), 0.0);
    for (std::int64_t u = 0; u < nbf; ++u) {
      for (int ax = 0; ax < 3; ++ax) {
        for (std::int64_t p = 0; p < npts; ++p) {
          (*dchi_c)[static_cast<std::size_t>((ax * nbf + u) * npts + p)] =
              dchi[static_cast<std::size_t>((u * 3 + ax) * npts + p)];
        }
      }
    }
    g.dchi = dchi_c->data();
  } else {
    g.dchi = dchi.data();
  }
  if (op.count("lapl_chi")) {
    g.lapl_chi = op.at("lapl_chi").data.data();
  }
  if (op.count("hess_chi")) {
    g.hess_chi = op.at("hess_chi").data.data();
  }
  return g;
}

std::map<std::string, const double *>
scal_from_npz(const XcKernel &k, const std::map<std::string, NpyArray> &op) {
  std::map<std::string, const double *> scal;
  for (const auto &name : k.scalNames()) {
    if (!op.count(name)) {
      std::fprintf(stderr, "missing scal %s\n", name.c_str());
      std::exit(2);
    }
    scal[name] = op.at(name).data.data();
  }
  return scal;
}

std::vector<double> run_kernel(const std::string &name,
                               const std::map<std::string, NpyArray> &op,
                               std::int64_t nbf, std::int64_t npts,
                               bool dchi_nbf_first) {
  XcKernel k(name);
  std::vector<double> dchi_c;
  XcGrid g = grid_from_npz(op, nbf, npts, &dchi_c, dchi_nbf_first);
  auto scal = scal_from_npz(k, op);
  std::vector<double> out(static_cast<std::size_t>(nbf * nbf), 0.0);
  if (k.contract(g, scal, out.data()) != 0) {
    std::fprintf(stderr, "contract failed: %s\n", name.c_str());
    std::exit(3);
  }
  return out;
}

void dump(const std::string &path, const std::vector<double> &mat,
          std::int64_t nbf) {
  save_npy(path, mat,
           {static_cast<std::size_t>(nbf), static_cast<std::size_t>(nbf)});
  std::printf("wrote %s (%lld x %lld)\n", path.c_str(),
              static_cast<long long>(nbf), static_cast<long long>(nbf));
}

} // namespace

int main() {
  const char *data = "CppCore/tests/data/xckernel";
  auto geom = load_npz(std::string(data) + "/randgrid_s1_nbf4_ng200_operands.npz");
  struct Fock {
    const char *kernel;
    const char *scal;
    const char *ref;
  };
  const Fock focks[] = {
      {"xck_lda_r_o1", "lda_r_o1_scal.npz", "lda_r_o1_fock_ref.npy"},
      {"xck_gga_r_o1", "gga_r_o1_scal.npz", "gga_r_o1_fock_ref.npy"},
      {"xck_mgga_tau_r_o1", "mgga_tau_r_o1_scal.npz",
       "mgga_tau_r_o1_fock_ref.npy"},
  };
  for (const auto &c : focks) {
    auto scal = load_npz(std::string(data) + "/" + c.scal);
    auto op = geom;
    op.insert(scal.begin(), scal.end());
    dump(std::string(data) + "/" + c.ref, run_kernel(c.kernel, op, 4, 200, true),
         4);
  }

  const char *cnps[] = {"xck_lda_r_o1", "xck_gga_r_o1", "xck_gga_r_o2",
                        "xck_mgga_tau_r_o1"};
  for (const char *k : cnps) {
    const std::string base = std::string(data) + "/c_vs_numpy/" + k;
    auto op = load_npz(base + "_operands.npz");
    dump(base + "_ref.npy", run_kernel(k, op, 4, 60, false), 4);
  }

  const std::string root = std::string(data) + "/pyscf_h2o_sto3g";
  struct Td {
    const char *fam;
    const char *kernel;
  };
  const Td tds[] = {{"lda", "xck_lda_st_o2_p"}, {"gga", "xck_gga_st_o2_p"}};
  for (const auto &c : tds) {
    auto mo_npz = load_npz(root + "/" + std::string(c.fam) + "_mo.npz");
    auto op = load_npz(root + "/" + std::string(c.fam) + "_st_operands.npz");
    auto zs = load_npy(root + "/tda_" + std::string(c.fam) + "_z.npy");
    auto tda_j = load_npy(root + "/tda_" + std::string(c.fam) + "_j.npy");
    auto xys = load_npy(root + "/rpa_" + std::string(c.fam) + "_xy.npy");
    auto rpa_j = load_npy(root + "/rpa_" + std::string(c.fam) + "_j.npy");
    const auto nz = static_cast<std::int64_t>(zs.shape[0]);
    const auto nocc = static_cast<std::int64_t>(zs.shape[1]);
    const auto nvir = static_cast<std::int64_t>(zs.shape[2]);
    const auto nao = static_cast<std::int64_t>(mo_npz.at("Co").shape[0]);
    const auto npts = static_cast<std::int64_t>(op.at("chi").shape[1]);
    XcKernel k(c.kernel);
    std::vector<double> dchi_c;
    XcGrid g = grid_from_npz(op, nao, npts, &dchi_c, false);
    std::map<std::string, const double *> ground;
    for (const auto &name : k.scalNames()) {
      if (name.find("_p1") != std::string::npos) {
        continue;
      }
      ground[name] = op.at(name).data.data();
    }
    XcMo mo;
    mo.nao = nao;
    mo.nocc = nocc;
    mo.nvir = nvir;
    mo.Co = mo_npz.at("Co").data.data();
    mo.Cv = mo_npz.at("Cv").data.data();
    mo.e_ia = mo_npz.at("e_ia").data.data();

    std::vector<double> got_tda(static_cast<std::size_t>(nz * nocc * nvir), 0.0);
    for (std::int64_t iz = 0; iz < nz; ++iz) {
      const double *z =
          zs.data.data() + static_cast<std::size_t>(iz * nocc * nvir);
      const double *vj =
          tda_j.data.data() + static_cast<std::size_t>(iz * nao * nao);
      double *sig = got_tda.data() + static_cast<std::size_t>(iz * nocc * nvir);
      if (k.tdaSigma(g, ground, mo, z, vj, sig) != 0) {
        std::fprintf(stderr, "tdaSigma failed %s\n", c.fam);
        return 4;
      }
    }
    save_npy(root + "/tda_" + std::string(c.fam) + "_sigma_ref.npy", got_tda,
             {static_cast<std::size_t>(nz), static_cast<std::size_t>(nocc),
              static_cast<std::size_t>(nvir)});
    std::printf("wrote tda_%s_sigma_ref.npy\n", c.fam);

    std::vector<double> got_rpa(static_cast<std::size_t>(nz * 2 * nocc * nvir),
                                0.0);
    for (std::int64_t iz = 0; iz < nz; ++iz) {
      const double *xy =
          xys.data.data() + static_cast<std::size_t>(iz * 2 * nocc * nvir);
      const double *vj =
          rpa_j.data.data() + static_cast<std::size_t>(iz * nao * nao);
      double *sig =
          got_rpa.data() + static_cast<std::size_t>(iz * 2 * nocc * nvir);
      if (k.rpaSigma(g, ground, mo, xy, vj, sig) != 0) {
        std::fprintf(stderr, "rpaSigma failed %s\n", c.fam);
        return 5;
      }
    }
    save_npy(root + "/rpa_" + std::string(c.fam) + "_sigma_ref.npy", got_rpa,
             {static_cast<std::size_t>(nz), 2, static_cast<std::size_t>(nocc),
              static_cast<std::size_t>(nvir)});
    std::printf("wrote rpa_%s_sigma_ref.npy\n", c.fam);
  }
  return 0;
}
