// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <vector>

#include "rgpot/Psi4Pot/Psi4Pot.hpp"
#include "rgpot/types/AtomMatrix.hpp"
#include "rgpot/units.hpp"

using rgpot::types::AtomMatrix;
using rgpot::units::HARTREE_TO_EV;

// Water molecule geometry (Angstrom) — standard test geometry
static const double water_pos[] = {
    0.00000000, 0.00000000,  0.11779000,  // O
    0.00000000, 0.75545000,  -0.47116000, // H
    0.00000000, -0.75545000, -0.47116000  // H
};
static const int water_atmnrs[] = {8, 1, 1};

// Reference: BLYP/STO-3G energy for this geometry (Hartree)
static constexpr double WATER_BLYP_STO3G_H = -75.27764712750371;

TEST_CASE("Psi4Pot BLYP/STO-3G water energy", "[psi4]") {
  if (!rgpot::Psi4Pot::probe_available()) {
    SKIP("libpsi4_engine / libpsi4 not available (set RGPOT_PSI4_SO, "
         "RGPOT_PSI4_ENGINE, PSIDATADIR)");
  }

  rgpot::Psi4Config cfg;
  cfg.basis = "sto-3g";
  cfg.charge = 0;
  cfg.multiplicity = 1;
  rgpot::Psi4Pot pot(cfg);
  REQUIRE(pot.available());

  AtomMatrix positions(3, 3);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = water_pos[i * 3 + j];
  std::vector<int> atmtypes(water_atmnrs, water_atmnrs + 3);
  std::array<std::array<double, 3>, 3> box = {
      {{100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}};

  auto [energy, forces] = pot(positions, atmtypes, box);

  const double expected_ev = WATER_BLYP_STO3G_H * HARTREE_TO_EV;
  // DFT grid / convergence tolerance: allow ~1e-4 Hartree in eV (~0.003 eV)
  REQUIRE_THAT(energy, Catch::Matchers::WithinAbs(expected_ev, 0.05));

  // Forces should be finite
  for (size_t i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      REQUIRE(std::isfinite(forces(i, j)));
    }
  }
}
