// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "rgpot/XTBPot/XTBPot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using rgpot::types::AtomMatrix;

// Water molecule geometry (Angstrom)
// O at origin, two H atoms
static const double water_pos[] = {
    0.00000000, 0.00000000,  0.11779000,  // O
    0.00000000, 0.75545000,  -0.47116000, // H
    0.00000000, -0.75545000, -0.47116000  // H
};
static const int water_atmnrs[] = {8, 1, 1};
static const double zero_box[9] = {100.0, 0.0, 0.0, 0.0,  100.0,
                                   0.0,   0.0, 0.0, 100.0};

TEST_CASE("XTBPot GFN2 water energy", "[xtb]") {
  rgpot::XTBPot pot;

  AtomMatrix positions(3, 3);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = water_pos[i * 3 + j];
  std::vector<int> atmtypes(water_atmnrs, water_atmnrs + 3);
  std::array<std::array<double, 3>, 3> box = {
      {{100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}};

  auto [energy, forces, variance] = pot(positions, atmtypes, box);
  (void)variance;

  // GFN2-xTB water energy is approximately -5.07 Hartree = -137.9 eV
  REQUIRE(energy < 0.0);
  REQUIRE_THAT(energy, Catch::Matchers::WithinAbs(-137.9, 1.0));

  // Forces should sum to approximately zero (translational invariance)
  double fx_sum = 0.0, fy_sum = 0.0, fz_sum = 0.0;
  for (size_t i = 0; i < 3; ++i) {
    fx_sum += forces(i, 0);
    fy_sum += forces(i, 1);
    fz_sum += forces(i, 2);
  }
  REQUIRE_THAT(fx_sum, Catch::Matchers::WithinAbs(0.0, 1e-6));
  REQUIRE_THAT(fy_sum, Catch::Matchers::WithinAbs(0.0, 1e-6));
  REQUIRE_THAT(fz_sum, Catch::Matchers::WithinAbs(0.0, 1e-6));
}

TEST_CASE("XTBPot GFNFF water energy", "[xtb]") {
  rgpot::XTBConfig cfg;
  cfg.method = rgpot::GFNMethod::GFNFF;
  rgpot::XTBPot pot(cfg);

  AtomMatrix positions(3, 3);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = water_pos[i * 3 + j];
  std::vector<int> atmtypes(water_atmnrs, water_atmnrs + 3);
  std::array<std::array<double, 3>, 3> box = {
      {{100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}};

  auto [energy, forces, variance] = pot(positions, atmtypes, box);
  (void)variance;

  // GFNFF energy should be negative for a bound molecule
  REQUIRE(energy < 0.0);
}

#include "rgpot/XTBPot/XTBDlopen.hpp"

#include <cstdlib>
#include <string>

static void fill_water(AtomMatrix &positions, std::vector<int> &atmtypes,
                       std::array<std::array<double, 3>, 3> &box) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = water_pos[i * 3 + j];
  atmtypes.assign(water_atmnrs, water_atmnrs + 3);
  box = {{{100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}};
}

TEST_CASE("XTBPot linked force shape and finite energy", "[xtb][linked]") {
  rgpot::XTBPot pot;
  AtomMatrix positions(3, 3);
  std::vector<int> atmtypes;
  std::array<std::array<double, 3>, 3> box{};
  fill_water(positions, atmtypes, box);

  auto [energy, forces, variance] = pot(positions, atmtypes, box);
  (void)variance;
  REQUIRE(std::isfinite(energy));
  REQUIRE(forces.rows() == 3);
  REQUIRE(forces.cols() == 3);
  for (size_t i = 0; i < 3; ++i)
    for (size_t j = 0; j < 3; ++j)
      REQUIRE(std::isfinite(forces(i, j)));
}

TEST_CASE("XTBDlopen matches linked GFN2 water", "[xtb][dlopen]") {
  // Prefer engine from env (set by meson test); else try default name.
  const char *eng = std::getenv("RGPOT_XTB_ENGINE");
  if (!eng || !*eng) {
    WARN("RGPOT_XTB_ENGINE unset; attempting bare libxtb_engine.so");
  }

  rgpot::XTBConfig cfg;
  cfg.method = rgpot::GFNMethod::GFN2xTB;
  rgpot::XTBPot linked(cfg);

  rgpot::XTBDlopenConfig dcfg;
  dcfg.xtb = cfg;
  if (eng && *eng)
    dcfg.engine_path = eng;

  std::unique_ptr<rgpot::XTBDlopen> dl;
  try {
    dl = std::make_unique<rgpot::XTBDlopen>(dcfg);
  } catch (const std::exception &ex) {
    FAIL("XTBDlopen construct failed: " << ex.what());
  }
  REQUIRE(dl->available());

  AtomMatrix positions(3, 3);
  std::vector<int> atmtypes;
  std::array<std::array<double, 3>, 3> box{};
  fill_water(positions, atmtypes, box);

  auto [e_l, f_l, v_l] = linked(positions, atmtypes, box);
  auto [e_d, f_d, v_d] = (*dl)(positions, atmtypes, box);
  (void)v_l;
  (void)v_d;

  REQUIRE(std::isfinite(e_l));
  REQUIRE(std::isfinite(e_d));
  // Same GFN2 singlepoint implementation: very tight energy agreement.
  REQUIRE_THAT(e_d, Catch::Matchers::WithinAbs(e_l, 1e-8));
  REQUIRE(f_d.rows() == 3);
  REQUIRE(f_d.cols() == 3);
  for (size_t i = 0; i < 3; ++i) {
    for (size_t j = 0; j < 3; ++j) {
      REQUIRE_THAT(f_d(i, j), Catch::Matchers::WithinAbs(f_l(i, j), 1e-6));
    }
  }

  // Warm update path (second call) must stay consistent.
  auto [e_l2, f_l2, v_l2] = linked(positions, atmtypes, box);
  auto [e_d2, f_d2, v_d2] = (*dl)(positions, atmtypes, box);
  (void)f_l2;
  (void)f_d2;
  (void)v_l2;
  (void)v_d2;
  REQUIRE_THAT(e_d2, Catch::Matchers::WithinAbs(e_l2, 1e-8));
}
