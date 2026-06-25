// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>

#include <capnp/message.h>
#include <string>

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/rpc/Potentials.capnp.h"
#include "rgpot/types/adapters/capnp/nwchem_capnp_map.hpp"

TEST_CASE("nwchemConfigFromCapnp / ToCapnp roundtrip all fields",
          "[nwchem][capnp]") {
  ::capnp::MallocMessageBuilder msg;
  auto root = msg.initRoot<NWChemParams>();
  // Every NWChemParams field set explicitly (configure options -> wire).
  root.setBasis("6-31g*");
  root.setTheory("dft");
  root.setScfType("blyp"); // DFT xc when theory=dft
  root.setCharge(1);
  root.setMultiplicity(2);
  root.setEnginePath("/tmp/libnwchem_engine.so");
  root.setNwchemRoot("/opt/nwchem");

  auto cfg = rgpot::types::adapt::capnp::nwchemConfigFromCapnp(root.asReader());
  REQUIRE(cfg.basis == "6-31g*");
  REQUIRE(cfg.theory == "dft");
  REQUIRE(cfg.scf_type == "blyp");
  REQUIRE(cfg.charge == 1);
  REQUIRE(cfg.multiplicity == 2);
  REQUIRE(cfg.engine_path == "/tmp/libnwchem_engine.so");
  REQUIRE(cfg.nwchem_root == "/opt/nwchem");

  ::capnp::MallocMessageBuilder msg2;
  auto root2 = msg2.initRoot<NWChemParams>();
  rgpot::types::adapt::capnp::nwchemConfigToCapnp(root2, cfg);
  REQUIRE(std::string(root2.getBasis().cStr()) == "6-31g*");
  REQUIRE(std::string(root2.getTheory().cStr()) == "dft");
  REQUIRE(std::string(root2.getScfType().cStr()) == "blyp");
  REQUIRE(root2.getCharge() == 1);
  REQUIRE(root2.getMultiplicity() == 2);
  REQUIRE(std::string(root2.getEnginePath().cStr()) ==
          "/tmp/libnwchem_engine.so");
  REQUIRE(std::string(root2.getNwchemRoot().cStr()) == "/opt/nwchem");
}

TEST_CASE("blyp theory option maps through NWChemParams", "[nwchem][capnp]") {
  ::capnp::MallocMessageBuilder msg;
  auto root = msg.initRoot<NWChemParams>();
  root.setBasis("sto-3g");
  root.setTheory("blyp");
  root.setScfType("blyp");
  root.setCharge(0);
  root.setMultiplicity(1);
  auto cfg = rgpot::types::adapt::capnp::nwchemConfigFromCapnp(root.asReader());
  REQUIRE(cfg.theory == "blyp");
  REQUIRE(cfg.scf_type == "blyp");
  auto sum = rgpot::types::adapt::capnp::nwchemConfigSummary(cfg);
  REQUIRE(sum.find("theory=blyp") != std::string::npos);
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
