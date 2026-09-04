// MIT License
// Copyright 2023--present rgpot developers
//
// ExprPot construct-time name checks, identity energy/forces vs LJPot,
// and analytic Lepton chain-rule forces.

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_all.hpp>

#include "Lepton.h"

#include "rgpot/ExprPot/ExprPot.hpp"
#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/Morse/MorsePot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using rgpot::types::AtomMatrix;

namespace {

AtomMatrix twoAtomPositions() {
  return AtomMatrix{{0.0, 0.0, 0.0}, {1.5, 0.0, 0.0}};
}

std::vector<int> twoAtomTypes() { return {18, 18}; }

std::array<std::array<double, 3>, 3> wideCell() {
  return {{{40.0, 0.0, 0.0}, {0.0, 40.0, 0.0}, {0.0, 0.0, 40.0}}};
}

// Morse/LJ unit fixture shared with MorsePotTest and LJClusterPotTest.
AtomMatrix nebMorseCluster() {
  return AtomMatrix{
      {50.594227, 52.017165, 52.277482}, {51.578540, 52.642148, 51.195222},
      {51.243758, 52.919086, 52.218383}, {50.490127, 52.736378, 51.417663},
      {51.522643, 50.884535, 51.656125}, {50.927263, 51.743087, 51.272643},
      {51.240676, 50.960436, 50.573042}, {51.275727, 52.061877, 50.284160},
      {50.434110, 50.976423, 51.879062}, {51.695007, 51.919472, 52.038680},
      {51.363596, 52.199101, 53.064923}, {52.017483, 51.639074, 51.008389},
      {51.187843, 51.165217, 52.678225}};
}

std::array<std::array<double, 3>, 3> nebMorseCell() {
  return {
      {{101.942400, 0.0, 0.0}, {0.0, 103.142600, 0.0}, {0.0, 0.0, 102.605500}}};
}

std::vector<rgpot::ExprPot::Term> oneLj(const char *name = "lj") {
  std::vector<rgpot::ExprPot::Term> terms;
  terms.emplace_back(name, std::make_unique<rgpot::LJPot>());
  return terms;
}

std::vector<rgpot::ExprPot::Term> ljAndMorse() {
  std::vector<rgpot::ExprPot::Term> terms;
  terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
  terms.emplace_back("morse", std::make_unique<rgpot::MorsePot>());
  return terms;
}

double maxAbsForceDiff(const AtomMatrix &a, const AtomMatrix &b) {
  REQUIRE(a.rows() == b.rows());
  REQUIRE(a.cols() == b.cols());
  double worst = 0.0;
  for (size_t i = 0; i < a.rows(); ++i) {
    for (size_t j = 0; j < a.cols(); ++j) {
      worst = std::max(worst, std::abs(a(i, j) - b(i, j)));
    }
  }
  return worst;
}

AtomMatrix scaledSum(double wa, const AtomMatrix &a, double wb,
                     const AtomMatrix &b) {
  AtomMatrix out(a.rows(), a.cols());
  for (size_t i = 0; i < a.rows(); ++i) {
    for (size_t j = 0; j < a.cols(); ++j) {
      out(i, j) = wa * a(i, j) + wb * b(i, j);
    }
  }
  return out;
}

} // namespace

TEST_CASE("ExprPot identity energy matches LJPot on two atoms", "[expr]") {
  rgpot::LJPot lj;
  const auto positions = twoAtomPositions();
  const auto atmtypes = twoAtomTypes();
  const auto box = wideCell();

  auto [e_lj, f_lj, v_lj] = lj(positions, atmtypes, box);
  (void)v_lj;

  rgpot::ExprPot pot("lj", oneLj());
  REQUIRE(pot.get_type() == rgpot::PotType::Expr);
  REQUIRE(pot.expression() == "lj");

  auto [e_expr, f_expr, v_expr] = pot(positions, atmtypes, box);
  (void)v_expr;

  REQUIRE_THAT(e_expr, WithinAbs(e_lj, 1e-14));
  REQUIRE_THAT(e_expr, WithinRel(e_lj, 1e-14));
  REQUIRE_THAT(maxAbsForceDiff(f_expr, f_lj), WithinAbs(0.0, 1e-12));
}

TEST_CASE("ExprPot rejects bad names before any force call", "[expr]") {
  SECTION("empty expression") {
    REQUIRE_THROWS_AS(rgpot::ExprPot("", oneLj()), std::invalid_argument);
    REQUIRE_THROWS_AS(rgpot::ExprPot("   ", oneLj()), std::invalid_argument);
  }
  SECTION("zero terms") {
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj", {}), std::invalid_argument);
  }
  SECTION("duplicate name") {
    std::vector<rgpot::ExprPot::Term> terms;
    terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
    terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj", std::move(terms)),
                      std::invalid_argument);
  }
  SECTION("name is not a Lepton identifier") {
    REQUIRE_THROWS_AS(rgpot::ExprPot("x", oneLj("1lj")), std::invalid_argument);
    REQUIRE_THROWS_AS(rgpot::ExprPot("x", oneLj("lj-x")), std::invalid_argument);
    REQUIRE_THROWS_AS(rgpot::ExprPot("x", oneLj("lj.x")), std::invalid_argument);
  }
  SECTION("name missing from the expression") {
    std::vector<rgpot::ExprPot::Term> terms = oneLj("lj");
    terms.emplace_back("unused", std::make_unique<rgpot::LJPot>());
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj", std::move(terms)),
                      std::invalid_argument);
  }
  SECTION("identifier in the expression with no term") {
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj + morse", oneLj()),
                      std::invalid_argument);
  }
}

