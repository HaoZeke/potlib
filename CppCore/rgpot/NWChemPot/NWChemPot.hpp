#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief NWChem potential under rgpot `PotentialConfig` user parameters.
 *
 * User path: Cap'n Proto `PotentialConfig` / `NWChemParams` only → copy fields
 * into embed `RgpotNWChemParams` → `rgpot_nwchem_set_params` / `energy_grad`.
 * No intermediate C++ config struct on the public configure/setParams surface.
 */

#include <string>

#include "rgpot/NWChemPot/nwchem_c_abi.h"
#include "rgpot/Potential.hpp"
#include "rgpot/rpc/Potentials.capnp.h"

namespace rgpot {

class NWChemPot : public Potential<NWChemPot> {
public:
  NWChemPot();
  /** Apply schema defaults, then optional Cap'n Proto NWChemParams. */
  explicit NWChemPot(const ::NWChemParams::Reader &params);
  ~NWChemPot() override;

  NWChemPot(const NWChemPot &) = delete;
  NWChemPot &operator=(const NWChemPot &) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

  /**
   * Apply NWChem arm payload (Cap'n Proto NWChemParams) → embed C ABI params.
   */
  bool setParams(const ::NWChemParams::Reader &params);

  /** Write last applied knobs back to Cap'n Proto. */
  void getParams(::NWChemParams::Builder out) const;

  /**
   * Apply full rgpot PotentialConfig; only succeeds for none or nwchem arms.
   */
  bool setPotentialConfig(const ::PotentialConfig::Reader &cfg,
                          std::string *message_out = nullptr);

  /** Last embed-boundary params (fixed buffers; not a user format). */
  const RgpotNWChemParams &abiParams() const { return abi_params_; }

  bool available() const;
  static bool probe_available();
  static bool abi_available();

private:
  struct Impl;
  Impl *impl_;
  /** Sticky last-applied options at embed boundary (Cap'n Proto → this POD). */
  RgpotNWChemParams abi_params_{};
};

} // namespace rgpot
