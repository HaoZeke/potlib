#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief NWChem quantum chemistry potential via runtime-loaded C ABI engine.
 *
 * Frontend always compiles. Loads libnwchem_engine (dlopen RTLD_GLOBAL) and
 * calls the stable C ABI in nwchem_c_abi.h. Without a real engine, methods
 * report unavailable (stub ABI returns abiAvailable=0).
 */

#include <string>

#include "rgpot/Potential.hpp"

namespace rgpot {

/**
 * @brief Native mirror of Cap'n Proto NWChemParams (rpc/Potentials.capnp).
 *
 * Field names differ only by snake_case; wire names use camelCase in schema.
 * Passed through rgpot::types::adapt::capnp::{nwchemConfigFromCapnp,ToCapnp}
 * and Potential.configure(PotentialConfig.nwchem).
 *
 * Engine compute path uses basis/theory/scf_type/charge/multiplicity via
 * rgpot_nwchem_set_config / rgpot_nwchem_energy_grad. engine_path/nwchem_root
 * control dlopen and embed environment only.
 */
struct NWChemConfig {
  std::string basis = "sto-3g";       ///< NWChemParams.basis
  std::string theory = "scf";         ///< NWChemParams.theory (scf|dft|blyp|...)
  std::string scf_type = "rhf";       ///< NWChemParams.scfType (rhf|uhf|blyp xc)
  int charge = 0;                     ///< NWChemParams.charge
  int multiplicity = 1;               ///< NWChemParams.multiplicity
  std::string engine_path;            ///< NWChemParams.enginePath -> RGPOT_NWCHEM_ENGINE
  std::string nwchem_root;            ///< NWChemParams.nwchemRoot -> NWCHEM_TOP
};

class NWChemPot : public Potential<NWChemPot> {
public:
  NWChemPot();
  explicit NWChemPot(const NWChemConfig &config);
  ~NWChemPot() override;

  NWChemPot(const NWChemPot &) = delete;
  NWChemPot &operator=(const NWChemPot &) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

  /// Apply config to the loaded engine (rgpot_nwchem_set_config).
  bool setConfig(const NWChemConfig &config);

  /// True if this instance successfully loaded the engine and ABI is live.
  bool available() const;

  /// Static probe: try loading engine without constructing a full potential.
  static bool probe_available();

  /// Engine reports real embed (rgpot_nwchem_abi_available), not stub-only.
  static bool abi_available();

private:
  struct Impl;
  Impl *impl_;
  NWChemConfig config_;
};

} // namespace rgpot
