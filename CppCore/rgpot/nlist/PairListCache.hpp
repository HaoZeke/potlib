#pragma once
// MIT License
// Copyright 2023--present rgpot developers
//
// Verlet-skin cached pair lists for classical pair potentials, ported from
// eOn's eonc::PairListCache (TheochemUI/eOn, fix/vesin-neighbor-perf).
//
// Header-only and dependency-free: the MIC regime (orthorhombic box, true
// cutoff within half the smallest periodic width) uses the fused
// brute-force scan from vesin_visit.hpp; outside that regime every call
// runs the eval-only scan, which reproduces the historical per-call MIC
// loop exactly, so behaviour never degrades below the uncached code.
//
// Design invariants (see the eOn failure analysis for the derivation):
// - Slots are immutable after build and handed out as shared_ptr, so
//   readers never race eviction; the pool mutex covers only the
//   proximity match, never the physics.
// - Evaluation always re-derives vectors from current positions with a
//   per-call MIC fold, filtered at the true cutoff: results match a fresh
//   scan bit-for-bit in pair content while every atom stays within skin/2
//   of the build positions (Verlet guarantee).
// - Lazy capture: the first sighting of a geometry family runs the fused
//   eval-only scan and records a phantom reference stamp; a second
//   sighting within skin/2 proves reuse and captures the list. One-shot
//   evaluations never pay list capture.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "rgpot/nlist/vesin_visit.hpp"

namespace rgpot {
namespace nlist {

class CachedPairList {
public:
  struct Options {
    double cutoff{0.0};
    double skin{1.0};
    std::array<bool, 3> periodic{{true, true, true}};

    bool operator==(const Options &o) const {
      return cutoff == o.cutoff && skin == o.skin && periodic == o.periodic;
    }
  };

