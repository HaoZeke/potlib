// MIT License
// Copyright 2023--present rgpot developers

#include "rgpot/XcKernel/XcKernel.hpp"

#include <map>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#ifdef RGPOT_HAS_XCKERNEL
#include "xckernel.h"
#include "xckernel/kernels/xck_gga_st_o2_p.hpp"
#include "xckernel/kernels/xck_lda_st_o2_p.hpp"

namespace {

struct KernelAbi {
  rgpot::XcKernel::KernelFn fn;
  const char **scal_names;
  const int *n_scal;
  const int *n_fields;
};

#define XCK_KERNEL(name)                                                       \
  {                                                                            \
#name, {                                                                   \
      name, name##_scal_names, &name##_n_scal, &name##_n_fields                \
    }                                                                          \
  }                                                                            \
  ,

const std::unordered_map<std::string, KernelAbi> kTable = {
#include "rgpot/XcKernel/kernel_table.inc"
};

#undef XCK_KERNEL

} // namespace
#endif

namespace rgpot {

XcKernel::XcKernel(std::string name) : m_name(std::move(name)) {
#ifdef RGPOT_HAS_XCKERNEL
  auto it = kTable.find(m_name);
  if (it == kTable.end()) {
    throw std::invalid_argument("unknown first-slice XcKernel: " + m_name);
  }
  m_fn = it->second.fn;
  m_scal_names = it->second.scal_names;
  m_n_scal = *it->second.n_scal;
  m_n_fields = *it->second.n_fields;
#else
  throw std::runtime_error(
      "XcKernel requires meson -Dwith_xckernel=true (generated C ABI)");
#endif
}

std::vector<std::string> XcKernel::scalNames() const {
  std::vector<std::string> out;
  out.reserve(static_cast<std::size_t>(m_n_scal));
  for (int i = 0; i < m_n_scal; ++i) {
    out.emplace_back(m_scal_names[i]);
  }
  return out;
}

std::vector<std::string> XcKernel::catalog() {
  std::vector<std::string> names;
#ifdef RGPOT_HAS_XCKERNEL
#define XCK_KERNEL(name) names.emplace_back(#name);
#include "rgpot/XcKernel/kernel_table.inc"
#undef XCK_KERNEL
#endif
  return names;
}

int XcKernel::contract(const XcGrid &grid,
                       const std::map<std::string, const double *> &scal,
                       double *out) const {
  if (m_fn == nullptr || out == nullptr) {
    return 1;
  }
  if (grid.npts <= 0 || grid.nbf <= 0 || grid.chi == nullptr ||
      grid.dchi == nullptr) {
    return 2;
  }
  std::vector<const double *> ptrs(static_cast<std::size_t>(m_n_scal), nullptr);
  for (int i = 0; i < m_n_scal; ++i) {
    auto it = scal.find(m_scal_names[i]);
    if (it == scal.end() || it->second == nullptr) {
      return 3;
    }
    ptrs[static_cast<std::size_t>(i)] = it->second;
  }
  return m_fn(grid.npts, grid.nbf, grid.chi, grid.dchi, grid.lapl_chi,
              grid.hess_chi, ptrs.data(), out);
}

XcFields XcKernel::fieldsFromDensity(const XcGrid &grid, const double *P) {
  XcFields f;
  const auto ng = static_cast<std::size_t>(grid.npts);
  const auto nbf = static_cast<std::size_t>(grid.nbf);
  f.rho.assign(ng, 0.0);
  f.sigma.assign(ng, 0.0);
  f.tau.assign(ng, 0.0);
  f.lapl.assign(ng, 0.0);
  f.grad_rho.assign(3 * ng, 0.0);
  if (P == nullptr || grid.chi == nullptr || grid.dchi == nullptr) {
    return f;
  }
  const double *chi = grid.chi;
  const double *dchi = grid.dchi;
  const double *lapl_chi = grid.lapl_chi;
  for (std::size_t u = 0; u < nbf; ++u) {
    for (std::size_t v = 0; v < nbf; ++v) {
      const double Puv = P[u * nbf + v];
      const double *chi_u = chi + u * ng;
      const double *chi_v = chi + v * ng;
      const double *dx_u = dchi + (0 * nbf + u) * ng;
      const double *dy_u = dchi + (1 * nbf + u) * ng;
      const double *dz_u = dchi + (2 * nbf + u) * ng;
      const double *dx_v = dchi + (0 * nbf + v) * ng;
      const double *dy_v = dchi + (1 * nbf + v) * ng;
      const double *dz_v = dchi + (2 * nbf + v) * ng;
      for (std::size_t g = 0; g < ng; ++g) {
        f.rho[g] += Puv * chi_u[g] * chi_v[g];
        f.grad_rho[g] += Puv * (dx_u[g] * chi_v[g] + chi_u[g] * dx_v[g]);
        f.grad_rho[ng + g] += Puv * (dy_u[g] * chi_v[g] + chi_u[g] * dy_v[g]);
        f.grad_rho[2 * ng + g] +=
            Puv * (dz_u[g] * chi_v[g] + chi_u[g] * dz_v[g]);
        f.tau[g] +=
            0.5 * Puv *
            (dx_u[g] * dx_v[g] + dy_u[g] * dy_v[g] + dz_u[g] * dz_v[g]);
        if (lapl_chi != nullptr) {
          f.lapl[g] += Puv * (lapl_chi[u * ng + g] * chi_v[g] +
                              2.0 * (dx_u[g] * dx_v[g] + dy_u[g] * dy_v[g] +
                                     dz_u[g] * dz_v[g]) +
                              chi_u[g] * lapl_chi[v * ng + g]);
        }
      }
    }
  }
  for (std::size_t g = 0; g < ng; ++g) {
    const double gx = f.grad_rho[g];
    const double gy = f.grad_rho[ng + g];
    const double gz = f.grad_rho[2 * ng + g];
    f.sigma[g] = gx * gx + gy * gy + gz * gz;
  }
  return f;
}

namespace {

using Ld = long double;

void promote(const double *src, std::size_t n, std::vector<Ld> *dst) {
  dst->assign(n, 0.0L);
  if (src == nullptr) {
    return;
  }
  for (std::size_t i = 0; i < n; ++i) {
    (*dst)[i] = src[i];
  }
}

void transition_dm_ld(const XcMo &mo, const double *z, Ld occ, Ld *dm) {
  const auto nao = static_cast<std::size_t>(mo.nao);
  const auto nocc = static_cast<std::size_t>(mo.nocc);
  const auto nvir = static_cast<std::size_t>(mo.nvir);
  const std::size_t n2 = nao * nao;
  for (std::size_t k = 0; k < n2; ++k) {
    dm[k] = 0.0L;
  }
  if (z == nullptr || mo.Co == nullptr || mo.Cv == nullptr) {
    return;
  }
  std::vector<Ld> tmp(nao * nocc, 0.0L);
  for (std::size_t p = 0; p < nao; ++p) {
    for (std::size_t i = 0; i < nocc; ++i) {
      Ld acc = 0.0L;
      for (std::size_t a = 0; a < nvir; ++a) {
        acc += static_cast<Ld>(mo.Cv[p * nvir + a]) * z[i * nvir + a];
      }
      tmp[p * nocc + i] = acc;
    }
  }
  for (std::size_t p = 0; p < nao; ++p) {
    for (std::size_t q = 0; q < nao; ++q) {
      Ld acc = 0.0L;
      for (std::size_t i = 0; i < nocc; ++i) {
        acc += tmp[p * nocc + i] * mo.Co[q * nocc + i];
      }
      dm[p * nao + q] = occ * acc;
    }
  }
}

void rpa_transition_dm_ld(const XcMo &mo, const double *x, const double *y,
                          Ld occ, Ld *dm) {
  transition_dm_ld(mo, x, occ, dm);
  const auto nao = static_cast<std::size_t>(mo.nao);
  const auto nocc = static_cast<std::size_t>(mo.nocc);
  const auto nvir = static_cast<std::size_t>(mo.nvir);
  if (y == nullptr || mo.Co == nullptr || mo.Cv == nullptr) {
    return;
  }
  std::vector<Ld> tmp(nao * nvir, 0.0L);
  for (std::size_t p = 0; p < nao; ++p) {
    for (std::size_t a = 0; a < nvir; ++a) {
      Ld acc = 0.0L;
      for (std::size_t i = 0; i < nocc; ++i) {
        acc += static_cast<Ld>(mo.Co[p * nocc + i]) * y[i * nvir + a];
      }
      tmp[p * nvir + a] = acc;
    }
  }
  for (std::size_t p = 0; p < nao; ++p) {
    for (std::size_t q = 0; q < nao; ++q) {
      Ld acc = 0.0L;
      for (std::size_t a = 0; a < nvir; ++a) {
        acc += tmp[p * nvir + a] * mo.Cv[q * nvir + a];
      }
      dm[p * nao + q] += occ * acc;
    }
  }
}

void project_ov_ld(const XcMo &mo, const Ld *Vao, Ld *ov) {
  const auto nao = static_cast<std::size_t>(mo.nao);
  const auto nocc = static_cast<std::size_t>(mo.nocc);
  const auto nvir = static_cast<std::size_t>(mo.nvir);
  const std::size_t nov = nocc * nvir;
  for (std::size_t k = 0; k < nov; ++k) {
    ov[k] = 0.0L;
  }
  if (Vao == nullptr || mo.Co == nullptr || mo.Cv == nullptr) {
    return;
  }
  std::vector<Ld> tmp(nao * nvir, 0.0L);
  for (std::size_t q = 0; q < nao; ++q) {
    for (std::size_t a = 0; a < nvir; ++a) {
      Ld acc = 0.0L;
      for (std::size_t p = 0; p < nao; ++p) {
        acc += Vao[p * nao + q] * mo.Cv[p * nvir + a];
      }
      tmp[q * nvir + a] = acc;
    }
  }
  for (std::size_t i = 0; i < nocc; ++i) {
    for (std::size_t a = 0; a < nvir; ++a) {
      Ld acc = 0.0L;
      for (std::size_t q = 0; q < nao; ++q) {
        acc += static_cast<Ld>(mo.Co[q * nocc + i]) * tmp[q * nvir + a];
      }
      ov[i * nvir + a] = acc;
    }
  }
}

void project_ov_noT_ld(const XcMo &mo, const Ld *Vao, Ld *ov) {
  const auto nao = static_cast<std::size_t>(mo.nao);
  const auto nocc = static_cast<std::size_t>(mo.nocc);
  const auto nvir = static_cast<std::size_t>(mo.nvir);
  const std::size_t nov = nocc * nvir;
  for (std::size_t k = 0; k < nov; ++k) {
    ov[k] = 0.0L;
  }
  if (Vao == nullptr || mo.Co == nullptr || mo.Cv == nullptr) {
    return;
  }
  std::vector<Ld> tmp(nao * nvir, 0.0L);
  for (std::size_t p = 0; p < nao; ++p) {
    for (std::size_t a = 0; a < nvir; ++a) {
      Ld acc = 0.0L;
      for (std::size_t q = 0; q < nao; ++q) {
        acc += Vao[p * nao + q] * mo.Cv[q * nvir + a];
      }
      tmp[p * nvir + a] = acc;
    }
  }
  for (std::size_t i = 0; i < nocc; ++i) {
    for (std::size_t a = 0; a < nvir; ++a) {
      Ld acc = 0.0L;
      for (std::size_t p = 0; p < nao; ++p) {
        acc += static_cast<Ld>(mo.Co[p * nocc + i]) * tmp[p * nvir + a];
      }
      ov[i * nvir + a] = acc;
    }
  }
}

#ifdef RGPOT_HAS_XCKERNEL
int apply_fxc_ld(const XcKernel &k, const XcGrid &grid,
                 const std::map<std::string, const double *> &ground,
                 const Ld *dm, Ld *vxc) {
  if (dm == nullptr || vxc == nullptr) {
    return 4;
  }
  const auto ng = static_cast<std::size_t>(grid.npts);
  const auto nbf = static_cast<std::size_t>(grid.nbf);
  const std::size_t n2 = nbf * nbf;
  for (std::size_t i = 0; i < n2; ++i) {
    vxc[i] = 0.0L;
  }
  if (grid.chi == nullptr || grid.npts <= 0 || grid.nbf <= 0) {
    return 2;
  }

  std::vector<Ld> chi_ld;
  std::vector<Ld> dchi_ld;
  promote(grid.chi, nbf * ng, &chi_ld);
  if (grid.dchi != nullptr) {
    promote(grid.dchi, 3 * nbf * ng, &dchi_ld);
  } else {
    dchi_ld.assign(3 * nbf * ng, 0.0L);
  }

  std::vector<Ld> rho_p1(ng, 0.0L);
  std::vector<Ld> gx(ng, 0.0L);
  std::vector<Ld> gy(ng, 0.0L);
  std::vector<Ld> gz(ng, 0.0L);
  const Ld *chi = chi_ld.data();
  const Ld *dchi = dchi_ld.data();
  for (std::size_t u = 0; u < nbf; ++u) {
    for (std::size_t v = 0; v < nbf; ++v) {
      const Ld Puv = dm[u * nbf + v];
      const Ld *chi_u = chi + u * ng;
      const Ld *chi_v = chi + v * ng;
      const Ld *dx_u = dchi + (0 * nbf + u) * ng;
      const Ld *dy_u = dchi + (1 * nbf + u) * ng;
      const Ld *dz_u = dchi + (2 * nbf + u) * ng;
      const Ld *dx_v = dchi + (0 * nbf + v) * ng;
      const Ld *dy_v = dchi + (1 * nbf + v) * ng;
      const Ld *dz_v = dchi + (2 * nbf + v) * ng;
      for (std::size_t g = 0; g < ng; ++g) {
        rho_p1[g] += Puv * chi_u[g] * chi_v[g];
        gx[g] += Puv * (dx_u[g] * chi_v[g] + chi_u[g] * dx_v[g]);
        gy[g] += Puv * (dy_u[g] * chi_v[g] + chi_u[g] * dy_v[g]);
        gz[g] += Puv * (dz_u[g] * chi_v[g] + chi_u[g] * dz_v[g]);
      }
    }
  }

  const int nfld = k.nFields();
  const int nscal = k.nScal();
  if (nfld <= 0 || nscal < nfld) {
    return 3;
  }
  auto names = k.scalNames();
  std::vector<std::vector<Ld>> field_store;
  field_store.reserve(static_cast<std::size_t>(nfld));
  std::vector<const Ld *> field_ptrs(static_cast<std::size_t>(nfld), nullptr);
  std::vector<const double *> xc_ptrs(
      static_cast<std::size_t>(nscal - nfld), nullptr);

  for (int i = 0; i < nscal; ++i) {
    const std::string &name = names[static_cast<std::size_t>(i)];
    if (i < nfld) {
      const Ld *ptr = nullptr;
      if (name == "rho_a_p1" || name == "rho_p1") {
        ptr = rho_p1.data();
      } else if (name == "grad_rho_a_p1_x" || name == "grad_rho_p1_x") {
        ptr = gx.data();
      } else if (name == "grad_rho_a_p1_y" || name == "grad_rho_p1_y") {
        ptr = gy.data();
      } else if (name == "grad_rho_a_p1_z" || name == "grad_rho_p1_z") {
        ptr = gz.data();
      } else {
        auto it = ground.find(name);
        if (it == ground.end() || it->second == nullptr) {
          return 3;
        }
        field_store.emplace_back();
        promote(it->second, ng, &field_store.back());
        ptr = field_store.back().data();
      }
      field_ptrs[static_cast<std::size_t>(i)] = ptr;
    } else {
      auto it = ground.find(name);
      if (it == ground.end() || it->second == nullptr) {
        return 3;
      }
      xc_ptrs[static_cast<std::size_t>(i - nfld)] = it->second;
    }
  }

  if (k.name() == "xck_lda_st_o2_p") {
    return xckernel::xck_lda_st_o2_p_t<Ld, double>(
        grid.npts, grid.nbf, chi_ld.data(), dchi_ld.data(), nullptr, nullptr,
        field_ptrs.data(), xc_ptrs.data(), vxc);
  }
  if (k.name() == "xck_gga_st_o2_p") {
    return xckernel::xck_gga_st_o2_p_t<Ld, double>(
        grid.npts, grid.nbf, chi_ld.data(), dchi_ld.data(), nullptr, nullptr,
        field_ptrs.data(), xc_ptrs.data(), vxc);
  }
  return 5;
}
#endif

} // namespace

void XcKernel::transitionDm(const XcMo &mo, const double *z, double occ,
                            double *dm) {
  const auto nao = static_cast<std::size_t>(mo.nao);
  std::vector<Ld> tmp(nao * nao, 0.0L);
  transition_dm_ld(mo, z, occ, tmp.data());
  if (dm != nullptr) {
    for (std::size_t k = 0; k < nao * nao; ++k) {
      dm[k] = static_cast<double>(tmp[k]);
    }
  }
}

void XcKernel::rpaTransitionDm(const XcMo &mo, const double *x, const double *y,
                               double occ, double *dm) {
  const auto nao = static_cast<std::size_t>(mo.nao);
  std::vector<Ld> tmp(nao * nao, 0.0L);
  rpa_transition_dm_ld(mo, x, y, occ, tmp.data());
  if (dm != nullptr) {
    for (std::size_t k = 0; k < nao * nao; ++k) {
      dm[k] = static_cast<double>(tmp[k]);
    }
  }
}

void XcKernel::projectOv(const XcMo &mo, const double *Vao, double *ov) {
  const auto nao = static_cast<std::size_t>(mo.nao);
  const auto nov =
      static_cast<std::size_t>(mo.nocc) * static_cast<std::size_t>(mo.nvir);
  std::vector<Ld> Vld;
  promote(Vao, nao * nao, &Vld);
  std::vector<Ld> ovld(nov, 0.0L);
  project_ov_ld(mo, Vld.data(), ovld.data());
  if (ov != nullptr) {
    for (std::size_t k = 0; k < nov; ++k) {
      ov[k] = static_cast<double>(ovld[k]);
    }
  }
}

void XcKernel::tdaSigma(const XcMo &mo, const double *z, const double *v1,
                        double *sigma) {
  projectOv(mo, v1, sigma);
  const auto nocc = static_cast<std::size_t>(mo.nocc);
  const auto nvir = static_cast<std::size_t>(mo.nvir);
  if (z == nullptr || mo.e_ia == nullptr || sigma == nullptr) {
    return;
  }
  for (std::size_t i = 0; i < nocc; ++i) {
    for (std::size_t a = 0; a < nvir; ++a) {
      const std::size_t ia = i * nvir + a;
      sigma[ia] = static_cast<double>(static_cast<Ld>(sigma[ia]) +
                                      static_cast<Ld>(mo.e_ia[ia]) * z[ia]);
    }
  }
}

void XcKernel::rpaSigma(const XcMo &mo, const double *xy, const double *v1,
                        double *sigma) {
  const auto nao = static_cast<std::size_t>(mo.nao);
  const auto nocc = static_cast<std::size_t>(mo.nocc);
  const auto nvir = static_cast<std::size_t>(mo.nvir);
  const std::size_t nov = nocc * nvir;
  tdaSigma(mo, xy, v1, sigma);
  if (xy == nullptr || v1 == nullptr || sigma == nullptr ||
      mo.e_ia == nullptr) {
    return;
  }
  const double *y = xy + nov;
  double *bot = sigma + nov;
  std::vector<Ld> Vld;
  promote(v1, nao * nao, &Vld);
  std::vector<Ld> ovld(nov, 0.0L);
  project_ov_noT_ld(mo, Vld.data(), ovld.data());
  for (std::size_t ia = 0; ia < nov; ++ia) {
    bot[ia] = -static_cast<double>(static_cast<Ld>(mo.e_ia[ia]) * y[ia] +
                                   ovld[ia]);
  }
}

int XcKernel::applyFxc(const XcGrid &grid,
                       const std::map<std::string, const double *> &ground,
                       const double *dm, double *vxc) const {
  if (dm == nullptr || vxc == nullptr) {
    return 4;
  }
  const auto n2 =
      static_cast<std::size_t>(grid.nbf) * static_cast<std::size_t>(grid.nbf);
#ifdef RGPOT_HAS_XCKERNEL
  std::vector<Ld> dm_ld;
  promote(dm, n2, &dm_ld);
  std::vector<Ld> vxc_ld(n2, 0.0L);
  const int rc = apply_fxc_ld(*this, grid, ground, dm_ld.data(), vxc_ld.data());
  if (rc != 0) {
    return rc;
  }
  for (std::size_t k = 0; k < n2; ++k) {
    vxc[k] += static_cast<double>(vxc_ld[k]);
  }
  return 0;
#else
  (void)grid;
  (void)ground;
  (void)n2;
  return 1;
#endif
}

int XcKernel::tdaSigma(const XcGrid &grid,
                       const std::map<std::string, const double *> &ground,
                       const XcMo &mo, const double *z, const double *vj,
                       double *sigma) const {
  if (z == nullptr || vj == nullptr || sigma == nullptr || mo.nao <= 0) {
    return 4;
  }
#ifdef RGPOT_HAS_XCKERNEL
  const auto nao = static_cast<std::size_t>(mo.nao);
  const auto nov =
      static_cast<std::size_t>(mo.nocc) * static_cast<std::size_t>(mo.nvir);
  std::vector<Ld> dm(nao * nao, 0.0L);
  std::vector<Ld> vxc(nao * nao, 0.0L);
  std::vector<Ld> v1(nao * nao, 0.0L);
  transition_dm_ld(mo, z, 2.0L, dm.data());
  const int rc = apply_fxc_ld(*this, grid, ground, dm.data(), vxc.data());
  if (rc != 0) {
    return rc;
  }
  for (std::size_t k = 0; k < nao * nao; ++k) {
    v1[k] = static_cast<Ld>(vj[k]) + 0.5L * vxc[k];
  }
  std::vector<Ld> ov(nov, 0.0L);
  project_ov_ld(mo, v1.data(), ov.data());
  for (std::size_t ia = 0; ia < nov; ++ia) {
    sigma[ia] = static_cast<double>(ov[ia] + static_cast<Ld>(mo.e_ia[ia]) * z[ia]);
  }
  return 0;
#else
  (void)grid;
  (void)ground;
  return 1;
#endif
}

int XcKernel::rpaSigma(const XcGrid &grid,
                       const std::map<std::string, const double *> &ground,
                       const XcMo &mo, const double *xy, const double *vj,
                       double *sigma) const {
  if (xy == nullptr || vj == nullptr || sigma == nullptr || mo.nao <= 0) {
    return 4;
  }
#ifdef RGPOT_HAS_XCKERNEL
  const auto nao = static_cast<std::size_t>(mo.nao);
  const auto nov =
      static_cast<std::size_t>(mo.nocc) * static_cast<std::size_t>(mo.nvir);
  std::vector<Ld> dm(nao * nao, 0.0L);
  std::vector<Ld> vxc(nao * nao, 0.0L);
  std::vector<Ld> v1(nao * nao, 0.0L);
  rpa_transition_dm_ld(mo, xy, xy + nov, 2.0L, dm.data());
  const int rc = apply_fxc_ld(*this, grid, ground, dm.data(), vxc.data());
  if (rc != 0) {
    return rc;
  }
  for (std::size_t k = 0; k < nao * nao; ++k) {
    v1[k] = static_cast<Ld>(vj[k]) + 0.5L * vxc[k];
  }
  std::vector<Ld> top(nov, 0.0L);
  std::vector<Ld> bot(nov, 0.0L);
  project_ov_ld(mo, v1.data(), top.data());
  project_ov_noT_ld(mo, v1.data(), bot.data());
  const double *y = xy + nov;
  for (std::size_t ia = 0; ia < nov; ++ia) {
    sigma[ia] =
        static_cast<double>(top[ia] + static_cast<Ld>(mo.e_ia[ia]) * xy[ia]);
    sigma[nov + ia] =
        -static_cast<double>(bot[ia] + static_cast<Ld>(mo.e_ia[ia]) * y[ia]);
  }
  return 0;
#else
  (void)grid;
  (void)ground;
  return 1;
#endif
}

} // namespace rgpot
