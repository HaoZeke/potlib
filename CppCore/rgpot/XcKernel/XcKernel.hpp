#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * In-process XC kernel contractions from libxckernel (arXiv:2608.26440).
 *
 * This is not a Potential. Inputs are grid collocation (chi, dchi, optional
 * lapl_chi / hess_chi), named Libxc derivative arrays, and (perturbed)
 * fields. Outputs are AO Fock-like matrices. Do not subclass Potential
 * and do not add a PotentialConfig.xckernel arm (rgpot-qf6b).
 *
 * Term ownership is XC-only: Coulomb, HF, and range-separated exchange
 * stay host-owned. libxckernel never evaluates functionals; the host
 * passes coefficient-mixed derivative arrays.
 *
 * Families (catalog FAMILY_VARS):
 *   lda        {rho}
 *   gga        {rho, sigma}
 *   mgga_tau   {rho, sigma, tau}     first slice
 *   mgga_lapl  {rho, sigma, lapl}    later slice; named, not generated
 *
 * First slice: lda, gga, mgga_tau; max_order 2 (Fock o1 + fxc o2).
 * Build with meson -Dwith_xckernel=true (default false).
 *
 * Reentrancy: one instance, one thread. Distinct instances may run
 * concurrently on distinct out/scal buffers.
 */

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rgpot {

class XcKernelError : public std::runtime_error {
public:
  explicit XcKernelError(const std::string &what) : std::runtime_error(what) {}
};

/// Catalog families. MggaLapl is in the 0cli title but not first-slice C.
enum class XcFamily { Lda, Gga, MggaTau, MggaLapl };

enum class XcSpin {
  Restricted,     ///< r
  UnrestrictedA,  ///< ua
  UnrestrictedB,  ///< ub
  SpinAdapted     ///< st (o2 p/m only in first slice)
};

/// Libxc input variable names for a family (catalog FAMILY_VARS).
[[nodiscard]] inline std::vector<std::string_view>
familyVars(XcFamily family) {
  switch (family) {
  case XcFamily::Lda:
    return {"rho"};
  case XcFamily::Gga:
    return {"rho", "sigma"};
  case XcFamily::MggaTau:
    return {"rho", "sigma", "tau"};
  case XcFamily::MggaLapl:
    return {"rho", "sigma", "lapl"};
  }
  return {};
}

[[nodiscard]] inline std::string_view familyName(XcFamily family) {
  switch (family) {
  case XcFamily::Lda:
    return "lda";
  case XcFamily::Gga:
    return "gga";
  case XcFamily::MggaTau:
    return "mgga_tau";
  case XcFamily::MggaLapl:
    return "mgga_lapl";
  }
  return "";
}

[[nodiscard]] inline bool isFirstSliceFamily(XcFamily family) {
  return family == XcFamily::Lda || family == XcFamily::Gga ||
         family == XcFamily::MggaTau;
}

[[nodiscard]] inline std::vector<XcFamily> firstSliceFamilies() {
  return {XcFamily::Lda, XcFamily::Gga, XcFamily::MggaTau};
}

/// Catalog name xck_<family>_<spin>_o<order>[_p|_m].
/// SpinAdapted requires order==2 and parity +1 (p) or -1 (m).
[[nodiscard]] std::string catalogName(XcFamily family, XcSpin spin, int order,
                                      int parity = 0);

class XcKernel {
public:
  using KernelFn = int (*)(std::int64_t npts, std::int64_t nbf,
                           const double *chi, const double *dchi,
                           const double *lapl_chi, const double *hess_chi,
                           const double *const *scal, double *out);

  /// Dispatch one catalog kernel by name (e.g. "xck_lda_r_o1").
  explicit XcKernel(std::string name);

  /// Dispatch by family / spin / order. MggaLapl throws (later slice).
  XcKernel(XcFamily family, XcSpin spin, int order, int parity = 0);

  [[nodiscard]] const std::string &name() const noexcept { return name_; }
  [[nodiscard]] XcFamily family() const noexcept { return family_; }
  [[nodiscard]] XcSpin spin() const noexcept { return spin_; }
  [[nodiscard]] int order() const noexcept { return order_; }
  [[nodiscard]] const std::vector<std::string> &scal_names() const noexcept {
    return scal_names_;
  }

  /// Accumulate the (nbf, nbf) matrix into `out` (+=), matching the C ABI.
  /// chi is nbf*npts row-major; dchi is 3*nbf*npts.
  void accumulate(
      std::int64_t npts, std::int64_t nbf, const double *chi,
      const double *dchi, const double *lapl_chi, const double *hess_chi,
      const std::unordered_map<std::string, const double *> &scal,
      double *out) const;

  /// Fresh (nbf*nbf) row-major matrix from accumulate on a zero buffer.
  [[nodiscard]] std::vector<double> contract(
      std::int64_t npts, std::int64_t nbf, const double *chi,
      const double *dchi, const double *lapl_chi, const double *hess_chi,
      const std::unordered_map<std::string, const double *> &scal) const;

  /// Compiled first-slice o1/o2 C kernel names (GIAO / o3 / o4 / lapl excluded).
  [[nodiscard]] static std::vector<std::string> first_slice_names();

  /// True when -Dwith_xckernel=true compiled the first-slice C ABI in.
  [[nodiscard]] static bool available() noexcept;

private:
  void resolve(const std::string &name);

  std::string name_;
  XcFamily family_ = XcFamily::Lda;
  XcSpin spin_ = XcSpin::Restricted;
  int order_ = 1;
  std::vector<std::string> scal_names_;
  KernelFn fn_ = nullptr;
};

} // namespace rgpot
