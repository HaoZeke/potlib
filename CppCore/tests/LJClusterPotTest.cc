// MIT License
// Copyright 2023--present rgpot developers
//
// Free-boundary Lennard-Jones cluster checks: the eOn energy pin, agreement
// with the periodic LJPot on an isolated cluster, and cell independence.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <catch2/catch_all.hpp>

#include "rgpot/LennardJones/LJClusterPot.hpp"
#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using rgpot::types::AtomMatrix;

namespace {

/// eOn's neb_morse reactant geometry (13 hydrogens, client/unit_tests/data/
/// systems/neb_morse/reactant.con), inlined because rgpot reads no .con.
AtomMatrix nebMorseCluster() {
  return AtomMatrix{
      {50.594227, 52.017165, 52.277482}, {51.578540, 52.642148, 51.195222},
      {51.243758, 52.919086, 52.218383}, {50.490127, 52.736378, 51.417663},
      {51.522643, 50.884535, 51.656125}, {50.927263, 51.743087, 51.272643},
      {51.240676, 50.960436, 50.573042}, {51.275727, 52.061877, 50.284160},
      {50.434110, 50.976423, 51.879062}, {51.695007, 51.919472, 52.038680},
      {51.363596, 52.199101, 53.064923}, {52.017483, 51.639074, 51.008389},
      {51.187843, 51.165217, 52.678225}};
}

/// The cell that ships with the geometry above.
std::array<std::array<double, 3>, 3> nebMorseCell() {
  return {
      {{101.942400, 0.0, 0.0}, {0.0, 103.142600, 0.0}, {0.0, 0.0, 102.605500}}};
}

double maxForceNorm(const AtomMatrix &forces) {
  double worst = 0.0;
  for (size_t i = 0; i < forces.rows(); ++i) {
    const double n2 = forces(i, 0) * forces(i, 0) +
                      forces(i, 1) * forces(i, 1) + forces(i, 2) * forces(i, 2);
    worst = std::max(worst, std::sqrt(n2));
  }
  return worst;
}

} // namespace

TEST_CASE("LJClusterPot energy on the neb_morse cluster", "[LJClusterPot]") {
  rgpot::LJClusterPot pot;
  const auto positions = nebMorseCluster();
  const std::vector<int> atmtypes(13, 1);

  auto [energy, forces, variance] = pot(positions, atmtypes, nebMorseCell());
  (void)variance;

  // eOn reference (client/unit_tests/PotTest.cpp, "LJCluster energy matches
  // SVN"), carried over with its epsilon.
  REQUIRE_THAT(energy, WithinRel(-39.965379, 1e-4));
  // Tighter pin from an independent double-precision evaluation of this
  // kernel; eOn's LJ case pins the same maximum force to 1e-2.
  REQUIRE_THAT(energy, WithinRel(-39.9653513626179, 1e-9));
  REQUIRE_THAT(maxForceNorm(forces), WithinRel(0.00470419517301762, 1e-9));
}

TEST_CASE("LJClusterPot matches LJPot on an isolated cluster",
          "[LJClusterPot]") {
  // Same 12-6 kernel and cutoff shift; the cell is wide enough that the
  // minimum image convention leaves LJPot with the identical pair set.
  rgpot::LJClusterPot cluster;
  rgpot::LJPot periodic;
  const auto positions = nebMorseCluster();
  const std::vector<int> atmtypes(13, 1);
  const auto cell = nebMorseCell();

  auto [e_cluster, f_cluster, v_cluster] = cluster(positions, atmtypes, cell);
  auto [e_periodic, f_periodic, v_periodic] =
      periodic(positions, atmtypes, cell);
  (void)v_cluster;
  (void)v_periodic;

  REQUIRE_THAT(e_cluster, WithinRel(e_periodic, 1e-12));
  for (size_t i = 0; i < f_cluster.rows(); ++i) {
    for (size_t j = 0; j < f_cluster.cols(); ++j) {
      REQUIRE_THAT(f_cluster(i, j), WithinAbs(f_periodic(i, j), 1e-12));
    }
  }
}

TEST_CASE("LJClusterPot ignores the cell", "[LJClusterPot]") {
  // Free boundaries: a cell narrower than the cutoff must not fold any pair
  // vector, so the energy is unchanged.
  rgpot::LJClusterPot pot;
  const auto positions = nebMorseCluster();
  const std::vector<int> atmtypes(13, 1);
  const std::array<std::array<double, 3>, 3> tight{
      {{5.0, 0.0, 0.0}, {0.0, 5.0, 0.0}, {0.0, 0.0, 5.0}}};

  auto [e_wide, f_wide, v_wide] = pot(positions, atmtypes, nebMorseCell());
  auto [e_tight, f_tight, v_tight] = pot(positions, atmtypes, tight);
  (void)v_wide;
  (void)v_tight;

  REQUIRE_THAT(e_tight, WithinRel(e_wide, 1e-12));
  REQUIRE_FALSE(pot.caps().periodic);
}

TEST_CASE("LJClusterPot parameter fingerprint tracks the config",
          "[LJClusterPot]") {
  const rgpot::LJClusterPot defaults;
  const rgpot::LJClusterPot wider{rgpot::LJClusterConfig{.cutoff = 20.0}};
  const rgpot::LJClusterPot sameAsDefaults{rgpot::LJClusterConfig{}};

  REQUIRE(defaults.paramsKey() == sameAsDefaults.paramsKey());
  REQUIRE(defaults.paramsKey() != wider.paramsKey());
  REQUIRE(defaults.get_type() == rgpot::PotType::LJCluster);
  // Identical numbers hash identically; the result cache keeps the two
  // kernels apart through PotType, not through paramsKey.
  REQUIRE(defaults.paramsKey() == rgpot::LJPot{}.paramsKey());
}
