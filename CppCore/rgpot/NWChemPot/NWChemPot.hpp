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

struct NWChemConfig {
  std::string basis = "sto-3g";
  std::string theory = "scf";
  std::string scf_type = "rhf";
  int charge = 0;
  int multiplicity = 1;
  /// Optional explicit path to libnwchem_engine; empty => probe candidates.
  std::string engine_path;
  /// Optional NWCHEM_TOP / install prefix hint for engine-side paths.
  std::string nwchem_root;
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
