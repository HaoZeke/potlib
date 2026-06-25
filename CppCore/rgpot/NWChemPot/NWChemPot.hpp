#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief NWChem potential under rgpot `PotentialConfig` user parameters.
 *
 * **rgpot params:** Cap'n Proto `PotentialConfig` (union: none | nwchem | … later
 * metatomic/xtb). NWChem uses the `nwchem` arm (`NWChemParams` payload only).
 * Apply via `setPotentialConfig` / RPC `configure`; typed `setParams`/`getParams`
 * for the nwchem payload when the arm is already selected.
 *
 * **Internal only:** `NWChemConfig` / embed `RgpotNWChemParams` are not user formats.
 */

#include <string>

#include "rgpot/NWChemPot/nwchem_c_abi.h"
#include "rgpot/Potential.hpp"
#include "rgpot/rpc/Potentials.capnp.h"

namespace rgpot {

/**
 * @brief Internal C++ mirror of Cap'n Proto NWChemParams (not a user format).
 * Populated only via adapt::capnp::nwchemConfigFromCapnp / toAbiParams for embed.
 */
struct NWChemConfig {
  std::string basis = "sto-3g";
  std::string theory = "scf";
  std::string scf_type = "rhf";
  int charge = 0;
  int multiplicity = 1;
  std::string engine_path;
  std::string nwchem_root;

  void toAbiParams(RgpotNWChemParams *out) const;
  static NWChemConfig fromAbiParams(const RgpotNWChemParams &p);
};

class NWChemPot : public Potential<NWChemPot> {
public:
  NWChemPot();
  /** Apply defaults then optional Cap'n Proto NWChemParams (user config). */
  explicit NWChemPot(const ::NWChemParams::Reader &params);
  /** @deprecated internal/tests: prefer NWChemParams::Reader constructor. */
  explicit NWChemPot(const NWChemConfig &config);
  ~NWChemPot() override;

  NWChemPot(const NWChemPot &) = delete;
  NWChemPot &operator=(const NWChemPot &) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

  /**
   * Apply NWChem arm of rgpot user params (Cap'n Proto NWChemParams).
   * Prefer applyPotentialConfig(PotentialConfig) at the generic layer; this is
   * the typed entry when the caller already has the nwchem struct.
   */
  bool setParams(const ::NWChemParams::Reader &params);

  /** Write current NWChem knobs back to Cap'n Proto (NWChemParams arm payload). */
  void getParams(::NWChemParams::Builder out) const;

  /**
   * Apply full rgpot PotentialConfig; only succeeds for none or nwchem arms
   * (other backends' arms rejected here — use the matching *Pot type later).
   */
  bool setPotentialConfig(const ::PotentialConfig::Reader &cfg,
                          std::string *message_out = nullptr);

  /** Internal: apply already-mirrored config (tests / embed only). */
  bool setConfig(const NWChemConfig &config);

  const NWChemConfig &config() const { return config_; }

  bool available() const;
  static bool probe_available();
  static bool abi_available();

private:
  struct Impl;
  Impl *impl_;
  NWChemConfig config_;
};

} // namespace rgpot
