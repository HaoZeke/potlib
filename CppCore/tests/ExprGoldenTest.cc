// MIT License
// Copyright 2023--present rgpot developers
//
// Golden masters for ExprPot algebra (rgpot-2wdt). Pins live under
// CppCore/tests/data/expr/. Fail closed if a named file is missing.
// Energy 1e-14 eV, forces 1e-12 eV/A against the independent sum.

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_all.hpp>

#include "npy_io.hpp"
#include "rgpot/ExprPot/ExprPot.hpp"
#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/Morse/MorsePot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

#ifdef RGPOT_HAS_DFTD3
#include "rgpot/D3Pot/D3Pot.hpp"
#endif

using Catch::Matchers::WithinAbs;
using rgpot::testio::load_npy;
using rgpot::testio::load_npz;
using rgpot::types::AtomMatrix;

namespace {

constexpr const char *kData = "CppCore/tests/data/expr";
constexpr double kEnergyTol = 1e-14;
constexpr double kForceTol = 1e-12;

const char *kRequired[] = {
    "CppCore/tests/data/expr/MANIFEST.json",
    "CppCore/tests/data/expr/geometry.npz",
    "CppCore/tests/data/expr/identity_energy.npy",
    "CppCore/tests/data/expr/identity_forces.npy",
    "CppCore/tests/data/expr/half_lj_plus_morse_energy.npy",
    "CppCore/tests/data/expr/half_lj_plus_morse_forces.npy",
    "CppCore/tests/data/expr/two_lj_minus_lj_energy.npy",
    "CppCore/tests/data/expr/two_lj_minus_lj_forces.npy",
    "CppCore/tests/data/expr/half_paren_lj_plus_morse_energy.npy",
    "CppCore/tests/data/expr/half_paren_lj_plus_morse_forces.npy",
};

#ifdef RGPOT_HAS_DFTD3
const char *kRequiredD3[] = {
    "CppCore/tests/data/expr/half_lj_plus_d3_energy.npy",
    "CppCore/tests/data/expr/half_lj_plus_d3_forces.npy",
};
#endif

void require_file(const std::string &path) {
  REQUIRE(std::filesystem::exists(path));
  REQUIRE(std::filesystem::file_size(path) > 0);
}

std::string slurp(const std::string &path) {
  require_file(path);
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

struct Geom {
  std::size_t nat = 0;
  AtomMatrix positions;
  std::vector<int> numbers;
  std::array<std::array<double, 3>, 3> box{};
};

Geom load_geom() {
  const std::string path = std::string(kData) + "/geometry.npz";
  require_file(path);
  auto arrays = load_npz(path);
  const auto &pos = arrays.at("positions");
  const auto &num = arrays.at("numbers");
  const auto &box = arrays.at("box");
  REQUIRE(pos.shape.size() == 2);
  REQUIRE(pos.shape[1] == 3);
  REQUIRE(num.shape.size() == 1);
  REQUIRE(num.shape[0] == pos.shape[0]);
  REQUIRE(box.shape.size() == 2);
  REQUIRE(box.shape[0] == 3);
  REQUIRE(box.shape[1] == 3);
  Geom g;
  g.nat = pos.shape[0];
  g.positions = AtomMatrix(g.nat, 3);
  g.numbers.resize(g.nat);
  for (std::size_t i = 0; i < g.nat; ++i) {
    g.numbers[i] = static_cast<int>(std::lround(num.data[i]));
    for (std::size_t j = 0; j < 3; ++j) {
      g.positions(i, j) = pos.data[3 * i + j];
    }
  }
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      g.box[i][j] = box.data[3 * i + j];
    }
  }
  return g;
}

struct Pin {
  double energy = 0.0;
  AtomMatrix forces;
};

