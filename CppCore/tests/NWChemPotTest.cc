// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <capnp/message.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/rpc/Potentials.capnp.h"
#include "rgpot/types/AtomMatrix.hpp"

using rgpot::types::AtomMatrix;

// Water molecule geometry (Angstrom)
static const double water_pos[] = {
    0.00000000, 0.00000000,  0.11779000,  // O
    0.00000000, 0.75545000,  -0.47116000, // H
    0.00000000, -0.75545000, -0.47116000  // H
};
static const int water_atmnrs[] = {8, 1, 1};

TEST_CASE("NWChemPot probe_available without engine", "[nwchem]") {
  // Without an nwchemc-compatible library on path, probe is false.
  // This documents expected behavior; does not require NWChem installed.
  bool ok = rgpot::NWChemPot::probe_available();
  if (!ok) {
    SUCCEED("engine not on path (expected in default CI)");
  } else {
    SUCCEED("engine found via RGPOT_NWCHEM_ENGINE / library path");
  }
}

TEST_CASE("NWChemPot abi_available without real embed", "[nwchem]") {
  // Real embed only when the loaded library was built with RGPOT_HAS_NWCHEM.
  bool abi = rgpot::NWChemPot::abi_available();
  if (!abi) {
    SUCCEED("stub or missing engine reports abi_available=false");
  } else {
    SUCCEED("real NWChem embed reports abi_available=true");
  }
}

TEST_CASE("NWChemPot water energy when engine present", "[nwchem]") {
  if (!rgpot::NWChemPot::probe_available()) {
    SKIP("libnwchemc not available (set NWCHEMC_LIBRARY, RGPOT_NWCHEMC_ENGINE, or NWCHEM_TOP)");
  }

  ::capnp::MallocMessageBuilder msg;
  auto p = msg.initRoot<::NWChemParams>();
  p.setBasis("sto-3g");
  p.setTheory("scf");
  p.setScfType("rhf");
  p.setCharge(0);
  p.setMultiplicity(1);
  rgpot::NWChemPot pot(p.asReader());
  REQUIRE(pot.available());

  if (!rgpot::NWChemPot::abi_available()) {
    SKIP("engine loaded but stub-only (no RGPOT_HAS_NWCHEM embed)");
  }

  AtomMatrix positions(3, 3);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = water_pos[i * 3 + j];
  std::vector<int> atmtypes(water_atmnrs, water_atmnrs + 3);
  std::array<std::array<double, 3>, 3> box = {
      {{100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}};

  auto [energy, forces] = pot(positions, atmtypes, box);

  REQUIRE(std::isfinite(energy));
  // SCF/STO-3G water should be strongly bound (negative energy, eV scale)
  REQUIRE(energy < 0.0);

  for (size_t i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      REQUIRE(std::isfinite(forces(i, j)));
    }
  }
}

TEST_CASE("NWChemPot setParams updates Cap'n Proto-visible params", "[nwchem]") {
  rgpot::NWChemPot pot;
  ::capnp::MallocMessageBuilder msg;
  auto p = msg.initRoot<::NWChemParams>();
  p.setBasis("6-31g");
  p.setTheory("dft");
  p.setCharge(-1);
  p.setMultiplicity(1);
  p.setTask("energy");
  p.setTitle("anion");
  p.setMemoryMb(768);
  p.setScratchDir("/scratch/rgpot-nwchem-test");
  p.setPermanentDir("/perm/rgpot-nwchem-test");
  auto blocks = p.initInputBlocks(2);
  blocks.set(0, "dft; xc b3lyp; end");
  blocks.set(1, "set int:acc_std 1e-8");
  auto stanzas = p.initInputStanzas(1);
  auto stanza = stanzas[0];
  stanza.setKind(::NWChemInputStanza::Kind::DFT);
  auto dft = stanza.initDft();
  dft.setDirect(true);
  dft.setXc("pbe0");
  auto smear = dft.initSmearing();
  smear.setSigmaHartree(0.001);
  smear.setMode(::NWChemDftSmearing::Mode::FIXSZ);
  // setParams returns false if engine not loaded; stored params still roundtrip.
  (void)pot.setParams(p.asReader());
  ::capnp::MallocMessageBuilder out_msg;
  auto out = out_msg.initRoot<::NWChemParams>();
  pot.getParams(out);
  REQUIRE(std::string(out.getBasis().cStr()) == "6-31g");
  REQUIRE(std::string(out.getTheory().cStr()) == "dft");
  REQUIRE(out.getCharge() == -1);
  REQUIRE(std::string(out.getTask().cStr()) == "energy");
  REQUIRE(std::string(out.getTitle().cStr()) == "anion");
  REQUIRE(out.getMemoryMb() == 768);
  REQUIRE(std::string(out.getScratchDir().cStr()) ==
          "/scratch/rgpot-nwchem-test");
  REQUIRE(std::string(out.getPermanentDir().cStr()) ==
          "/perm/rgpot-nwchem-test");
  REQUIRE(out.getInputBlocks().size() == 2);
  REQUIRE(std::string(out.getInputBlocks()[0].cStr()) ==
          "dft; xc b3lyp; end");
  REQUIRE(std::string(out.getInputBlocks()[1].cStr()) ==
          "set int:acc_std 1e-8");
  REQUIRE(out.getInputStanzas().size() == 1);
  REQUIRE(out.getInputStanzas()[0].getKind() ==
          ::NWChemInputStanza::Kind::DFT);
  REQUIRE(out.getInputStanzas()[0].getDft().getDirect());
  REQUIRE(std::string(out.getInputStanzas()[0].getDft().getXc().cStr()) ==
          "pbe0");
  REQUIRE(out.getInputStanzas()[0]
              .getDft()
              .getSmearing()
              .getMode() == ::NWChemDftSmearing::Mode::FIXSZ);
  REQUIRE_THAT(out.getInputStanzas()[0]
                   .getDft()
                   .getSmearing()
                   .getSigmaHartree(),
               Catch::Matchers::WithinAbs(0.001, 1e-12));
  SUCCEED();
}
