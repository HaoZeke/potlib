// MIT License
// Copyright 2023--present rgpot developers
//
// s-dftd3 ISO C API smoke (dftd3.h). Golden pins live on rgpot-0r9w.
// This suite checks that D3Pot evaluates water, that the explicit ATM
// flag changes the energy, and that water-octamer forces match a live
// s-dftd3 C-API gradient (sigma buffer required) at sqrt(eps) Hartree/Bohr.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <vector>

#include "rgpot/D3Pot/D3Pot.hpp"
#include "rgpot/types/AtomMatrix.hpp"
#include "rgpot/units.hpp"

using Catch::Matchers::WithinAbs;
using rgpot::types::AtomMatrix;

// Water (Å): O + 2H
static const double kWaterPos[] = {
    0.00000000, 0.00000000,  0.11779000,  // O
    0.00000000, 0.75545000,  -0.47116000, // H
    0.00000000, -0.75545000, -0.47116000  // H
};
static const int kWaterZ[] = {8, 1, 1};

static void fill_water(AtomMatrix &positions, std::vector<int> &atmtypes,
                       std::array<std::array<double, 3>, 3> &box) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      positions(static_cast<size_t>(i), static_cast<size_t>(j)) =
          kWaterPos[i * 3 + j];
  atmtypes.assign(kWaterZ, kWaterZ + 3);
  box = {{{100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}};
}

// (H2O)8 cube: same monomer + half-side 1.4 A as regen_dftd_goldens.py.
static void fill_water_octamer(AtomMatrix &positions,
                               std::vector<int> &atmtypes,
                               std::array<std::array<double, 3>, 3> &box) {
  constexpr double half = 1.4;
  constexpr double signs[2] = {-half, half};
  positions = AtomMatrix(24, 3);
  atmtypes.clear();
  atmtypes.reserve(24);
  size_t row = 0;
  for (double sx : signs) {
    for (double sy : signs) {
      for (double sz : signs) {
        for (int a = 0; a < 3; ++a) {
          positions(row, 0) = kWaterPos[a * 3 + 0] + sx;
          positions(row, 1) = kWaterPos[a * 3 + 1] + sy;
          positions(row, 2) = kWaterPos[a * 3 + 2] + sz;
          atmtypes.push_back(kWaterZ[a]);
          ++row;
        }
      }
    }
  }
  box = {{{100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}};
}

// Live s-dftd3 C-API gradient (Hartree/Bohr). Passes sigma so the library
// fills the gradient buffer; this is the same call D3Pot must make.
static void capi_bj_pbe_grad(const AtomMatrix &positions,
                             const std::vector<int> &atmtypes,
                             const std::array<std::array<double, 3>, 3> &box,
                             bool atm, double *energy_ha,
                             std::vector<double> *grad) {
  const int nat = static_cast<int>(positions.rows());
  REQUIRE(nat > 0);
  REQUIRE(atmtypes.size() == static_cast<size_t>(nat));
  std::vector<double> pos_bohr(static_cast<size_t>(3 * nat));
  for (int i = 0; i < nat; ++i) {
    for (int j = 0; j < 3; ++j) {
      pos_bohr[static_cast<size_t>(3 * i + j)] =
          positions(static_cast<size_t>(i), static_cast<size_t>(j)) *
          rgpot::units::ANGSTROM_TO_BOHR;
    }
  }
  double box_bohr[9];
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      box_bohr[3 * i + j] =
          box[static_cast<size_t>(i)][static_cast<size_t>(j)] *
          rgpot::units::ANGSTROM_TO_BOHR;
    }
  }
  const bool periodic[3] = {false, false, false};

  dftd3_error err = dftd3_new_error();
  REQUIRE(err != nullptr);
  char method[] = "pbe";
  dftd3_param param = dftd3_load_rational_damping(err, method, atm);
  REQUIRE(dftd3_check_error(err) == 0);
  REQUIRE(param != nullptr);
  dftd3_structure mol = dftd3_new_structure(
      err, nat, atmtypes.data(), pos_bohr.data(), box_bohr, periodic);
  REQUIRE(dftd3_check_error(err) == 0);
  REQUIRE(mol != nullptr);
  dftd3_model model = dftd3_new_d3_model(err, mol);
  REQUIRE(dftd3_check_error(err) == 0);
  REQUIRE(model != nullptr);

  *energy_ha = 0.0;
  grad->assign(static_cast<size_t>(3 * nat), 0.0);
  double sigma[9] = {};
  dftd3_get_dispersion(err, mol, model, param, energy_ha, grad->data(), sigma);
  REQUIRE(dftd3_check_error(err) == 0);

  dftd3_delete_param(&param);
  dftd3_delete_model(&model);
  dftd3_delete_structure(&mol);
  dftd3_delete_error(&err);
}

TEST_CASE("D3Pot default is BJ PBE with ATM on", "[d3][config]") {
  rgpot::D3Pot pot;
  REQUIRE(pot.get_type() == rgpot::PotType::D3);
  REQUIRE(pot.config().damping == rgpot::D3Damping::BJ);
  REQUIRE(pot.config().functional == "pbe");
  REQUIRE(pot.config().atm);
}

