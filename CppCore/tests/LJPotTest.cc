// MIT License
// Copyright 2023--present rgpot developers
#include <catch2/catch_all.hpp>

#include <array>
#include <cmath>
#include <vector>

#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using rgpot::types::AtomMatrix;

namespace {

// Wide enough that the minimum image convention never wraps a dimer, and wider
// than the 15.0 default cutoff.
std::array<std::array<double, 3>, 3> wideBox() {
  return {{{100.0, 0, 0}, {0, 100.0, 0}, {0, 0, 100.0}}};
}

double dimerEnergy(rgpot::LJPot &pot, double separation) {
  AtomMatrix positions{{0.0, 0.0, 0.0}, {separation, 0.0, 0.0}};
  std::vector<int> atmtypes{0, 0};
  auto [energy, forces] = pot(positions, atmtypes, wideBox());
  return energy;
}

} // namespace

// The pair term is 4*u0*((psi/r)^12 - (psi/r)^6) - cuttOffU, so at r = psi the
// bracket vanishes and the whole energy is the shift. This pins cuttOffU on its
// own and fails loudly if it is ever left uninitialized.
TEST_CASE("LJPot shift equals the potential at the cutoff", "[LJPot]") {
  auto pot = rgpot::LJPot();
  REQUIRE_THAT(dimerEnergy(pot, 1.0),
               Catch::Matchers::WithinAbs(3.5116594996622389e-07, 1e-18));
}

TEST_CASE("LJPot dimer sits at -u0 plus the shift", "[LJPot]") {
  auto pot = rgpot::LJPot();
  REQUIRE_THAT(dimerEnergy(pot, std::pow(2.0, 1.0 / 6.0)),
               Catch::Matchers::WithinAbs(-0.99999964883405001, 1e-12));
}

// An uninitialized member reads whatever the allocation happened to hold, so
// two independently constructed potentials can disagree on the same input.
TEST_CASE("LJPot energies are reproducible across instances", "[LJPot]") {
  auto first = rgpot::LJPot();
  auto second = rgpot::LJPot();
  const double r = 1.3;
  REQUIRE(dimerEnergy(first, r) == dimerEnergy(second, r));
}

TEST_CASE("LJPot honours explicit parameters", "[LJPot]") {
  const double u0 = 2.5, rc = 8.0, psi = 1.4;
  auto pot = rgpot::LJPot(u0, rc, psi);
  const double shift =
      4 * u0 * (std::pow(psi / rc, 12) - std::pow(psi / rc, 6));
  REQUIRE_THAT(dimerEnergy(pot, psi),
               Catch::Matchers::WithinAbs(-shift, 1e-15));
  REQUIRE_THAT(dimerEnergy(pot, psi * std::pow(2.0, 1.0 / 6.0)),
               Catch::Matchers::WithinAbs(-u0 - shift, 1e-12));
}
