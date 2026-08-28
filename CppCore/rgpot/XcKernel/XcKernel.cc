// MIT License
// Copyright 2023--present rgpot developers

#include "rgpot/XcKernel/XcKernel.hpp"

#include <cstdlib>
#include <sstream>

#ifdef RGPOT_HAS_XCKERNEL
#include "xckernel.h"
#endif

namespace rgpot {
namespace {

#ifdef RGPOT_HAS_XCKERNEL
struct KernelEntry {
  const char *name;
  XcKernel::KernelFn fn;
  const char **scal_names;
  const int *n_scal;
};

#define XCK_ENTRY(sym)                                                         \
  {#sym, &sym, sym##_scal_names, &sym##_n_scal}

// First-slice C ABI: lda/gga/mgga_tau, r/ua/ub o1+o2 and st o2 p/m.
// Energy helpers (o0), GIAO, and mgga_lapl are not dispatched here.
const KernelEntry kTable[] = {
    XCK_ENTRY(xck_lda_r_o1),
    XCK_ENTRY(xck_lda_r_o2),
    XCK_ENTRY(xck_lda_ua_o1),
    XCK_ENTRY(xck_lda_ua_o2),
    XCK_ENTRY(xck_lda_ub_o1),
    XCK_ENTRY(xck_lda_ub_o2),
    XCK_ENTRY(xck_lda_st_o2_p),
    XCK_ENTRY(xck_lda_st_o2_m),
    XCK_ENTRY(xck_gga_r_o1),
    XCK_ENTRY(xck_gga_r_o2),
    XCK_ENTRY(xck_gga_ua_o1),
    XCK_ENTRY(xck_gga_ua_o2),
    XCK_ENTRY(xck_gga_ub_o1),
    XCK_ENTRY(xck_gga_ub_o2),
    XCK_ENTRY(xck_gga_st_o2_p),
    XCK_ENTRY(xck_gga_st_o2_m),
    XCK_ENTRY(xck_mgga_tau_r_o1),
    XCK_ENTRY(xck_mgga_tau_r_o2),
    XCK_ENTRY(xck_mgga_tau_ua_o1),
    XCK_ENTRY(xck_mgga_tau_ua_o2),
    XCK_ENTRY(xck_mgga_tau_ub_o1),
    XCK_ENTRY(xck_mgga_tau_ub_o2),
    XCK_ENTRY(xck_mgga_tau_st_o2_p),
    XCK_ENTRY(xck_mgga_tau_st_o2_m),
};

#undef XCK_ENTRY

const KernelEntry *find_entry(const std::string &name) {
  for (const auto &e : kTable) {
    if (name == e.name) {
      return &e;
    }
  }
  return nullptr;
}
#endif

bool parse_catalog_name(const std::string &name, XcFamily &family, XcSpin &spin,
                        int &order) {
  // xck_<family>_<spin>_o<N>[_p|_m]
  if (name.rfind("xck_", 0) != 0) {
    return false;
  }
  const std::string rest = name.substr(4);
  std::string fam;
  if (rest.rfind("mgga_tau_", 0) == 0) {
    family = XcFamily::MggaTau;
    fam = "mgga_tau_";
  } else if (rest.rfind("mgga_lapl_", 0) == 0) {
    family = XcFamily::MggaLapl;
    fam = "mgga_lapl_";
  } else if (rest.rfind("gga_", 0) == 0) {
    family = XcFamily::Gga;
    fam = "gga_";
  } else if (rest.rfind("lda_", 0) == 0) {
    family = XcFamily::Lda;
    fam = "lda_";
  } else {
    return false;
  }
  const std::string tail = rest.substr(fam.size());
  if (tail.rfind("r_o", 0) == 0) {
    spin = XcSpin::Restricted;
  } else if (tail.rfind("ua_o", 0) == 0) {
    spin = XcSpin::UnrestrictedA;
  } else if (tail.rfind("ub_o", 0) == 0) {
    spin = XcSpin::UnrestrictedB;
  } else if (tail.rfind("st_o", 0) == 0) {
    spin = XcSpin::SpinAdapted;
  } else {
    return false;
  }
  const auto opos = tail.find("_o");
  if (opos == std::string::npos || opos + 2 >= tail.size()) {
    return false;
  }
  char *end = nullptr;
  const long v = std::strtol(tail.c_str() + opos + 2, &end, 10);
  if (end == tail.c_str() + opos + 2 || v < 0 || v > 4) {
    return false;
  }
  order = static_cast<int>(v);
  return true;
}

} // namespace

std::string catalogName(XcFamily family, XcSpin spin, int order, int parity) {
  if (order < 0 || order > 4) {
    throw XcKernelError("order must be in [0, 4]");
  }
  std::string spin_tag;
  switch (spin) {
  case XcSpin::Restricted:
    spin_tag = "r";
    break;
  case XcSpin::UnrestrictedA:
    spin_tag = "ua";
    break;
  case XcSpin::UnrestrictedB:
    spin_tag = "ub";
    break;
  case XcSpin::SpinAdapted:
    spin_tag = "st";
    break;
  }
  std::string name = "xck_";
  name += familyName(family);
  name += "_";
  name += spin_tag;
  name += "_o";
  name += std::to_string(order);
  if (spin == XcSpin::SpinAdapted) {
    if (order != 2 || (parity != 1 && parity != -1)) {
      throw XcKernelError(
          "spin-adapted first-slice kernels are o2 with parity +1 (p) or -1 (m)");
    }
    name += (parity > 0) ? "_p" : "_m";
  }
  return name;
}

void XcKernel::resolve(const std::string &name) {
#ifndef RGPOT_HAS_XCKERNEL
  throw XcKernelError(
      "XcKernel requires meson -Dwith_xckernel=true (libxckernel first slice)");
#else
  if (!parse_catalog_name(name, family_, spin_, order_)) {
    throw XcKernelError("unparseable kernel name '" + name + "'");
  }
  if (family_ == XcFamily::MggaLapl) {
    throw XcKernelError(
        "mgga_lapl is a later slice (not generated in first-slice C ABI)");
  }
  if (!isFirstSliceFamily(family_)) {
    throw XcKernelError("family '" + std::string(familyName(family_)) +
                        "' is not in the first slice");
  }
  const KernelEntry *e = find_entry(name);
  if (e == nullptr) {
    throw XcKernelError("unknown first-slice kernel '" + name + "'");
  }
  fn_ = e->fn;
  const int n = *e->n_scal;
  scal_names_.clear();
  scal_names_.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    scal_names_.emplace_back(e->scal_names[i]);
  }
#endif
}

XcKernel::XcKernel(std::string name) : name_(std::move(name)) {
  resolve(name_);
}

XcKernel::XcKernel(XcFamily family, XcSpin spin, int order, int parity)
    : name_(catalogName(family, spin, order, parity)), family_(family),
      spin_(spin), order_(order) {
  resolve(name_);
}

void XcKernel::accumulate(
    std::int64_t npts, std::int64_t nbf, const double *chi, const double *dchi,
    const double *lapl_chi, const double *hess_chi,
    const std::unordered_map<std::string, const double *> &scal,
    double *out) const {
#ifndef RGPOT_HAS_XCKERNEL
  (void)npts;
  (void)nbf;
  (void)chi;
  (void)dchi;
  (void)lapl_chi;
  (void)hess_chi;
  (void)scal;
  (void)out;
  throw XcKernelError(
      "XcKernel requires meson -Dwith_xckernel=true (libxckernel first slice)");
#else
  if (fn_ == nullptr || out == nullptr || chi == nullptr || dchi == nullptr) {
    throw XcKernelError(name_ + ": null pointer");
  }
  if (npts <= 0 || nbf <= 0) {
    throw XcKernelError(name_ + ": npts and nbf must be positive");
  }
  std::vector<const double *> packed;
  packed.reserve(scal_names_.size());
  std::vector<std::string> missing;
  for (const auto &key : scal_names_) {
    auto it = scal.find(key);
    if (it == scal.end() || it->second == nullptr) {
      missing.push_back(key);
    } else {
      packed.push_back(it->second);
    }
  }
  if (!missing.empty()) {
    std::ostringstream oss;
    oss << name_ << ": missing operands [";
    for (std::size_t i = 0; i < missing.size(); ++i) {
      if (i) {
        oss << ", ";
      }
      oss << missing[i];
    }
    oss << "]";
    throw XcKernelError(oss.str());
  }
  const int rc =
      fn_(npts, nbf, chi, dchi, lapl_chi, hess_chi, packed.data(), out);
  if (rc != 0) {
    throw XcKernelError(name_ + " returned " + std::to_string(rc));
  }
#endif
}

std::vector<double> XcKernel::contract(
    std::int64_t npts, std::int64_t nbf, const double *chi, const double *dchi,
    const double *lapl_chi, const double *hess_chi,
    const std::unordered_map<std::string, const double *> &scal) const {
  std::vector<double> out(static_cast<std::size_t>(nbf * nbf), 0.0);
  accumulate(npts, nbf, chi, dchi, lapl_chi, hess_chi, scal, out.data());
  return out;
}

std::vector<std::string> XcKernel::first_slice_names() {
#ifdef RGPOT_HAS_XCKERNEL
  std::vector<std::string> names;
  names.reserve(sizeof(kTable) / sizeof(kTable[0]));
  for (const auto &e : kTable) {
    names.emplace_back(e.name);
  }
  return names;
#else
  return {};
#endif
}

bool XcKernel::available() noexcept {
#ifdef RGPOT_HAS_XCKERNEL
  return true;
#else
  return false;
#endif
}

} // namespace rgpot
