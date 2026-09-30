// MIT License
// Copyright 2023--present rgpot developers
#include <catch2/catch_all.hpp>

#include <array>

#include "rgpot/CalculatorGroup.hpp"

// A process started without mpirun is one calculator of one rank, with
// or without MPI in the build. A second bind with another size is
// refused because the world is split once.
TEST_CASE("One process is one calculator", "[CalculatorGroup]") {
  const rgpot::CalculatorGroup g = rgpot::bindCalculators(1);
  REQUIRE(g.index == 0);
  REQUIRE(g.ranks == 1);
  REQUIRE(g.world_size == 1);
  REQUIRE(rgpot::calculatorCount() == 1);
  REQUIRE(rgpot::calculatorWorldSize() == 1);
#ifdef RGPOT_HAS_MPI
  REQUIRE(rgpot::calculatorsUseMpi() == 1);
#else
  REQUIRE(rgpot::calculatorsUseMpi() == 0);
#endif
  REQUIRE(rgpot::thisCalculator().index == 0);

  std::array<double, 4> buf{1.0, -2.0, 3.5, 0.25};
  const int shared = rgpot::shareFromCalculator(0, buf.data(), sizeof(buf));
#ifdef RGPOT_HAS_MPI
  REQUIRE(shared == 1);
#else
  REQUIRE(shared == 0);
#endif
  REQUIRE(buf == std::array<double, 4>{1.0, -2.0, 3.5, 0.25});
  REQUIRE(rgpot::shareFromCalculator(1, buf.data(), sizeof(buf)) == 0);

  REQUIRE(rgpot::bindCalculators(1).index == 0);
  REQUIRE(rgpot::bindCalculators(2).index == -1);
}
