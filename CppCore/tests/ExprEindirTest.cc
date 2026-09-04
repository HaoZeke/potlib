// MIT License
// Copyright 2023--present rgpot developers
//
// ExprPot wraps through PotentialHandle::from_impl /
// rgpot_potential_new_eindir as one eindir objective. One
// rgpot_potential_calculate returns the 0.5*lj + morse combo.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_all.hpp>

#include "npy_io.hpp"
#include "rgpot/ExprPot/ExprPot.hpp"
#include "rgpot/ForceStructs.hpp"
#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/Morse/MorsePot.hpp"
#include "rgpot/potential.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using Catch::Matchers::WithinAbs;
using rgpot::testio::load_npy;
using rgpot::testio::load_npz;
using rgpot::types::AtomMatrix;

namespace {

constexpr const char *kData = "CppCore/tests/data/expr";
constexpr double kEnergyTol = 1e-14;
constexpr double kForceTol = 1e-12;

struct Geom {
  std::size_t nat = 0;
  AtomMatrix positions;
  std::vector<int> numbers;
  std::array<std::array<double, 3>, 3> box{};
  std::vector<double> pos_flat;
  std::array<double, 9> box_flat{};
};

void require_file(const std::string &path) {
  REQUIRE(std::filesystem::exists(path));
  REQUIRE(std::filesystem::file_size(path) > 0);
}

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
  g.pos_flat.resize(3 * g.nat);
  for (std::size_t i = 0; i < g.nat; ++i) {
    g.numbers[i] = static_cast<int>(std::lround(num.data[i]));
    for (std::size_t j = 0; j < 3; ++j) {
      const double v = pos.data[3 * i + j];
      g.positions(i, j) = v;
      g.pos_flat[3 * i + j] = v;
    }
  }
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      const double v = box.data[3 * i + j];
      g.box[i][j] = v;
      g.box_flat[3 * i + j] = v;
    }
  }
  return g;
}

std::vector<rgpot::ExprPot::Term> ljAndMorse() {
  std::vector<rgpot::ExprPot::Term> terms;
  terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
  terms.emplace_back("morse", std::make_unique<rgpot::MorsePot>());
  return terms;
}

double maxAbsForceDiff(const AtomMatrix &a, const std::vector<double> &flat) {
  REQUIRE(a.size() == flat.size());
  double worst = 0.0;
  for (size_t i = 0; i < a.rows(); ++i) {
    for (size_t j = 0; j < a.cols(); ++j) {
      worst = std::max(worst, std::abs(a(i, j) - flat[3 * i + j]));
    }
  }
  return worst;
}

void checkCombo(const Geom &geom, rgpot::PotentialHandle &handle,
                double e_cpp, const AtomMatrix &f_cpp, double e_pin,
                const AtomMatrix &f_pin) {
  REQUIRE(handle.raw() != nullptr);

  rgpot::InputSpec input(geom.nat, geom.pos_flat.data(), geom.numbers.data(),
                         geom.box_flat.data());
  rgpot_force_out_t output = rgpot_force_out_create();
  const rgpot_status_t status =
      rgpot_potential_calculate(handle.raw(), &input.c_struct(), &output);
  REQUIRE(status == RGPOT_SUCCESS);
  REQUIRE_THAT(output.energy, WithinAbs(e_cpp, kEnergyTol));
  REQUIRE_THAT(output.energy, WithinAbs(e_pin, kEnergyTol));
  REQUIRE(output.forces != nullptr);
  auto *tensor = &output.forces->dl_tensor;
  REQUIRE(tensor->ndim == 2);
  REQUIRE(tensor->shape[0] == static_cast<int64_t>(geom.nat));
  REQUIRE(tensor->shape[1] == 3);
  const auto *f_abi = static_cast<const double *>(tensor->data);
  std::vector<double> flat(f_abi, f_abi + 3 * geom.nat);
  REQUIRE_THAT(maxAbsForceDiff(f_cpp, flat), WithinAbs(0.0, kForceTol));
  REQUIRE_THAT(maxAbsForceDiff(f_pin, flat), WithinAbs(0.0, kForceTol));
  rgpot_tensor_free(output.forces);
}

} // namespace

TEST_CASE("ExprPot 0.5*lj + morse is one eindir calculate",
          "[expr][eindir]") {
  require_file("CppCore/tests/data/expr/half_lj_plus_morse_energy.npy");
  require_file("CppCore/tests/data/expr/half_lj_plus_morse_forces.npy");
  auto geom = load_geom();
  auto e_pin = load_npy(std::string(kData) + "/half_lj_plus_morse_energy.npy");
  auto f_pin_npy =
      load_npy(std::string(kData) + "/half_lj_plus_morse_forces.npy");
  REQUIRE(e_pin.data.size() == 1);
  REQUIRE(f_pin_npy.data.size() == 3 * geom.nat);
  AtomMatrix f_pin(geom.nat, 3);
  std::memcpy(f_pin.data(), f_pin_npy.data.data(),
              3 * geom.nat * sizeof(double));

  rgpot::ExprPot pot("0.5*lj + morse", ljAndMorse());
  auto [e_cpp, f_cpp, v_cpp] = pot(geom.positions, geom.numbers, geom.box);
  (void)v_cpp;

  SECTION("PotentialHandle::from_impl") {
    auto handle = rgpot::PotentialHandle::from_impl(pot);
    REQUIRE(handle.raw()->n_atoms == 0);
    checkCombo(geom, handle, e_cpp, f_cpp, e_pin.data[0], f_pin);
  }
  SECTION("PotentialHandle::from_impl_eindir") {
    auto handle = rgpot::PotentialHandle::from_impl_eindir(
        pot, geom.nat, geom.numbers.data(), geom.box_flat.data());
    REQUIRE(handle.raw()->n_atoms == geom.nat);
    checkCombo(geom, handle, e_cpp, f_cpp, e_pin.data[0], f_pin);
  }
  SECTION("rgpot_potential_new_eindir then one calculate") {
    auto handle = rgpot::PotentialHandle::from_impl_eindir(
        pot, geom.nat, geom.numbers.data(), geom.box_flat.data());
    rgpot::InputSpec input(geom.nat, geom.pos_flat.data(), geom.numbers.data(),
                           geom.box_flat.data());
    auto result = handle.calculate(input);
    REQUIRE_THAT(result.energy(), WithinAbs(e_cpp, kEnergyTol));
    REQUIRE_THAT(result.energy(), WithinAbs(e_pin.data[0], kEnergyTol));
    const auto flat = result.forces_vec();
    REQUIRE_THAT(maxAbsForceDiff(f_cpp, flat), WithinAbs(0.0, kForceTol));
    REQUIRE_THAT(maxAbsForceDiff(f_pin, flat), WithinAbs(0.0, kForceTol));
  }
}
