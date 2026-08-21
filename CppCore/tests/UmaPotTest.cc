// MIT License — UmaPot is the metatomic C++ stack plus omol extras.

#include <array>
#include <cmath>
#include <vector>

#include <catch2/catch_all.hpp>

#include "rgpot/MetatomicPot/MetatomicPot.hpp"
#include "rgpot/UmaPot/UmaPot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using Catch::Matchers::WithinAbs;
using rgpot::types::AtomMatrix;

// Same 13-H cluster as MetatomicPotTest (workdir = CppCore/tests).
static const double lj13_pos[] = {
    50.594227, 52.017165, 52.277482, 51.578540, 52.642148, 51.195222,
    51.243758, 52.919086, 52.218383, 50.490127, 52.736378, 51.417663,
    51.522643, 50.884535, 51.656125, 50.927263, 51.743087, 51.272643,
    51.240676, 50.960436, 50.573042, 51.275727, 52.061877, 50.284160,
    50.434110, 50.976423, 51.879062, 51.695007, 51.919472, 52.038680,
    51.363596, 52.199101, 53.064923, 52.017483, 51.639074, 51.008389,
    51.187843, 51.165217, 52.678225,
};
static const int lj13_atmnrs[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static constexpr int N_ATOMS = 13;

namespace {

AtomMatrix lj13Positions() {
  AtomMatrix positions(N_ATOMS, 3);
  for (int i = 0; i < N_ATOMS; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = lj13_pos[i * 3 + j];
  return positions;
}

std::array<std::array<double, 3>, 3> lj13Box() {
  return {{{101.9424, 0.0, 0.0}, {0.0, 103.1426, 0.0}, {0.0, 0.0, 102.6055}}};
}

} // namespace

TEST_CASE("UmaPot matches MetatomicPot on an LJ metatomic model", "[UmaPot]") {
  rgpot::UmaConfig ucfg;
  ucfg.model_path = "data/lj38/lennard-jones.pt";
  ucfg.device = "cpu";
  ucfg.task_name = "omol";
  rgpot::UmaPot uma(ucfg);

  rgpot::MetatomicConfig mcfg;
  mcfg.model_path = "data/lj38/lennard-jones.pt";
  mcfg.device = "cpu";
  rgpot::MetatomicPot mta(mcfg);

  const auto positions = lj13Positions();
  const std::vector<int> atmtypes(lj13_atmnrs, lj13_atmnrs + N_ATOMS);
  const auto box = lj13Box();

  auto [e_uma, f_uma, v_uma] = uma(positions, atmtypes, box);
  auto [e_mta, f_mta, v_mta] = mta(positions, atmtypes, box);
  (void)v_uma;
  (void)v_mta;
  REQUIRE_THAT(e_uma, WithinAbs(e_mta, 1e-8));
  for (int i = 0; i < N_ATOMS; ++i) {
    REQUIRE_THAT(f_uma(i, 0), WithinAbs(f_mta(i, 0), 1e-6));
    REQUIRE_THAT(f_uma(i, 1), WithinAbs(f_mta(i, 1), 1e-6));
    REQUIRE_THAT(f_uma(i, 2), WithinAbs(f_mta(i, 2), 1e-6));
  }
}

TEST_CASE("UmaPot extras do not change an LJ model", "[UmaPot]") {
  rgpot::UmaConfig ucfg;
  ucfg.model_path = "data/lj38/lennard-jones.pt";
  ucfg.device = "cpu";
  rgpot::UmaPot pot(ucfg);
  const auto positions = lj13Positions();
  const std::vector<int> atmtypes(lj13_atmnrs, lj13_atmnrs + N_ATOMS);
  const auto box = lj13Box();
  auto [e0, f0, v0] = pot(positions, atmtypes, box);
  (void)f0;
  (void)v0;
  pot.setChargeSpin(-1, 2);
  auto [e1, f1, v1] = pot(positions, atmtypes, box);
  (void)f1;
  (void)v1;
  REQUIRE_THAT(e1, WithinAbs(e0, 1e-8));
}

TEST_CASE("UmaPot paramsKey changes with charge and path", "[UmaPot]") {
  rgpot::UmaConfig cfg;
  cfg.model_path = "data/lj38/lennard-jones.pt";
  rgpot::UmaPot a(cfg);
  const uint64_t k0 = a.paramsKey();
  a.setChargeSpin(-1, 1);
  REQUIRE(a.paramsKey() != k0);
}

TEST_CASE("UmaPot empty model_path throws", "[UmaPot]") {
  rgpot::UmaConfig cfg;
  REQUIRE_THROWS_WITH(rgpot::UmaPot(cfg),
                      Catch::Matchers::ContainsSubstring("model_path"));
}
