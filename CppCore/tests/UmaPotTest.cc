// MIT License — UmaPot talks to uma_helper.py over line-JSON.

#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <catch2/catch_all.hpp>

#include "rgpot/UmaPot/UmaPot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using Catch::Matchers::WithinAbs;
using rgpot::types::AtomMatrix;

namespace {

#ifdef RGPOT_UMA_HELPER_SOURCE
const char *kHelper = RGPOT_UMA_HELPER_SOURCE;
#else
const char *kHelper = "";
#endif

rgpot::UmaConfig fakeConfig() {
  rgpot::UmaConfig cfg;
  cfg.model = "uma-s-1p1";
  cfg.task_name = "omol";
  cfg.helper_path = kHelper;
  // Force the in-tree helper into the harmonic well so this test never
  // downloads a checkpoint.
  ::setenv("RGPOT_UMA_FAKE", "1", 1);
  return cfg;
}

} // namespace

TEST_CASE("UmaPot fake helper matches the harmonic well", "[UmaPot]") {
  REQUIRE(kHelper[0] != '\0');
  rgpot::UmaPot pot(fakeConfig());
  REQUIRE(pot.backend() == "fake");

  const AtomMatrix positions{{1.0, 0.0, 0.0}, {0.0, 2.0, 0.0}};
  const std::vector<int> atmtypes{1, 6};
  const std::array<std::array<double, 3>, 3> box{
      {{20.0, 0.0, 0.0}, {0.0, 20.0, 0.0}, {0.0, 0.0, 20.0}}};

  auto [energy, forces, variance] = pot(positions, atmtypes, box);
  // E = 0.5 * (1 + 4) = 2.5; F = -r
  REQUIRE_THAT(energy, WithinAbs(2.5, 1e-12));
  REQUIRE_THAT(variance, WithinAbs(0.0, 0.0));
  REQUIRE_THAT(forces(0, 0), WithinAbs(-1.0, 1e-12));
  REQUIRE_THAT(forces(0, 1), WithinAbs(0.0, 1e-12));
  REQUIRE_THAT(forces(1, 1), WithinAbs(-2.0, 1e-12));
}

TEST_CASE("UmaPot paramsKey changes with charge and model", "[UmaPot]") {
  auto cfg = fakeConfig();
  rgpot::UmaPot a(cfg);
  const uint64_t k0 = a.paramsKey();
  a.setChargeSpin(-1, 1);
  REQUIRE(a.paramsKey() != k0);
  cfg.model = "uma-m-1p1";
  rgpot::UmaPot b(cfg);
  REQUIRE(b.paramsKey() != k0);
}

TEST_CASE("UmaPot missing helper throws", "[UmaPot]") {
  rgpot::UmaConfig cfg;
  cfg.helper_path = "/no/such/uma_helper.py";
  REQUIRE_THROWS_WITH(rgpot::UmaPot(cfg),
                      Catch::Matchers::ContainsSubstring("uma_helper"));
}