TEST_CASE("ExprPot nested identity still matches LJPot energy", "[expr]") {
  std::vector<rgpot::ExprPot::Term> inner_terms = oneLj();
  auto inner = std::make_unique<rgpot::ExprPot>("lj", std::move(inner_terms));
  std::vector<rgpot::ExprPot::Term> outer_terms;
  outer_terms.emplace_back("inner", std::move(inner));
  rgpot::ExprPot pot("inner", std::move(outer_terms));

  rgpot::LJPot lj;
  const auto positions = twoAtomPositions();
  const auto atmtypes = twoAtomTypes();
  const auto box = wideCell();
  auto [e_lj, f_lj, v_lj] = lj(positions, atmtypes, box);
  auto [e_expr, f_expr, v_expr] = pot(positions, atmtypes, box);
  (void)v_lj;
  (void)v_expr;
  REQUIRE_THAT(e_expr, WithinAbs(e_lj, 1e-14));
  REQUIRE_THAT(maxAbsForceDiff(f_expr, f_lj), WithinAbs(0.0, 1e-12));
}

TEST_CASE("ExprPot differentiate lj of half lj plus morse is exactly 0.5",
          "[expr]") {
  auto parsed = Lepton::Parser::parse("0.5*lj + morse");
  auto dfdlj = parsed.differentiate("lj").createCompiledExpression();
  REQUIRE(dfdlj.getVariables().empty());
  REQUIRE(dfdlj.evaluate() == 0.5);
}

TEST_CASE("ExprPot chain-rule forces match 0.5*lj + morse", "[expr]") {
  rgpot::LJPot lj;
  rgpot::MorsePot morse;

  const auto check = [&](const AtomMatrix &positions,
                         const std::vector<int> &atmtypes,
                         const std::array<std::array<double, 3>, 3> &box) {
    auto [e_lj, f_lj, v_lj] = lj(positions, atmtypes, box);
    auto [e_morse, f_morse, v_morse] = morse(positions, atmtypes, box);
    rgpot::ExprPot pot("0.5*lj + morse", ljAndMorse());
    auto [e_expr, f_expr, v_expr] = pot(positions, atmtypes, box);

    REQUIRE_THAT(e_expr, WithinAbs(0.5 * e_lj + e_morse, 1e-12));
    const AtomMatrix expected = scaledSum(0.5, f_lj, 1.0, f_morse);
    REQUIRE_THAT(maxAbsForceDiff(f_expr, expected), WithinAbs(0.0, 1e-12));
    REQUIRE(std::isfinite(v_lj));
    REQUIRE(std::isfinite(v_morse));
    REQUIRE_THAT(v_expr, WithinAbs(0.5 * v_lj + v_morse, 1e-12));
  };

  SECTION("two-atom unit fixture") {
    check(twoAtomPositions(), twoAtomTypes(), wideCell());
  }
  SECTION("neb_morse unit fixture") {
    check(nebMorseCluster(), std::vector<int>(13, 1), nebMorseCell());
  }
}

TEST_CASE("ExprPot 2*lj - lj forces match lj", "[expr]") {
  rgpot::LJPot lj;
  const auto check = [&](const AtomMatrix &positions,
                         const std::vector<int> &atmtypes,
                         const std::array<std::array<double, 3>, 3> &box) {
    auto [e_lj, f_lj, v_lj] = lj(positions, atmtypes, box);
    (void)v_lj;
    std::vector<rgpot::ExprPot::Term> terms;
    terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
    rgpot::ExprPot pot("2*lj - lj", std::move(terms));
    auto [e_expr, f_expr, v_expr] = pot(positions, atmtypes, box);
    (void)v_expr;
    REQUIRE_THAT(e_expr, WithinAbs(e_lj, 1e-12));
    REQUIRE_THAT(maxAbsForceDiff(f_expr, f_lj), WithinAbs(0.0, 1e-12));
  };

  SECTION("two-atom unit fixture") {
    check(twoAtomPositions(), twoAtomTypes(), wideCell());
  }
  SECTION("neb_morse unit fixture") {
    check(nebMorseCluster(), std::vector<int>(13, 1), nebMorseCell());
  }
}

TEST_CASE("ExprPot caps stay at least PerInstance and hash expression",
          "[expr]") {
  rgpot::ExprPot pot("0.5*lj + morse", ljAndMorse());
  const rgpot::PotCaps caps = pot.caps();
  REQUIRE(caps.reentrancy == rgpot::Reentrancy::PerInstance);
  REQUIRE_FALSE(caps.batched);
  REQUIRE(caps.periodic);

  rgpot::ExprPot same("0.5*lj + morse", ljAndMorse());
  rgpot::ExprPot other("lj + morse", ljAndMorse());
  std::vector<rgpot::ExprPot::Term> deep;
  deep.emplace_back("lj", std::make_unique<rgpot::LJPot>());
  deep.emplace_back("morse",
                    std::make_unique<rgpot::MorsePot>(
                        rgpot::MorseConfig{.De = 0.8}));
  rgpot::ExprPot deeper("0.5*lj + morse", std::move(deep));
  REQUIRE(pot.paramsKey() == same.paramsKey());
  REQUIRE(pot.paramsKey() != other.paramsKey());
  REQUIRE(pot.paramsKey() != deeper.paramsKey());
}