Pin load_pin(const std::string &stem, std::size_t nat) {
  const std::string epath = std::string(kData) + "/" + stem + "_energy.npy";
  const std::string fpath = std::string(kData) + "/" + stem + "_forces.npy";
  require_file(epath);
  require_file(fpath);
  auto e = load_npy(epath);
  auto f = load_npy(fpath);
  REQUIRE(e.data.size() == 1);
  REQUIRE(f.shape.size() == 2);
  REQUIRE(f.shape[0] == nat);
  REQUIRE(f.shape[1] == 3);
  REQUIRE(f.data.size() == 3 * nat);
  Pin p;
  p.energy = e.data[0];
  p.forces = AtomMatrix(nat, 3);
  std::memcpy(p.forces.data(), f.data.data(), 3 * nat * sizeof(double));
  return p;
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

AtomMatrix scaleAdd(double wa, const AtomMatrix &a, double wb,
                    const AtomMatrix &b) {
  AtomMatrix out(a.rows(), a.cols());
  for (size_t i = 0; i < a.rows(); ++i) {
    for (size_t j = 0; j < a.cols(); ++j) {
      out(i, j) = wa * a(i, j) + wb * b(i, j);
    }
  }
  return out;
}

std::vector<rgpot::ExprPot::Term> oneLj() {
  std::vector<rgpot::ExprPot::Term> terms;
  terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
  return terms;
}

std::vector<rgpot::ExprPot::Term> ljAndMorse() {
  std::vector<rgpot::ExprPot::Term> terms;
  terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
  terms.emplace_back("morse", std::make_unique<rgpot::MorsePot>());
  return terms;
}

void checkVsPinAndRef(double e_expr, const AtomMatrix &f_expr, double e_ref,
                      const AtomMatrix &f_ref, const Pin &pin) {
  REQUIRE_THAT(e_expr, WithinAbs(e_ref, kEnergyTol));
  REQUIRE_THAT(e_expr, WithinAbs(pin.energy, kEnergyTol));
  REQUIRE_THAT(e_ref, WithinAbs(pin.energy, kEnergyTol));
  REQUIRE_THAT(maxAbsForceDiff(f_expr, f_ref), WithinAbs(0.0, kForceTol));
  REQUIRE_THAT(maxAbsForceDiff(f_expr, pin.forces), WithinAbs(0.0, kForceTol));
  REQUIRE_THAT(maxAbsForceDiff(f_ref, pin.forces), WithinAbs(0.0, kForceTol));
}

} // namespace

TEST_CASE("expr golden fixtures exist (fail closed)", "[expr][golden]") {
  for (const char *p : kRequired) {
    require_file(p);
  }
  const std::string man = slurp("CppCore/tests/data/expr/MANIFEST.json");
  REQUIRE(man.find("sha256") != std::string::npos);
  REQUIRE(man.find("identity_energy.npy") != std::string::npos);
  REQUIRE(man.find("half_lj_plus_morse_energy.npy") != std::string::npos);
  REQUIRE(man.find("two_lj_minus_lj_energy.npy") != std::string::npos);
  REQUIRE(man.find("half_paren_lj_plus_morse_energy.npy") != std::string::npos);
  REQUIRE(man.find("1e-14") != std::string::npos);
  REQUIRE(man.find("1e-12") != std::string::npos);
}

TEST_CASE("ExprPot construct-time rejects unknown duplicate empty",
          "[expr][golden]") {
  SECTION("unknown name") {
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj + morse", oneLj()),
                      std::invalid_argument);
  }
  SECTION("duplicate name") {
    std::vector<rgpot::ExprPot::Term> terms;
    terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
    terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj", std::move(terms)),
                      std::invalid_argument);
  }
  SECTION("empty terms") {
    REQUIRE_THROWS_AS(rgpot::ExprPot("lj", {}), std::invalid_argument);
  }
}

TEST_CASE("ExprPot identity lj matches LJPot pin", "[expr][golden]") {
  auto geom = load_geom();
  auto pin = load_pin("identity", geom.nat);
  rgpot::LJPot lj;
  rgpot::ExprPot pot("lj", oneLj());
  auto [e_lj, f_lj, v_lj] = lj(geom.positions, geom.numbers, geom.box);
  auto [e_expr, f_expr, v_expr] = pot(geom.positions, geom.numbers, geom.box);
  (void)v_lj;
  (void)v_expr;
  checkVsPinAndRef(e_expr, f_expr, e_lj, f_lj, pin);
}