  /// True while the cached pair set is valid for positions ``R``: same
  /// atom count, options and box as the build, and max displacement
  /// below skin/2.
  [[nodiscard]] bool valid(const double *R, std::size_t n, const double *box,
                           const Options &opt) const {
    if (!built_ || n != n_ || !(opt == opt_)) {
      return false;
    }
    for (int k = 0; k < 9; ++k) {
      if (box[k] != boxref_[k]) {
        return false;
      }
    }
    if (complete_) {
      return true; // all pairs are candidates; motion cannot invalidate
    }
    const double thr2 = 0.25 * opt_.skin * opt_.skin;
    const double *ref = Rref_.data();
    for (std::size_t a = 0; a < n; ++a) {
      const double dx = R[3 * a] - ref[3 * a];
      const double dy = R[3 * a + 1] - ref[3 * a + 1];
      const double dz = R[3 * a + 2] - ref[3 * a + 2];
      if (dx * dx + dy * dy + dz * dz > thr2) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool isPhantom() const { return phantom_; }

  /// First sighting: fused eval-only scan plus a phantom stamp (MIC
  /// regime), or just the scan when caching is unsafe for this box.
  template <typename Fn>
  void visitOnly(const double *R, std::size_t n, const double *box,
                 const Options &opt, Fn &&fn) {
    setup(n, box, opt);
    double w[3];
    double inv[3];
    fold_params(box, opt, w, inv);
    vesin::cpu::brute_force_visit_only(
        R, n, w, inv, opt.cutoff * opt.cutoff,
        [&](int32_t i, int32_t j, double dx, double dy, double dz,
            double r2) { fn(i, j, -dx, -dy, -dz, r2); });
    if (mic_) {
      pairsIJ_.clear();
      finishRebuild(R, n, box, opt);
      phantom_ = true;
    }
  }

  /// Second sighting: capture the candidate list at cutoff+skin while
  /// evaluating ``fn`` in the same scan.
  template <typename Fn>
  void rebuildFused(const double *R, std::size_t n, const double *box,
                    const Options &opt, Fn &&fn) {
    setup(n, box, opt);
    double w[3];
    double inv[3];
    fold_params(box, opt, w, inv);
    const double bc = opt.cutoff + opt.skin;
    vesin::cpu::brute_force_visit(
        R, n, w, inv, bc * bc, opt.cutoff * opt.cutoff, pairsIJ_,
        [&](int32_t i, int32_t j, double dx, double dy, double dz,
            double r2) { fn(i, j, -dx, -dy, -dz, r2); });
    finishRebuild(R, n, box, opt);
  }

  /// True when caching applies to this box (decided by the last build).
  [[nodiscard]] bool cacheable() const { return mic_; }

  /// Visit every cached pair within the true cutoff of the current
  /// positions; ``fn(i, j, dx, dy, dz, r2)`` uses ``d = r_i - r_j`` with
  /// the minimum image applied.
  template <typename Fn> void forEach(const double *R, Fn &&fn) const {
    const double cutoff2 = opt_.cutoff * opt_.cutoff;
    const std::size_t np = pairsIJ_.size();
    const int32_t *ij = pairsIJ_.data();
    if (micInv_[0] == 0.0 && micInv_[1] == 0.0 && micInv_[2] == 0.0) {
      for (std::size_t p = 0; p < np; p += 2) {
        const int32_t i = ij[p];
        const int32_t j = ij[p + 1];
        const double dx = R[3 * i] - R[3 * j];
        const double dy = R[3 * i + 1] - R[3 * j + 1];
        const double dz = R[3 * i + 2] - R[3 * j + 2];
        const double r2 = dx * dx + dy * dy + dz * dz;
        if (r2 <= cutoff2) {
          fn(i, j, dx, dy, dz, r2);
        }
      }
      return;
    }
    const double w0 = boxref_[0], w1 = boxref_[4], w2 = boxref_[8];
    const double i0 = micInv_[0], i1 = micInv_[1], i2 = micInv_[2];
    for (std::size_t p = 0; p < np; p += 2) {
      const int32_t i = ij[p];
      const int32_t j = ij[p + 1];
      double dx = R[3 * i] - R[3 * j];
      double dy = R[3 * i + 1] - R[3 * j + 1];
      double dz = R[3 * i + 2] - R[3 * j + 2];
      dx -= w0 * vesin::cpu::visit_round(dx * i0);
      dy -= w1 * vesin::cpu::visit_round(dy * i1);
      dz -= w2 * vesin::cpu::visit_round(dz * i2);
      const double r2 = dx * dx + dy * dy + dz * dz;
      if (r2 <= cutoff2) {
        fn(i, j, dx, dy, dz, r2);
      }
    }
  }

private:
  void setup(std::size_t n, const double *box, const Options &opt) {
    const bool orthorhombic = box[1] == 0.0 && box[2] == 0.0 &&
                              box[3] == 0.0 && box[5] == 0.0 &&
                              box[6] == 0.0 && box[7] == 0.0;
    mic_ = orthorhombic && n <= 20000;
    for (int k = 0; mic_ && k < 3; ++k) {
      if (opt.periodic[static_cast<std::size_t>(k)] &&
          opt.cutoff > 0.5 * box[4 * k]) {
        mic_ = false;
      }
    }
  }

  static void fold_params(const double *box, const Options &opt, double w[3],
                          double inv[3]) {
    for (int k = 0; k < 3; ++k) {
      w[k] = box[4 * k];
      inv[k] = (opt.periodic[static_cast<std::size_t>(k)] && w[k] != 0.0)
                   ? 1.0 / w[k]
                   : 0.0;
    }
  }

  void finishRebuild(const double *R, std::size_t n, const double *box,
                     const Options &opt) {
    for (int k = 0; k < 3; ++k) {
      micInv_[static_cast<std::size_t>(k)] =
          (mic_ && opt.periodic[static_cast<std::size_t>(k)])
              ? 1.0 / box[4 * k]
              : 0.0;
    }
    Rref_.assign(R, R + 3 * n);
    for (int k = 0; k < 9; ++k) {
      boxref_[k] = box[k];
    }
    opt_ = opt;
    n_ = n;
    complete_ = mic_ && pairsIJ_.size() / 2 == n * (n - 1) / 2;
    built_ = true;
  }

  std::vector<int32_t> pairsIJ_;
  std::vector<double> Rref_;
  std::array<double, 9> boxref_{};
  std::array<double, 3> micInv_{};
  Options opt_{};
  std::size_t n_{0};
  bool mic_{false};
  bool phantom_{false};
  bool complete_{false};
  bool built_{false};
};

/// Process-wide pool of CachedPairList slots, matched by geometry
/// proximity; safe for shared potential instances evaluated from
/// short-lived worker threads.
class PairListCache {
public:
  static constexpr std::size_t kMaxSlots = 8;

  /// Single-pass evaluation with lazy list capture (see file header).
  template <typename Fn>
  void evaluate(const double *R, std::size_t n, const double *box,
                const CachedPairList::Options &opt, Fn &&fn) {
    std::shared_ptr<const CachedPairList> hit;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (std::size_t s = 0; s < slots_.size(); ++s) {
        if (slots_[s]->valid(R, n, box, opt)) {
          if (s != 0) {
            auto slot = std::move(slots_[s]);
            slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(s));
            slots_.insert(slots_.begin(), std::move(slot));
          }
          hit = slots_.front();
          break;
        }
      }
    }

    if (hit && !hit->isPhantom()) {
      hit->forEach(R, std::forward<Fn>(fn));
      return;
    }

    auto fresh = std::make_shared<CachedPairList>();
    if (hit) {
      fresh->rebuildFused(R, n, box, opt, fn);
    } else {
      fresh->visitOnly(R, n, box, opt, fn);
      if (!fresh->cacheable()) {
        return; // box unsafe for caching; behave exactly like the old loop
      }
    }

    std::lock_guard<std::mutex> lock(mu_);
    if (hit) {
      for (std::size_t s = 0; s < slots_.size(); ++s) {
        if (slots_[s] == hit) {
          slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(s));
          break;
        }
      }
    }
    slots_.insert(slots_.begin(), fresh);
    if (slots_.size() > kMaxSlots) {
      slots_.pop_back();
    }
  }

  static PairListCache &global() {
    static PairListCache cache;
    return cache;
  }

private:
  std::mutex mu_;
  std::vector<std::shared_ptr<CachedPairList>> slots_;
};

} // namespace nlist
} // namespace rgpot
