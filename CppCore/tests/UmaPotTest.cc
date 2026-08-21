// MIT License — UmaPot loads an AOTI .pt2, not a metatomic checkpoint.

#include <string>

#include <catch2/catch_all.hpp>

#include "rgpot/UmaPot/UmaPot.hpp"

TEST_CASE("UmaPot empty model_path throws", "[UmaPot]") {
  rgpot::UmaConfig cfg;
  REQUIRE_THROWS_WITH(rgpot::UmaPot(cfg),
                      Catch::Matchers::ContainsSubstring("model_path"));
}

TEST_CASE("UmaPot rejects a metatomic checkpoint", "[UmaPot]") {
  rgpot::UmaConfig cfg;
  cfg.model_path = "data/lj38/lennard-jones.pt";
  REQUIRE_THROWS_WITH(rgpot::UmaPot(cfg),
                      Catch::Matchers::ContainsSubstring(".pt2"));
}

TEST_CASE("UmaPot paramsKey changes with charge and path", "[UmaPot]") {
  rgpot::UmaConfig cfg;
  cfg.model_path = "no-such-uma.pt2";
  rgpot::UmaPot a(cfg);
  const uint64_t k0 = a.paramsKey();
  a.setChargeSpin(-1, 1);
  REQUIRE(a.paramsKey() != k0);
  cfg.model_path = "other-uma.pt2";
  rgpot::UmaPot b(cfg);
  REQUIRE(b.paramsKey() != k0);
}
