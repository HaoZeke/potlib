// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rgpot/XcKernel/XcKernel.hpp"
#include "xckernel_npy.hpp"

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;
using rgpot::XcFamily;
using rgpot::XcKernel;
using rgpot::XcSpin;
using rgpot::catalogName;
using rgpot::familyName;
using rgpot::familyVars;
using rgpot::firstSliceFamilies;
using rgpot::isFirstSliceFamily;
using rgpot::testio::load_npy;
using rgpot::testio::load_npz;
using rgpot::testio::NpyArray;

namespace {

const fs::path kData{"data/xckernel"};

void require_file(const fs::path &p) {
  INFO("missing golden fixture: " << p.string());
  REQUIRE(fs::exists(p));
  REQUIRE(fs::is_regular_file(p));
  REQUIRE(fs::file_size(p) > 0);
}

double max_abs_diff(const std::vector<double> &a, const std::vector<double> &b) {
  REQUIRE(a.size() == b.size());
  double m = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    m = std::max(m, std::abs(a[i] - b[i]));
  }
  return m;
}

double max_rel_diff(const std::vector<double> &got,
                    const std::vector<double> &ref) {
  REQUIRE(got.size() == ref.size());
  double num = 0.0;
  double den = 1.0;
  for (std::size_t i = 0; i < ref.size(); ++i) {
    num = std::max(num, std::abs(got[i] - ref[i]));
    den = std::max(den, std::abs(ref[i]));
  }
  if (den < 1.0) {
    den = 1.0;
  }
  return num / den;
}

std::unordered_map<std::string, const double *>
scal_from_npz(const std::unordered_map<std::string, NpyArray> &npz,
              const std::vector<std::string> &names) {
  std::unordered_map<std::string, const double *> scal;
  for (const auto &n : names) {
    auto it = npz.find(n);
    REQUIRE(it != npz.end());
    scal.emplace(n, it->second.data.data());
  }
  return scal;
}

// C ABI dchi is (3, nbf, npts). Fixtures may store (nbf, 3, npts).
std::vector<double> dchi_c_abi(const NpyArray &dchi, std::int64_t nbf,
                               std::int64_t npts) {
  if (dchi.shape.size() == 3 && dchi.shape[0] == 3 &&
      dchi.shape[1] == static_cast<std::size_t>(nbf) &&
      dchi.shape[2] == static_cast<std::size_t>(npts)) {
    return dchi.data;
  }
  REQUIRE(dchi.shape.size() == 3);
  REQUIRE(dchi.shape[0] == static_cast<std::size_t>(nbf));
  REQUIRE(dchi.shape[1] == 3);
  REQUIRE(dchi.shape[2] == static_cast<std::size_t>(npts));
  std::vector<double> out(static_cast<std::size_t>(3 * nbf * npts));
  for (std::int64_t u = 0; u < nbf; ++u) {
    for (int ax = 0; ax < 3; ++ax) {
      for (std::int64_t g = 0; g < npts; ++g) {
        const std::size_t src =
            static_cast<std::size_t>((u * 3 + ax) * npts + g);
        const std::size_t dst =
            static_cast<std::size_t>((ax * nbf + u) * npts + g);
        out[dst] = dchi.data[src];
      }
    }
  }
  return out;
}

} // namespace

TEST_CASE("xckernel golden fixtures exist (fail closed)", "[xckernel]") {
  const std::vector<fs::path> required = {
      kData / "randgrid_s1_nbf4_ng200_operands.npz",
      kData / "lda_r_o1_fock_ref.npy",
      kData / "gga_r_o1_fock_ref.npy",
      kData / "mgga_tau_r_o1_fock_ref.npy",
      kData / "mol_h2o_sto3g_lvl3_operands.npz",
      kData / "gga_r_o2_fxc_ref.npy",
      kData / "MANIFEST.json",
      kData / "c_vs_numpy" / "xck_lda_r_o1_operands.npz",
      kData / "c_vs_numpy" / "xck_lda_r_o1_ref.npy",
      kData / "c_vs_numpy" / "xck_gga_r_o1_operands.npz",
      kData / "c_vs_numpy" / "xck_gga_r_o1_ref.npy",
      kData / "c_vs_numpy" / "xck_gga_r_o2_operands.npz",
      kData / "c_vs_numpy" / "xck_gga_r_o2_ref.npy",
      kData / "c_vs_numpy" / "xck_mgga_tau_r_o1_operands.npz",
      kData / "c_vs_numpy" / "xck_mgga_tau_r_o1_ref.npy",
      kData / "pyscf_h2o_sto3g" / "meta.json",
      kData / "pyscf_h2o_sto3g" / "dm0.npy",
      kData / "pyscf_h2o_sto3g" / "dm1.npy",
      kData / "pyscf_h2o_sto3g" / "lda_fock_ref.npy",
      kData / "pyscf_h2o_sto3g" / "gga_fock_ref.npy",
      kData / "pyscf_h2o_sto3g" / "mgga_tau_fock_ref.npy",
      kData / "pyscf_h2o_sto3g" / "gga_fxc_ref.npy",
      kData / "pyscf_h2o_sto3g" / "tda_lda_sigma_ref.npy",
      kData / "pyscf_h2o_sto3g" / "tda_gga_sigma_ref.npy",
      kData / "pyscf_h2o_sto3g" / "rpa_lda_sigma_ref.npy",
      kData / "pyscf_h2o_sto3g" / "rpa_gga_sigma_ref.npy",
  };
  for (const auto &p : required) {
    require_file(p);
  }
}

