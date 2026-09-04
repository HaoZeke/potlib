// MIT License
// Copyright 2023--present rgpot developers
//
// ExprPot construct-time name checks and identity energy vs LJPot.

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_all.hpp>

#include "rgpot/ExprPot/ExprPot.hpp"
#include "rgpot/LennardJones/LJPot.hpp"
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

std::vector<rgpot::ExprPot::Term> oneLj(const char *name = "lj") {
  std::vector<rgpot::ExprPot::Term> terms;
  terms.emplace_back(name, std::make_unique<rgpot::LJPot>());
  return terms;
}

} // namespace

TEST_CASE("ExprPot identity energy matches LJPot on two atoms", "[expr]") {
  rgpot::LJPot lj;
  const auto positions = twoAtomPositions();
  const auto atmtypes = twoAtomTypes();
  const auto box = wideCell();

  auto [e_lj, f_lj, v_lj] = lj(positions, atmtypes, box);
  (void)f_lj;
  (void)v_lj;

  rgpot::ExprPot pot("lj", oneLj());
  REQUIRE(pot.get_type() == rgpot::PotType::Expr);
  REQUIRE(pot.expression() == "lj");

  auto [e_expr, f_expr, v_expr] = pot(positions, atmtypes, box);
  (void)f_expr;
  (void)v_expr;

  REQUIRE_THAT(e_expr, WithinAbs(e_lj, 1e-14));
  REQUIRE_THAT(e_expr, WithinRel(e_lj, 1e-14));
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
  (void)f_lj;
  (void)v_lj;
  (void)f_expr;
  (void)v_expr;
  REQUIRE_THAT(e_expr, WithinAbs(e_lj, 1e-14));
}
