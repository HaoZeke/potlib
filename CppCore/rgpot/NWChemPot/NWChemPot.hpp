#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief NWChem potential: user options via Cap'n Proto NWChemParams only.
 *
 * **User contract (in/out):** `NWChemParams` in Potentials.capnp — one schema for
 * RPC configure, Python clients, and in-process C++ (`setParams` / `getParams`).
 * No separate user TOML/JSON/YAML; no second public C++ option struct.
 *
 * **Internal only:** `NWChemConfig` mirrors Cap'n Proto while calling embed;
 * `RgpotNWChemParams` (nwchem_c_abi.h) is fixed buffers at the engine .so boundary.
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
   * Apply user options from Cap'n Proto NWChemParams (primary configure path,
   * in-process or via applyPotentialConfig / potserv.configure).
   */
  bool setParams(const ::NWChemParams::Reader &params);

  /** Write current options back to Cap'n Proto (symmetric get for users). */
  void getParams(::NWChemParams::Builder out) const;

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