TEST_CASE("family vars match catalog FAMILY_VARS", "[xckernel][family]") {
  REQUIRE(familyVars(XcFamily::Lda) ==
          std::vector<std::string_view>{"rho"});
  REQUIRE(familyVars(XcFamily::Gga) ==
          std::vector<std::string_view>{"rho", "sigma"});
  REQUIRE(familyVars(XcFamily::MggaTau) ==
          std::vector<std::string_view>{"rho", "sigma", "tau"});
  REQUIRE(familyVars(XcFamily::MggaLapl) ==
          std::vector<std::string_view>{"rho", "sigma", "lapl"});
  REQUIRE(familyName(XcFamily::Lda) == "lda");
  REQUIRE(familyName(XcFamily::Gga) == "gga");
  REQUIRE(familyName(XcFamily::MggaTau) == "mgga_tau");
  REQUIRE(familyName(XcFamily::MggaLapl) == "mgga_lapl");
  REQUIRE(isFirstSliceFamily(XcFamily::Lda));
  REQUIRE(isFirstSliceFamily(XcFamily::Gga));
  REQUIRE(isFirstSliceFamily(XcFamily::MggaTau));
  REQUIRE_FALSE(isFirstSliceFamily(XcFamily::MggaLapl));
  const auto fams = firstSliceFamilies();
  REQUIRE(fams.size() == 3);
  REQUIRE(fams[0] == XcFamily::Lda);
  REQUIRE(fams[1] == XcFamily::Gga);
  REQUIRE(fams[2] == XcFamily::MggaTau);
}

TEST_CASE("catalogName encodes family spin order", "[xckernel][family]") {
  REQUIRE(catalogName(XcFamily::Lda, XcSpin::Restricted, 1) == "xck_lda_r_o1");
  REQUIRE(catalogName(XcFamily::Gga, XcSpin::Restricted, 2) == "xck_gga_r_o2");
  REQUIRE(catalogName(XcFamily::MggaTau, XcSpin::UnrestrictedA, 1) ==
          "xck_mgga_tau_ua_o1");
  REQUIRE(catalogName(XcFamily::Gga, XcSpin::SpinAdapted, 2, 1) ==
          "xck_gga_st_o2_p");
  REQUIRE(catalogName(XcFamily::Lda, XcSpin::SpinAdapted, 2, -1) ==
          "xck_lda_st_o2_m");
}

TEST_CASE("XcKernel is not a Potential and first-slice names are o1/o2",
          "[xckernel][family]") {
  REQUIRE(XcKernel::available());
  const auto names = XcKernel::first_slice_names();
  REQUIRE(names.size() == 24);
  for (const auto &n : names) {
    REQUIRE(n.find("_o3") == std::string::npos);
    REQUIRE(n.find("_o4") == std::string::npos);
    REQUIRE(n.find("giao") == std::string::npos);
    REQUIRE(n.find("cmgga") == std::string::npos);
    REQUIRE(n.find("hmgga") == std::string::npos);
    REQUIRE(n.find("mgga_lapl") == std::string::npos);
  }
  REQUIRE_THROWS(XcKernel("xck_gga_r_o3"));
  REQUIRE_THROWS(XcKernel("xck_lda_r_giao"));
  REQUIRE_THROWS_WITH(XcKernel(XcFamily::MggaLapl, XcSpin::Restricted, 1),
                      ContainsSubstring("mgga_lapl"));
  REQUIRE_THROWS_WITH(XcKernel("xck_mgga_lapl_r_o1"),
                      ContainsSubstring("mgga_lapl"));
}

