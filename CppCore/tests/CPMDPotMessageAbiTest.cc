// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <capnp/message.h>

#include <array>
#include <cstdlib>
#include <vector>

#include "rgpot/CPMDPot/CPMDPot.hpp"
#include "rgpot/NWChemPot/DynLib.hpp"
#include "rgpot/rpc/Potentials.capnp.h"
#include "rgpot/types/AtomMatrix.hpp"
#include "rgpot/units.hpp"

using Catch::Matchers::WithinAbs;

namespace {

// The fake engine is one shared object in this process, so a second
// dlopen of the same path shares its globals with the one CPMDPot holds.
struct FakeEngineCounter {
  using CountFn = int (*)(void);
  rgpot::DynLib lib;
  CountFn count = nullptr;

  FakeEngineCounter() {
    const char *path = std::getenv("RGPOT_CPMD_ENGINE");
    REQUIRE(path != nullptr);
    lib.open(path);
    count = lib.sym<CountFn>("cpmdc_fake_session_create_count");
  }

  int sessions() const { return count(); }
};

} // namespace

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
      {{20.0, 0.0, 0.0}, {0.0, 21.0, 0.0}, {0.0, 0.0, 23.0}}};

  auto [energy, forces, variance] = pot(positions, atmtypes, box);
  (void)variance;

  REQUIRE_THAT(energy, WithinAbs(0.773, 1e-12));
  REQUIRE_THAT(forces(0, 0), WithinAbs(0.011, 1e-12));
  REQUIRE_THAT(forces(0, 1), WithinAbs(0.012, 1e-12));
  REQUIRE_THAT(forces(0, 2), WithinAbs(0.013, 1e-12));
}

// A session carries the engine's converged wavefunction. Identical
// params must keep the live session; changed params must replace it.
TEST_CASE("CPMDPot keeps its engine session across identical setParams",
          "[cpmd][abi]") {
  FakeEngineCounter counter;

  ::capnp::MallocMessageBuilder msg;
  auto p = msg.initRoot<::CPMDParams>();
  p.setFunctional("BLYP");
  p.setCutOffRy(70.0);
  p.setCharge(0);
  p.setMultiplicity(1);

  const int before = counter.sessions();
  rgpot::CPMDPot pot(p.asReader());
  REQUIRE(pot.available());
  REQUIRE(counter.sessions() == before + 1);

  REQUIRE(pot.setParams(p.asReader()));
  REQUIRE(pot.setParams(p.asReader()));
  REQUIRE(pot.available());
  REQUIRE(counter.sessions() == before + 1);

  ::capnp::MallocMessageBuilder cfg_msg;
  auto cfg = cfg_msg.initRoot<::PotentialConfig>();
  auto cp = cfg.initCpmd();
  cp.setFunctional("BLYP");
  cp.setCutOffRy(70.0);
  cp.setCharge(0);
  cp.setMultiplicity(1);
  REQUIRE(pot.setPotentialConfig(cfg.asReader()));
  REQUIRE(counter.sessions() == before + 1);

  p.setCutOffRy(80.0);
  REQUIRE(pot.setParams(p.asReader()));
  REQUIRE(pot.available());
  REQUIRE(counter.sessions() == before + 2);
}