TEST_CASE("ExprPot 0.5*lj + morse matches independent sum pin",
          "[expr][golden]") {
  auto geom = load_geom();
  auto pin = load_pin("half_lj_plus_morse", geom.nat);
  rgpot::LJPot lj;
  rgpot::MorsePot morse;
  rgpot::ExprPot pot("0.5*lj + morse", ljAndMorse());
  auto [e_lj, f_lj, v_lj] = lj(geom.positions, geom.numbers, geom.box);
  auto [e_m, f_m, v_m] = morse(geom.positions, geom.numbers, geom.box);
  auto [e_expr, f_expr, v_expr] = pot(geom.positions, geom.numbers, geom.box);
  (void)v_lj;
  (void)v_m;
  (void)v_expr;
  const AtomMatrix f_ref = scaleAdd(0.5, f_lj, 1.0, f_m);
  checkVsPinAndRef(e_expr, f_expr, 0.5 * e_lj + e_m, f_ref, pin);
}

TEST_CASE("ExprPot 2*lj - lj matches lj pin", "[expr][golden]") {
  auto geom = load_geom();
  auto pin = load_pin("two_lj_minus_lj", geom.nat);
  rgpot::LJPot lj;
  rgpot::ExprPot pot("2*lj - lj", oneLj());
  auto [e_lj, f_lj, v_lj] = lj(geom.positions, geom.numbers, geom.box);
  auto [e_expr, f_expr, v_expr] = pot(geom.positions, geom.numbers, geom.box);
  (void)v_lj;
  (void)v_expr;
  checkVsPinAndRef(e_expr, f_expr, e_lj, f_lj, pin);
}

TEST_CASE("ExprPot 0.5*(lj+morse) matches independent sum pin",
          "[expr][golden]") {
  auto geom = load_geom();
  auto pin = load_pin("half_paren_lj_plus_morse", geom.nat);
  rgpot::LJPot lj;
  rgpot::MorsePot morse;
  rgpot::ExprPot pot("0.5*(lj+morse)", ljAndMorse());
  auto [e_lj, f_lj, v_lj] = lj(geom.positions, geom.numbers, geom.box);
  auto [e_m, f_m, v_m] = morse(geom.positions, geom.numbers, geom.box);
  auto [e_expr, f_expr, v_expr] = pot(geom.positions, geom.numbers, geom.box);
  (void)v_lj;
  (void)v_m;
  (void)v_expr;
  const AtomMatrix f_ref = scaleAdd(0.5, f_lj, 0.5, f_m);
  checkVsPinAndRef(e_expr, f_expr, 0.5 * (e_lj + e_m), f_ref, pin);
}

#ifdef RGPOT_HAS_DFTD3
TEST_CASE("expr d3 golden fixtures exist (fail closed)", "[expr][golden][d3]") {
  for (const char *p : kRequiredD3) {
    require_file(p);
  }
  const std::string man = slurp("CppCore/tests/data/expr/MANIFEST.json");
  REQUIRE(man.find("half_lj_plus_d3_energy.npy") != std::string::npos);
}

TEST_CASE("ExprPot 0.5*lj + d3 matches independent D3Pot pin",
          "[expr][golden][d3]") {
  auto geom = load_geom();
  auto pin = load_pin("half_lj_plus_d3", geom.nat);
  rgpot::LJPot lj;
  rgpot::D3Pot d3;
  std::vector<rgpot::ExprPot::Term> terms;
  terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
  terms.emplace_back("d3", std::make_unique<rgpot::D3Pot>());
  rgpot::ExprPot pot("0.5*lj + d3", std::move(terms));
  auto [e_lj, f_lj, v_lj] = lj(geom.positions, geom.numbers, geom.box);
  auto [e_d3, f_d3, v_d3] = d3(geom.positions, geom.numbers, geom.box);
  auto [e_expr, f_expr, v_expr] = pot(geom.positions, geom.numbers, geom.box);
  (void)v_lj;
  (void)v_d3;
  (void)v_expr;
  const AtomMatrix f_ref = scaleAdd(0.5, f_lj, 1.0, f_d3);
  checkVsPinAndRef(e_expr, f_expr, 0.5 * e_lj + e_d3, f_ref, pin);
}
#endif