TEST_CASE("family constructor dispatches lda gga mgga_tau",
          "[xckernel][family]") {
  XcKernel lda(XcFamily::Lda, XcSpin::Restricted, 1);
  REQUIRE(lda.family() == XcFamily::Lda);
  REQUIRE(lda.order() == 1);
  REQUIRE(lda.name() == "xck_lda_r_o1");
  XcKernel gga(XcFamily::Gga, XcSpin::Restricted, 2);
  REQUIRE(gga.family() == XcFamily::Gga);
  REQUIRE(gga.name() == "xck_gga_r_o2");
  XcKernel tau(XcFamily::MggaTau, XcSpin::Restricted, 1);
  REQUIRE(tau.family() == XcFamily::MggaTau);
  REQUIRE(tau.name() == "xck_mgga_tau_r_o1");
}

TEST_CASE("missing scal operands fail before the C call", "[xckernel]") {
  XcKernel k(XcFamily::Lda, XcSpin::Restricted, 1);
  std::vector<double> chi(4 * 2, 1.0);
  std::vector<double> dchi(3 * 4 * 2, 0.0);
  std::vector<double> out(16, 0.0);
  std::unordered_map<std::string, const double *> empty;
  REQUIRE_THROWS_WITH(k.accumulate(2, 4, chi.data(), dchi.data(), nullptr,
                                   nullptr, empty, out.data()),
                      ContainsSubstring("missing operands"));
}

TEST_CASE("randgrid Fock vs pinned refs", "[xckernel]") {
  require_file(kData / "randgrid_s1_nbf4_ng200_operands.npz");
  auto ops = load_npz((kData / "randgrid_s1_nbf4_ng200_operands.npz").string());
  REQUIRE(ops.count("w"));
  REQUIRE(ops.count("chi"));
  REQUIRE(ops.count("dchi"));
  REQUIRE(ops.count("lapl_chi"));
  REQUIRE(ops.count("P"));
  const auto nbf = static_cast<std::int64_t>(ops["chi"].shape[0]);
  const auto npts = static_cast<std::int64_t>(ops["chi"].shape[1]);
  REQUIRE(nbf == 4);
  REQUIRE(npts == 200);
  auto dchi = dchi_c_abi(ops["dchi"], nbf, npts);
  const double *lapl =
      ops.count("lapl_chi") ? ops["lapl_chi"].data.data() : nullptr;

  struct Case {
    XcFamily family;
    const char *ref;
    const char *scal;
  };
  const Case cases[] = {
      {XcFamily::Lda, "lda_r_o1_fock_ref.npy", "lda_r_o1_operands.npz"},
      {XcFamily::Gga, "gga_r_o1_fock_ref.npy", "gga_r_o1_operands.npz"},
      {XcFamily::MggaTau, "mgga_tau_r_o1_fock_ref.npy",
       "mgga_tau_r_o1_operands.npz"},
  };
  for (const auto &c : cases) {
    require_file(kData / c.ref);
    require_file(kData / c.scal);
    XcKernel k(c.family, XcSpin::Restricted, 1);
    auto local = load_npz((kData / c.scal).string());
    local.insert(ops.begin(), ops.end());
    auto scal = scal_from_npz(local, k.scal_names());
    auto got = k.contract(npts, nbf, ops["chi"].data.data(), dchi.data(), lapl,
                          nullptr, scal);
    auto ref = load_npy((kData / c.ref).string());
    REQUIRE(ref.shape.size() == 2);
    REQUIRE(ref.shape[0] == 4);
    REQUIRE(ref.shape[1] == 4);
    INFO(k.name() << " rel=" << max_rel_diff(got, ref.data));
    REQUIRE(max_rel_diff(got, ref.data) <= 1e-16);
  }
}

TEST_CASE("GGA fxc vs pinned mol ref", "[xckernel]") {
  require_file(kData / "mol_h2o_sto3g_lvl3_operands.npz");
  require_file(kData / "gga_r_o2_fxc_ref.npy");
  auto ops = load_npz((kData / "mol_h2o_sto3g_lvl3_operands.npz").string());
  REQUIRE((ops.count("weights") || ops.count("w")));
  REQUIRE(ops.count("chi"));
  REQUIRE(ops.count("dchi"));
  const auto nbf = static_cast<std::int64_t>(ops["chi"].shape[0]);
  const auto npts = static_cast<std::int64_t>(ops["chi"].shape[1]);
  auto dchi = dchi_c_abi(ops["dchi"], nbf, npts);
  if (!ops.count("w") && ops.count("weights")) {
    ops.emplace("w", ops["weights"]);
  }
  XcKernel k(XcFamily::Gga, XcSpin::Restricted, 2);
  auto scal = scal_from_npz(ops, k.scal_names());
  auto got = k.contract(npts, nbf, ops["chi"].data.data(), dchi.data(), nullptr,
                        nullptr, scal);
  auto ref = load_npy((kData / "gga_r_o2_fxc_ref.npy").string());
  REQUIRE(max_abs_diff(got, ref.data) <= 1e-13);
}

