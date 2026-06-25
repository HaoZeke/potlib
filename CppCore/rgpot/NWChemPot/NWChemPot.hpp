#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief NWChem potential via runtime-loaded stable C ABI (libnwchem_engine).
 *
 * Direct in-process path only: dlopen engine, pass RgpotNWChemParams through
 * rgpot_nwchem_set_params / rgpot_nwchem_energy_grad. No RPC/Cap'n Proto on
 * this path. NWChemConfig is the C++ mirror of RgpotNWChemParams.
 */

#include <string>

#include "rgpot/NWChemPot/nwchem_c_abi.h"
#include "rgpot/Potential.hpp"

namespace rgpot {

/** C++ mirror of RgpotNWChemParams (stable C ABI in nwchem_c_abi.h). */
struct NWChemConfig {
  std::string basis = "sto-3g";
  std::string theory = "scf";
  std::string scf_type = "rhf";
  int charge = 0;
  int multiplicity = 1;
  std::string engine_path;  ///< frontend: which .so to dlopen
  std::string nwchem_root;  ///< frontend: NWCHEM_TOP before engine calls

  /** Fill C ABI params (all option fields copied into fixed buffers). */
  void toAbiParams(RgpotNWChemParams *out) const;

  /** Read from C ABI params. */
  static NWChemConfig fromAbiParams(const RgpotNWChemParams &p);
};

class NWChemPot : public Potential<NWChemPot> {
public:
  NWChemPot();
  explicit NWChemPot(const NWChemConfig &config);
  ~NWChemPot() override;

  NWChemPot(const NWChemPot &) = delete;
  NWChemPot &operator=(const NWChemPot &) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

  /** Apply full config via C ABI rgpot_nwchem_set_params (after dlopen). */
  bool setConfig(const NWChemConfig &config);

  /** Last applied config (C++ view). */
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
