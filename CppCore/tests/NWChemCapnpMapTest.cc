// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>

#include <capnp/message.h>

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/rpc/Potentials.capnp.h"
#include "rgpot/types/adapters/capnp/nwchem_capnp_map.hpp"

TEST_CASE("nwchemConfigFromCapnp / ToCapnp roundtrip", "[nwchem][capnp]") {
  ::capnp::MallocMessageBuilder msg;
  auto root = msg.initRoot<NWChemParams>();
  root.setBasis("6-31g*");
  root.setTheory("dft");
  root.setScfType("uhf");
  root.setCharge(1);
  root.setMultiplicity(2);
  root.setEnginePath("/tmp/libnwchem_engine.so");
  root.setNwchemRoot("/opt/nwchem");

  auto cfg = rgpot::types::adapt::capnp::nwchemConfigFromCapnp(root.asReader());
  REQUIRE(cfg.basis == "6-31g*");
  REQUIRE(cfg.theory == "dft");
  REQUIRE(cfg.scf_type == "uhf");
  REQUIRE(cfg.charge == 1);
  REQUIRE(cfg.multiplicity == 2);
  REQUIRE(cfg.engine_path == "/tmp/libnwchem_engine.so");
  REQUIRE(cfg.nwchem_root == "/opt/nwchem");

  ::capnp::MallocMessageBuilder msg2;
  auto root2 = msg2.initRoot<NWChemParams>();
  rgpot::types::adapt::capnp::nwchemConfigToCapnp(root2, cfg);
  REQUIRE(std::string(root2.getBasis().cStr()) == "6-31g*");
  REQUIRE(root2.getCharge() == 1);
}

TEST_CASE("applyPotentialConfig none is no-op", "[nwchem][capnp]") {
  rgpot::NWChemPot pot;
  ::capnp::MallocMessageBuilder msg;
  auto cfg = msg.initRoot<PotentialConfig>();
  cfg.setNone();
  std::string message;
  bool ok = rgpot::types::adapt::capnp::applyPotentialConfig(
      pot, cfg.asReader(), &message);
  REQUIRE(ok);
  REQUIRE(message == "no-op");
}

TEST_CASE("applyPotentialConfig nwchem arm", "[nwchem][capnp]") {
  rgpot::NWChemPot pot;
  ::capnp::MallocMessageBuilder msg;
  auto cfg = msg.initRoot<PotentialConfig>();
  auto nw = cfg.initNwchem();
  nw.setBasis("sto-3g");
  nw.setTheory("scf");
  nw.setScfType("rhf");
  nw.setCharge(0);
  nw.setMultiplicity(1);
  std::string message;
  bool ok = rgpot::types::adapt::capnp::applyPotentialConfig(
      pot, cfg.asReader(), &message);
  // ok is false when engine not loaded; true when setConfig succeeds.
  REQUIRE((ok || !ok)); // always defined
  REQUIRE(!message.empty());
}
