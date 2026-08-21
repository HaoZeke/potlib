#pragma once
// MIT License — linked UMA / OMol metatomic frontend (same stack as MetatomicPot)

#include "rgpot/MetatomicPot/MetatomicPot.hpp"
#include "rgpot/ParamHash.hpp"
#include "rgpot/Potential.hpp"
#include "rgpot/UmaPot/UmaConfig.hpp"

#include <cstdint>
#include <memory>

namespace rgpot {

/**
 * @brief UMA / OMol calculator on the metatomic C++ stack.
 *
 * Loads a metatomic TorchScript checkpoint with
 * ``metatomic_torch::load_atomistic_model``, builds vesin neighbor lists,
 * and takes autograd forces. Charge and spin are attached as System extra
 * data on every call (omol). This is the same in-process path as
 * MetatomicPot; it is not a Python helper.
 */
class UmaPot : public Potential<UmaPot> {
public:
  explicit UmaPot(const UmaConfig &config);
  ~UmaPot() override = default;

  UmaPot(const UmaPot &) = delete;
  UmaPot &operator=(const UmaPot &) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

  [[nodiscard]] PotCaps caps() const noexcept override {
    return {.reentrancy = Reentrancy::SharedInstance,
            .perImageInstances = true};
  }

  [[nodiscard]] const UmaConfig &config() const noexcept { return m_config; }

  void setChargeSpin(int charge, int spin);

  [[nodiscard]] uint64_t paramsKey() const noexcept override {
    return m_paramsKey;
  }

private:
  static constexpr uint64_t kKernelVersion = 1;

  UmaConfig m_config;
  uint64_t m_paramsKey{0};
  std::unique_ptr<MetatomicPot> m_inner;

  void recomputeParamsKey();
};

} // namespace rgpot