TEST_CASE("D3Pot water energy is finite and ATM flips it", "[d3][atm]") {
  AtomMatrix positions(3, 3);
  std::vector<int> atmtypes;
  std::array<std::array<double, 3>, 3> box{};
  fill_water(positions, atmtypes, box);

  rgpot::D3Config on;
  on.damping = rgpot::D3Damping::BJ;
  on.functional = "pbe";
  on.atm = true;
  rgpot::D3Config off = on;
  off.atm = false;

  rgpot::D3Pot pot_on(on);
  rgpot::D3Pot pot_off(off);
  auto [e_on, f_on, v_on] = pot_on(positions, atmtypes, box);
  auto [e_off, f_off, v_off] = pot_off(positions, atmtypes, box);
  (void)v_on;
  (void)v_off;

  REQUIRE(std::isfinite(e_on));
  REQUIRE(std::isfinite(e_off));
  REQUIRE(e_on != e_off);
  REQUIRE(std::abs(e_on - e_off) > 1e-12);

  REQUIRE(f_on.rows() == 3);
  REQUIRE(f_off.rows() == 3);
  double f2 = 0.0;
  for (size_t i = 0; i < 3; ++i)
    for (size_t j = 0; j < 3; ++j) {
      REQUIRE(std::isfinite(f_on(i, j)));
      REQUIRE(std::isfinite(f_off(i, j)));
      f2 += f_on(i, j) * f_on(i, j);
    }
  REQUIRE(f2 > 0.0);

  double fx = 0, fy = 0, fz = 0;
  for (size_t i = 0; i < 3; ++i) {
    fx += f_on(i, 0);
    fy += f_on(i, 1);
    fz += f_on(i, 2);
  }
  REQUIRE_THAT(fx, WithinAbs(0.0, 1e-8));
  REQUIRE_THAT(fy, WithinAbs(0.0, 1e-8));
  REQUIRE_THAT(fz, WithinAbs(0.0, 1e-8));
}

TEST_CASE("D3Pot zero damping water is finite", "[d3][zero]") {
  rgpot::D3Config cfg;
  cfg.damping = rgpot::D3Damping::Zero;
  cfg.functional = "pbe";
  cfg.atm = true;
  rgpot::D3Pot pot(cfg);

  AtomMatrix positions(3, 3);
  std::vector<int> atmtypes;
  std::array<std::array<double, 3>, 3> box{};
  fill_water(positions, atmtypes, box);

  auto [energy, forces, variance] = pot(positions, atmtypes, box);
  (void)variance;
  REQUIRE(std::isfinite(energy));
  REQUIRE(forces.rows() == 3);
}

TEST_CASE("D3Pot warm update_structure path is stable", "[d3][capi]") {
  rgpot::D3Pot pot;
  AtomMatrix positions(3, 3);
  std::vector<int> atmtypes;
  std::array<std::array<double, 3>, 3> box{};
  fill_water(positions, atmtypes, box);

  auto [e1, f1, v1] = pot(positions, atmtypes, box);
  positions(1, 1) += 0.01;
  auto [e2, f2, v2] = pot(positions, atmtypes, box);
  (void)f1;
  (void)f2;
  (void)v1;
  (void)v2;
  REQUIRE(std::isfinite(e1));
  REQUIRE(std::isfinite(e2));
  REQUIRE(e1 != e2);
}

TEST_CASE("D3Pot water-octamer forces match s-dftd3 C-API gradient",
          "[d3][capi][octamer]") {
  // s-dftd3 test/unit/test_dftd3.f90: thr2 = sqrt(epsilon). Converted to
  // eV/A with the same NEG_GRAD_TO_FORCE factor D3Pot applies.
  constexpr double kGradThrHa = 1.4901161193847656e-08;
  const double gtol = kGradThrHa * std::abs(rgpot::units::NEG_GRAD_TO_FORCE);

  AtomMatrix positions(24, 3);
  std::vector<int> atmtypes;
  std::array<std::array<double, 3>, 3> box{};
  fill_water_octamer(positions, atmtypes, box);
  REQUIRE(positions.rows() == 24);
  REQUIRE(atmtypes.size() == 24);

  rgpot::D3Config on;
  on.damping = rgpot::D3Damping::BJ;
  on.functional = "pbe";
  on.atm = true;
  rgpot::D3Config off = on;
  off.atm = false;

  rgpot::D3Pot pot_on(on);
  rgpot::D3Pot pot_off(off);
  auto [e_on, f_on, v_on] = pot_on(positions, atmtypes, box);
  auto [e_off, f_off, v_off] = pot_off(positions, atmtypes, box);
  (void)v_on;
  (void)v_off;
  (void)e_on;
  (void)e_off;

  REQUIRE(f_on.rows() == 24);
  REQUIRE(f_off.rows() == 24);
  REQUIRE(f_on.cols() == 3);
  REQUIRE(f_off.cols() == 3);

  double e_on_ha = 0.0;
  double e_off_ha = 0.0;
  std::vector<double> g_on;
  std::vector<double> g_off;
  capi_bj_pbe_grad(positions, atmtypes, box, true, &e_on_ha, &g_on);
  capi_bj_pbe_grad(positions, atmtypes, box, false, &e_off_ha, &g_off);
  REQUIRE(g_on.size() == 72);
  REQUIRE(g_off.size() == 72);

  double dg2 = 0.0;
  double f2_on = 0.0;
  for (size_t i = 0; i < 24; ++i) {
    for (size_t j = 0; j < 3; ++j) {
      const double f_ref_on = g_on[3 * i + j] * rgpot::units::NEG_GRAD_TO_FORCE;
      const double f_ref_off =
          g_off[3 * i + j] * rgpot::units::NEG_GRAD_TO_FORCE;
      REQUIRE_THAT(f_on(i, j), WithinAbs(f_ref_on, gtol));
      REQUIRE_THAT(f_off(i, j), WithinAbs(f_ref_off, gtol));
      const double d = f_on(i, j) - f_off(i, j);
      dg2 += d * d;
      f2_on += f_on(i, j) * f_on(i, j);
    }
  }
  REQUIRE(f2_on > 0.0);
  REQUIRE(dg2 > 0.0);
}