TEST_CASE("C backend vs NumPy pin at 1e-16", "[xckernel]") {
  struct Case {
    XcFamily family;
    int order;
    const char *stem;
  };
  const Case cases[] = {
      {XcFamily::Lda, 1, "xck_lda_r_o1"},
      {XcFamily::Gga, 1, "xck_gga_r_o1"},
      {XcFamily::Gga, 2, "xck_gga_r_o2"},
      {XcFamily::MggaTau, 1, "xck_mgga_tau_r_o1"},
  };
  for (const auto &c : cases) {
    const fs::path ops_p =
        kData / "c_vs_numpy" / (std::string(c.stem) + "_operands.npz");
    const fs::path ref_p =
        kData / "c_vs_numpy" / (std::string(c.stem) + "_ref.npy");
    require_file(ops_p);
    require_file(ref_p);
    auto ops = load_npz(ops_p.string());
    auto ref = load_npy(ref_p.string());
    REQUIRE(ops.count("chi"));
    REQUIRE(ops.count("dchi"));
    const auto nbf = static_cast<std::int64_t>(ops["chi"].shape[0]);
    const auto npts = static_cast<std::int64_t>(ops["chi"].shape[1]);
    auto dchi = dchi_c_abi(ops["dchi"], nbf, npts);
    const double *lapl =
        ops.count("lapl_chi") ? ops["lapl_chi"].data.data() : nullptr;
    const double *hess =
        ops.count("hess_chi") ? ops["hess_chi"].data.data() : nullptr;
    XcKernel k(c.family, XcSpin::Restricted, c.order);
    auto scal = scal_from_npz(ops, k.scal_names());
    auto got = k.contract(npts, nbf, ops["chi"].data.data(), dchi.data(), lapl,
                          hess, scal);
    INFO(k.name() << " rel=" << max_rel_diff(got, ref.data)
                  << " abs=" << max_abs_diff(got, ref.data));
    REQUIRE(max_rel_diff(got, ref.data) <= 1e-16);
  }
}

TEST_CASE("PySCF Fock and fxc pins at paper tols", "[xckernel]") {
  const fs::path root = kData / "pyscf_h2o_sto3g";
  require_file(root / "lda_fock_ref.npy");
  require_file(root / "gga_fock_ref.npy");
  require_file(root / "mgga_tau_fock_ref.npy");
  require_file(root / "gga_fxc_ref.npy");
  require_file(kData / "mol_h2o_sto3g_lvl3_operands.npz");

  auto ops = load_npz((kData / "mol_h2o_sto3g_lvl3_operands.npz").string());
  if (!ops.count("w") && ops.count("weights")) {
    ops.emplace("w", ops["weights"]);
  }
  struct Case {
    XcFamily family;
    int order;
    const char *ref;
    double tol;
  };
  const Case cases[] = {
      {XcFamily::Lda, 1, "lda_fock_ref.npy", 1e-15},
      {XcFamily::Gga, 1, "gga_fock_ref.npy", 1e-15},
      {XcFamily::MggaTau, 1, "mgga_tau_fock_ref.npy", 1e-15},
      {XcFamily::Gga, 2, "gga_fxc_ref.npy", 1e-13},
  };
  for (const auto &c : cases) {
    XcKernel k(c.family, XcSpin::Restricted, c.order);
    std::unordered_map<std::string, NpyArray> local = ops;
    const fs::path extra = root / (k.name() + "_operands.npz");
    if (fs::exists(extra)) {
      auto more = load_npz(extra.string());
      for (auto &kv : more) {
        local[kv.first] = std::move(kv.second);
      }
    }
    auto scal = scal_from_npz(local, k.scal_names());
    const auto nbf_k = static_cast<std::int64_t>(local["chi"].shape[0]);
    const auto npts_k = static_cast<std::int64_t>(local["chi"].shape[1]);
    auto dchi_k = dchi_c_abi(local.at("dchi"), nbf_k, npts_k);
    const double *lapl_k =
        local.count("lapl_chi") ? local["lapl_chi"].data.data() : nullptr;
    auto got = k.contract(npts_k, nbf_k, local["chi"].data.data(), dchi_k.data(),
                          lapl_k, nullptr, scal);
    auto ref = load_npy((root / c.ref).string());
    INFO(k.name() << " abs=" << max_abs_diff(got, ref.data)
                  << " rel=" << max_rel_diff(got, ref.data));
    REQUIRE(max_abs_diff(got, ref.data) <= c.tol);
  }
}
