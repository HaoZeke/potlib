// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <capnp/message.h>

#include <array>
#include <vector>

#include "rgpot/CPMDPot/CPMDPot.hpp"
#include "rgpot/rpc/Potentials.capnp.h"
#include "rgpot/types/AtomMatrix.hpp"
#include "rgpot/units.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("CPMDPot passes serialized CPMDParams to cpmdc engine",
          "[cpmd][abi]") {
  ::capnp::MallocMessageBuilder msg;
  auto p = msg.initRoot<::CPMDParams>();
  p.setFunctional("BLYP");
  p.setCutOffRy(70.0);
  p.setCharge(0);
  p.setMultiplicity(1);

  rgpot::CPMDPot pot(p.asReader());
  REQUIRE(pot.available());

  rgpot::types::AtomMatrix positions(1, 3);
  positions(0, 0) = 0.0;
  positions(0, 1) = 0.0;
  positions(0, 2) = 0.0;
  std::vector<int> atmtypes{8};
  std::array<std::array<double, 3>, 3> box = {
      {{20.0, 0.0, 0.0}, {0.0, 20.0, 0.0}, {0.0, 0.0, 20.0}}};

  auto [energy, forces] = pot(positions, atmtypes, box);

  REQUIRE_THAT(energy, WithinAbs(0.50 * rgpot::units::HARTREE_TO_EV, 1e-12));
  REQUIRE_THAT(forces(0, 0),
               WithinAbs(0.004 * rgpot::units::NEG_GRAD_TO_FORCE, 1e-12));
  REQUIRE_THAT(forces(0, 1),
               WithinAbs(0.005 * rgpot::units::NEG_GRAD_TO_FORCE, 1e-12));
  REQUIRE_THAT(forces(0, 2),
               WithinAbs(0.006 * rgpot::units::NEG_GRAD_TO_FORCE, 1e-12));
}
