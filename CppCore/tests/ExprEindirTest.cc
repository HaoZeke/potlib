// MIT License
// Copyright 2023--present rgpot developers
//
// ExprPot wraps as one eindir objective: one handle, one
// rgpot_potential_calculate, no caller-side sum.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch_all.hpp>

#include "rgpot/ExprPot/ExprPot.hpp"
#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/Morse/MorsePot.hpp"
#include "rgpot/potential.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using Catch::Matchers::WithinAbs;
using rgpot::types::AtomMatrix;

namespace {

AtomMatrix twoAtomPositions() {
  return AtomMatrix{{0.0, 0.0, 0.0}, {1.5, 0.0, 0.0}};
}

std::vector<int> twoAtomTypes() { return {18, 18}; }

std::array<std::array<double, 3>, 3> wideCell() {
  return {{{40.0, 0.0, 0.0}, {0.0, 40.0, 0.0}, {0.0, 0.0, 40.0}}};
}

std::vector<rgpot::ExprPot::Term> ljAndMorse() {
  std::vector<rgpot::ExprPot::Term> terms;
  terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
  terms.emplace_back("morse", std::make_unique<rgpot::MorsePot>());
  return terms;
}

double maxAbsForceDiff(const AtomMatrix &a, const std::vector<double> &flat) {
  REQUIRE(flat.size() == a.rows() * a.cols());
  double worst = 0.0;
  for (size_t i = 0; i < a.rows(); ++i) {
    for (size_t j = 0; j < a.cols(); ++j) {
      worst = std::max(worst, std::abs(a(i, j) - flat[i * a.cols() + j]));
    }
  }
  return worst;
}

} // namespace

TEST_CASE("C ABI calculate on eindir-wrapped ExprPot 0.5*lj + morse",
          "[expr][eindir]") {
  rgpot::ExprPot pot("0.5*lj + morse", ljAndMorse());
  const auto positions = twoAtomPositions();
  const auto atmtypes = twoAtomTypes();
  const auto box = wideCell();

  auto [e_cpp, f_cpp, v_cpp] = pot(positions, atmtypes, box);
  (void)v_cpp;

  std::vector<double> pos(positions.data(),
                          positions.data() + positions.size());
  std::vector<int> atm = atmtypes;
  double flatBox[9];
  std::memcpy(flatBox, static_cast<const void *>(&box), sizeof(flatBox));

  auto handle = rgpot::PotentialHandle::from_impl(pot);
  REQUIRE(handle.raw() != nullptr);
  REQUIRE(handle.raw()->n_atoms == 0);

  rgpot::InputSpec input(pos.size() / 3, pos.data(), atm.data(), flatBox);
  rgpot::CalcResult result;
  const rgpot_status_t status = rgpot_potential_calculate(
      handle.raw(), &input.c_struct(), &result.c_struct());
  REQUIRE(status == RGPOT_SUCCESS);

  REQUIRE_THAT(result.energy(), WithinAbs(e_cpp, 1e-14));
  REQUIRE(result.has_forces());
  const std::vector<double> f_abi = result.forces_vec();
  REQUIRE_THAT(maxAbsForceDiff(f_cpp, f_abi), WithinAbs(0.0, 1e-12));
}

TEST_CASE("from_impl eindir context still one calculate for 0.5*lj + morse",
          "[expr][eindir]") {
  rgpot::ExprPot pot("0.5*lj + morse", ljAndMorse());
  const auto positions = twoAtomPositions();
  auto atmtypes = twoAtomTypes();
  const auto box = wideCell();

  auto [e_cpp, f_cpp, v_cpp] = pot(positions, atmtypes, box);
  (void)v_cpp;

  std::vector<double> pos(positions.data(),
                          positions.data() + positions.size());
  double flatBox[9];
  std::memcpy(flatBox, static_cast<const void *>(&box), sizeof(flatBox));

  std::vector<int32_t> atm32(atmtypes.begin(), atmtypes.end());
  auto handle = rgpot::PotentialHandle::from_impl(
      pot, pos.size() / 3, atm32.data(), flatBox);
  REQUIRE(handle.raw() != nullptr);
  REQUIRE(handle.raw()->n_atoms == pos.size() / 3);
  REQUIRE(handle.raw()->base.dim == 3 * handle.raw()->n_atoms);

  rgpot::InputSpec input(pos.size() / 3, pos.data(), atmtypes.data(), flatBox);
  rgpot::CalcResult result;
  const rgpot_status_t status = rgpot_potential_calculate(
      handle.raw(), &input.c_struct(), &result.c_struct());
  REQUIRE(status == RGPOT_SUCCESS);
  REQUIRE_THAT(result.energy(), WithinAbs(e_cpp, 1e-14));
  REQUIRE_THAT(maxAbsForceDiff(f_cpp, result.forces_vec()),
               WithinAbs(0.0, 1e-12));
}
