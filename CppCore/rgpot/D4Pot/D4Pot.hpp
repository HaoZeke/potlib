#pragma once
// MIT License
// Copyright 2023--present rgpot developers

#include <string>
#include <vector>

#include <dftd4.h>

#include "rgpot/Potential.hpp"

namespace rgpot {

/**
 * Linked dftd4 pot (NEEDED libdftd4).
 *
 * ``functional`` is the dftd4 method key for damping parameters (for
 * example ``"pbe"``). ``charge`` is the molecular charge passed to
 * ``dftd4_new_structure``; the library owns D4 charge scaling.
 * ``atm`` is dftd4's many-body (mdb) flag on
 * ``dftd4_load_rational_damping`` (ATM E(3); default on).
 */
struct D4Config {
  std::string functional = "pbe";
  double charge = 0.0;
  bool atm = true; //!< dftd4 mdb / ATM E(3); default on.
};

/**
 * Grimme DFT-D4 dispersion via the dftd4 ISO C API (``dftd4.h``).
 *
 * Each instance owns ``dftd4_error`` / ``dftd4_structure`` / ``dftd4_model``
 * / ``dftd4_param`` handles. First force builds the structure and model;
 * later forces call ``dftd4_update_structure``. Do not share one instance
 * across threads.
 */
class D4Pot : public Potential<D4Pot> {
public:
  D4Pot();
  explicit D4Pot(const D4Config &config);
  ~D4Pot();

  D4Pot(const D4Pot &) = delete;
  D4Pot &operator=(const D4Pot &) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

  [[nodiscard]] PotCaps caps() const noexcept override {
    return {.reentrancy = Reentrancy::PerInstance};
  }

  [[nodiscard]] const D4Config &config() const noexcept { return m_config; }

private:
  D4Config m_config;

  mutable dftd4_error m_err = nullptr;
  mutable dftd4_structure m_mol = nullptr;
  mutable dftd4_model m_model = nullptr;
  mutable dftd4_param m_param = nullptr;
  mutable bool m_initialized = false;
  mutable int m_natoms = 0;
  mutable std::vector<double> m_pos_bohr;

  void initHandles();
  void loadParam() const;
  void throwIfError(const char *what) const;
};

} // namespace rgpot
