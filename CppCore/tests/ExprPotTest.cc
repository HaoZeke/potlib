// MIT License
// Copyright 2023--present rgpot developers
//
// ExprPot energy compile + fail-closed names. Force goldens live on a
// later ticket; this suite only checks identity energy and construct-time
// rejects.

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include "rgpot/ExprPot/ExprPot.hpp"
#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/Morse/MorsePot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using Catch::Matchers::WithinAbs;
using rgpot::types::AtomMatrix;

namespace {

AtomMatrix twoAtom() { return AtomMatrix{{0.0, 0.0, 0.0}, {1.5, 0.0, 0.0}}; }

std::vector<int> twoHydrogen() { return {1, 1}; }

std::array<std::array<double, 3>, 3> wideCell() {
  return {{{40.0, 0.0, 0.0}, {0.0, 40.0, 0.0}, {0.0, 0.0, 40.0}}};
}

std::unique_ptr<rgpot::LJPot> makeLJ() {
  return std::make_unique<rgpot::LJPot>();
}

} // namespace

TEST_CASE("ExprPot type and identity energy match LJPot", "[expr][energy]") {
  rgpot::LJPot lj;
  const auto positions = twoAtom();
  const auto atmtypes = twoHydrogen();
  const auto box = wideCell();
  auto [ljEnergy, ljForces, ljVar] = lj(positions, atmtypes, box);
  (void)ljForces;
  (void)ljVar;

  std::vector<rgpot::ExprTerm> terms;
  terms.emplace_back("lj", makeLJ());
  rgpot::ExprPot expr("lj", std::move(terms));

  REQUIRE(expr.get_type() == rgpot::PotType::Expr);
  REQUIRE(expr.nTerms() == 1);
  REQUIRE(expr.expression() == "lj");
  REQUIRE(expr.caps().reentrancy == rgpot::Reentrancy::PerInstance);
  REQUIRE_FALSE(expr.caps().batched);

  auto [energy, forces, variance] = expr(positions, atmtypes, box);
  (void)variance;
  REQUIRE_THAT(energy, WithinAbs(ljEnergy, 1e-14));
  REQUIRE(forces.rows() == 2);
  REQUIRE_THAT(forces(0, 0), WithinAbs(0.0, 0.0));
}

TEST_CASE("ExprPot 0.5*lj energy is half of LJPot", "[expr][energy]") {
  rgpot::LJPot lj;
  const auto positions = twoAtom();
  const auto atmtypes = twoHydrogen();
  const auto box = wideCell();
  auto [ljEnergy, ljForces, ljVar] = lj(positions, atmtypes, box);
  (void)ljForces;
  (void)ljVar;

  std::vector<rgpot::ExprTerm> terms;
  terms.emplace_back("lj", makeLJ());
  rgpot::ExprPot expr("0.5*lj", std::move(terms));
  auto [energy, forces, variance] = expr(positions, atmtypes, box);
  (void)forces;
  (void)variance;
  REQUIRE_THAT(energy, WithinAbs(0.5 * ljEnergy, 1e-14));
}

TEST_CASE("ExprPot nested child is a Potential", "[expr][nested]") {
  const auto positions = twoAtom();
  const auto atmtypes = twoHydrogen();
  const auto box = wideCell();

  rgpot::LJPot lj;
  auto [ljEnergy, ljForces, ljVar] = lj(positions, atmtypes, box);
  (void)ljForces;
  (void)ljVar;

  std::vector<rgpot::ExprTerm> innerTerms;
  innerTerms.emplace_back("lj", makeLJ());
  auto inner = std::make_unique<rgpot::ExprPot>("lj", std::move(innerTerms));

  std::vector<rgpot::ExprTerm> outerTerms;
  outerTerms.emplace_back("inner", std::move(inner));
  rgpot::ExprPot outer("inner", std::move(outerTerms));

  auto [energy, forces, variance] = outer(positions, atmtypes, box);
  (void)forces;
  (void)variance;
  REQUIRE_THAT(energy, WithinAbs(ljEnergy, 1e-14));
}

TEST_CASE("ExprPot fail-closed names throw before any force call",
          "[expr][construct]") {
  SECTION("empty expression") {
    std::vector<rgpot::ExprTerm> terms;
    terms.emplace_back("lj", makeLJ());
    REQUIRE_THROWS_AS(rgpot::ExprPot("", std::move(terms)),
                      std::invalid_argument);
  }
  SECTION("zero terms") {
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj", {}), std::invalid_argument);
  }
  SECTION("duplicate name") {
    std::vector<rgpot::ExprTerm> terms;
    terms.emplace_back("lj", makeLJ());
    terms.emplace_back("lj", makeLJ());
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj", std::move(terms)),
                      std::invalid_argument);
  }
  SECTION("name is not a Lepton identifier") {
    std::vector<rgpot::ExprTerm> terms;
    terms.emplace_back("1lj", makeLJ());
    REQUIRE_THROWS_AS(rgpot::ExprPot("1lj", std::move(terms)),
                      std::invalid_argument);
  }
  SECTION("name missing from the expression") {
    std::vector<rgpot::ExprTerm> terms;
    terms.emplace_back("morse", std::make_unique<rgpot::MorsePot>());
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj", std::move(terms)),
                      std::invalid_argument);
  }
  SECTION("identifier in the expression with no term") {
    std::vector<rgpot::ExprTerm> terms;
    terms.emplace_back("lj", makeLJ());
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj + foo", std::move(terms)),
                      std::invalid_argument);
  }
}
