#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief Psi4 DFT (BLYP) potential via runtime-loaded pure C++ engine.
 *
 * No Python interpreter. Loads libpsi4 then libpsi4_engine (dlopen RTLD_GLOBAL)
 * and calls the stable C ABI in rgpot_psi4_abi.h.
 */

#include <string>

#include "rgpot/Potential.hpp"

namespace rgpot {

struct Psi4Config {
  std::string basis = "sto-3g";
  int charge = 0;
  int multiplicity = 1;
  /// Optional explicit path to libpsi4 (.so/.dylib); empty => probe candidates.
  std::string library_path;
  /// PSIDATADIR override (basis/*.gbs). Empty => env PSIDATADIR / RGPOT_PSI4_DATADIR.
  std::string data_dir;
};

class Psi4Pot : public Potential<Psi4Pot> {
public:
  Psi4Pot();
  explicit Psi4Pot(const Psi4Config &config);
  ~Psi4Pot() override;

  Psi4Pot(const Psi4Pot &) = delete;
  Psi4Pot &operator=(const Psi4Pot &) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

  /// True if this instance successfully loaded the engine.
  bool available() const;

  /// Static probe: try loading engine without constructing a full potential.
  static bool probe_available();

private:
  struct Impl;
  Impl *impl_;
  Psi4Config config_;
};

} // namespace rgpot
