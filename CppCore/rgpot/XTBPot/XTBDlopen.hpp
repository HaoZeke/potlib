#pragma once
// MIT License — thin XTB frontend: dlopen libxtb_engine.so
#include "rgpot/ForceStructs.hpp"
#include "rgpot/XTBPot/XTBPot.hpp"
#include "rgpot/XTBPot/xtb_c_abi.h"

#include <string>

namespace rgpot {

/**
 * Slow/plugin path: loads libxtb_engine.so at runtime (linked libxtb lives
 * inside the engine). Search order: config.engine_path,
 * RGPOT_XTB_ENGINE, XTB_ENGINE, bare libxtb_engine.so, then
 * EON_POTENTIALS_PATH / RGPOT_ENGINE_PATH directories.
 */
struct XTBDlopenConfig {
  XTBConfig xtb{};
  std::string engine_path;
};

class XTBDlopen {
public:
  explicit XTBDlopen(const XTBDlopenConfig &config);
  explicit XTBDlopen(const XTBConfig &xtb = XTBConfig{});
  ~XTBDlopen();

  XTBDlopen(const XTBDlopen &) = delete;
  XTBDlopen &operator=(const XTBDlopen &) = delete;

  [[nodiscard]] bool available() const noexcept { return m_pot != nullptr; }

  void forceImpl(const ForceInput &in, ForceOut *out) const;

  /// Convenience operator matching Potential interface shape.
  std::tuple<double, types::AtomMatrix, double>
  operator()(const types::AtomMatrix &positions,
             const std::vector<int> &atmtypes,
             const std::array<std::array<double, 3>, 3> &box) const;

private:
  void *m_lib{nullptr};
  RgpotXtbPot *m_pot{nullptr};
  using create_fn = RgpotXtbPot *(*)(const RgpotXtbConfig *, char *, size_t);
  using destroy_fn = void (*)(RgpotXtbPot *);
  using force_fn = int (*)(RgpotXtbPot *, long, const double *, const int *,
                           double *, double *, double *, const double *);
  create_fn m_create{nullptr};
  destroy_fn m_destroy{nullptr};
  force_fn m_force{nullptr};
};

} // namespace rgpot
