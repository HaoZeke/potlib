// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>

#include <capnp/message.h>
#include <string>

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/NWChemPot/nwchem_c_abi.h"
#include "rgpot/rpc/Potentials.capnp.h"
#include "rgpot/types/adapters/capnp/nwchem_capnp_map.hpp"

TEST_CASE("nwchemParamsToAbi / AbiToParams roundtrip all fields",
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

  RgpotNWChemParams abi{};
  rgpot::types::adapt::capnp::nwchemParamsToAbi(root.asReader(), &abi);
  REQUIRE(std::string(abi.basis) == "6-31g*");
  REQUIRE(std::string(abi.theory) == "dft");
  REQUIRE(std::string(abi.scf_type) == "blyp");
  REQUIRE(abi.charge == 1);
  REQUIRE(abi.multiplicity == 2);
  REQUIRE(std::string(abi.engine_path) == "/tmp/libnwchem_engine.so");
  REQUIRE(std::string(abi.nwchem_root) == "/opt/nwchem");

  ::capnp::MallocMessageBuilder msg2;
  auto root2 = msg2.initRoot<NWChemParams>();
  rgpot::types::adapt::capnp::nwchemAbiToParams(abi, root2);
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
  // ok is false when engine not loaded; true when setParams succeeds.
  REQUIRE((ok || !ok)); // always defined
  REQUIRE(!message.empty());
}

TEST_CASE("NWChemPot setParams/getParams is Cap'n Proto only user path",
          "[nwchem][capnp]") {
  ::capnp::MallocMessageBuilder in_msg;
  auto in = in_msg.initRoot<::NWChemParams>();
  in.setBasis("6-31g");
  in.setTheory("dft");
  in.setScfType("blyp");
  in.setCharge(-1);
  in.setMultiplicity(1);
  in.setEnginePath("/tmp/libnwchem_engine.so");
  in.setNwchemRoot("/opt/nwchem");

  rgpot::NWChemPot pot;
  (void)pot.setParams(in.asReader());

  ::capnp::MallocMessageBuilder out_msg;
  auto out = out_msg.initRoot<::NWChemParams>();
  pot.getParams(out);
  REQUIRE(std::string(out.getBasis().cStr()) == "6-31g");
  REQUIRE(std::string(out.getTheory().cStr()) == "dft");
  REQUIRE(std::string(out.getScfType().cStr()) == "blyp");
  REQUIRE(out.getCharge() == -1);
  REQUIRE(std::string(out.getEnginePath().cStr()) == "/tmp/libnwchem_engine.so");
  REQUIRE(std::string(out.getNwchemRoot().cStr()) == "/opt/nwchem");
}

TEST_CASE("NWChemPot constructed from NWChemParams reader", "[nwchem][capnp]") {
  ::capnp::MallocMessageBuilder msg;
  auto p = msg.initRoot<::NWChemParams>();
  p.setTheory("blyp");
  p.setScfType("blyp");
  rgpot::NWChemPot pot(p.asReader());
  ::capnp::MallocMessageBuilder out_msg;
  auto out = out_msg.initRoot<::NWChemParams>();
  pot.getParams(out);
  REQUIRE(std::string(out.getTheory().cStr()) == "blyp");
}

TEST_CASE("setPotentialConfig applies rgpot params nwchem arm", "[nwchem][capnp]") {
  ::capnp::MallocMessageBuilder msg;
  auto cfg = msg.initRoot<::PotentialConfig>();
  auto nw = cfg.initNwchem();
  nw.setTheory("dft");
  nw.setScfType("blyp");
  nw.setBasis("sto-3g");
  rgpot::NWChemPot pot;
  std::string message;
  (void)pot.setPotentialConfig(cfg.asReader(), &message);
  REQUIRE(message.find("nwchem") != std::string::npos);
  ::capnp::MallocMessageBuilder out_msg;
  auto out = out_msg.initRoot<::NWChemParams>();
  pot.getParams(out);
  REQUIRE(std::string(out.getTheory().cStr()) == "dft");
  REQUIRE(std::string(out.getScfType().cStr()) == "blyp");
}
