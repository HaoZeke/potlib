#pragma once
// MIT License — fairchem UMA / OMol frontend (no torch C++ link)

#include "rgpot/ParamHash.hpp"
#include "rgpot/Potential.hpp"
#include "rgpot/UmaPot/UmaConfig.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace rgpot {

/**
 * @brief UMA / OMol calculator via a persistent fairchem Python helper.
 *
 * UMA lives in fairchem-core, not as a metatomic TorchScript .pt. This
 * frontend forks ``uma_helper.py`` once, keeps the predictor loaded, and
 * evaluates energy/forces over a line-JSON pipe. Tests inject a fake helper
 * through ``RGPOT_UMA_HELPER`` / ``UmaConfig::helper_path``.
 */
class UmaPot : public Potential<UmaPot> {
public:
  UmaPot() : UmaPot(UmaConfig{}) {}
  explicit UmaPot(const UmaConfig &config);
  ~UmaPot() override;

  UmaPot(const UmaPot &) = delete;
  UmaPot &operator=(const UmaPot &) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

  [[nodiscard]] PotCaps caps() const noexcept override {
    return {.reentrancy = Reentrancy::ProcessSerial, .periodic = true};
  }

  [[nodiscard]] const UmaConfig &config() const noexcept { return m_config; }

  void setChargeSpin(int charge, int spin);

  [[nodiscard]] uint64_t paramsKey() const noexcept override {
    return m_paramsKey;
  }

  [[nodiscard]] const std::string &backend() const noexcept {
    return m_backend;
  }

private:
  static constexpr uint64_t kKernelVersion = 1;

  struct Helper;
  UmaConfig m_config;
  uint64_t m_paramsKey{0};
  mutable std::string m_backend;
  mutable std::unique_ptr<Helper> m_helper;
  mutable std::mutex m_mutex;

  void recomputeParamsKey();
  void ensureHelper() const;
};

} // namespace rgpot
