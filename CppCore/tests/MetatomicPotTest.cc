// MIT License
// Copyright 2023--present rgpot developers

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <vector>

#include "rgpot/MetatomicPot/MetatomicPot.hpp"
#include "rgpot/MetatomicPot/vesin_compat.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using Catch::Matchers::WithinAbs;
using rgpot::types::AtomMatrix;

// 13 hydrogen atoms from eOn lj38 test (pos.con), positions in Angstrom
// Cell: 101.9424 x 103.1426 x 102.6055 (orthogonal, 90 deg)
// clang-format off
static const double lj13_pos[] = {
    50.594227, 52.017165, 52.277482,
    51.578540, 52.642148, 51.195222,
    51.243758, 52.919086, 52.218383,
    50.490127, 52.736378, 51.417663,
    51.522643, 50.884535, 51.656125,
    50.927263, 51.743087, 51.272643,
    51.240676, 50.960436, 50.573042,
    51.275727, 52.061877, 50.284160,
    50.434110, 50.976423, 51.879062,
    51.695007, 51.919472, 52.038680,
    51.363596, 52.199101, 53.064923,
    52.017483, 51.639074, 51.008389,
    51.187843, 51.165217, 52.678225,
};
// clang-format on
static const int lj13_atmnrs[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static constexpr int N_ATOMS = 13;

TEST_CASE("MetatomicPot LJ energy and forces", "[metatomic]") {
  rgpot::MetatomicConfig cfg;
  cfg.model_path = "data/lj38/lennard-jones.pt";
  cfg.device = "cpu";
  cfg.length_unit = "angstrom";
  rgpot::MetatomicPot pot(cfg);

  AtomMatrix positions(N_ATOMS, 3);
  for (int i = 0; i < N_ATOMS; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = lj13_pos[i * 3 + j];

  std::vector<int> atmtypes(lj13_atmnrs, lj13_atmnrs + N_ATOMS);
  std::array<std::array<double, 3>, 3> box = {
      {{101.9424, 0.0, 0.0}, {0.0, 103.1426, 0.0}, {0.0, 0.0, 102.6055}}};

  auto [energy, forces, variance] = pot(positions, atmtypes, box);
  (void)variance;

  // Reference values from eOn MetatomicTest (lennard-jones.pt model)
  double expected_energy = 98374.87753058573;
  REQUIRE_THAT(energy, WithinAbs(expected_energy, 1e-2));

  // clang-format off
  double expected_forces_flat[] = {
      -90017.67874663,  14048.63238980,  42667.56998833,
       51715.62564964,  81020.09231365, -33288.72879168,
       13893.56176940,  89793.76258628,  33937.11367155,
      -71755.76235155,  52324.79462503, -32402.45955531,
       46268.61994191, -89837.93451292,  11509.42733453,
      -67770.69814970,  -6678.90441423, -33619.79643378,
      -17329.67691782, -68054.08864133, -59929.39232182,
      -14176.97088206,  30833.36510504, -85864.44933633,
      -75239.50566824, -57104.74927750,  -3715.98865599,
       90169.57785920,   5214.90295971,  30786.57650636,
       20281.84422414,  21840.57389606,  85752.46910389,
      104913.11853287, -11120.68664665, -29664.02315515,
        9047.94473885, -62279.76038293,  73831.68164540,
  };
  // clang-format on

  for (int i = 0; i < N_ATOMS; ++i) {
    for (int j = 0; j < 3; ++j) {
      REQUIRE_THAT(forces(i, j),
                   WithinAbs(expected_forces_flat[i * 3 + j], 1e-2));
    }
  }
}

TEST_CASE("MetatomicPot forces sum to zero", "[metatomic]") {
  rgpot::MetatomicConfig cfg;
  cfg.model_path = "data/lj38/lennard-jones.pt";
  cfg.device = "cpu";
  cfg.length_unit = "angstrom";
  rgpot::MetatomicPot pot(cfg);

  AtomMatrix positions(N_ATOMS, 3);
  for (int i = 0; i < N_ATOMS; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = lj13_pos[i * 3 + j];

  std::vector<int> atmtypes(lj13_atmnrs, lj13_atmnrs + N_ATOMS);
  std::array<std::array<double, 3>, 3> box = {
      {{101.9424, 0.0, 0.0}, {0.0, 103.1426, 0.0}, {0.0, 0.0, 102.6055}}};

  auto [energy, forces, variance] = pot(positions, atmtypes, box);
  (void)energy;
  (void)variance;

  // Newton's third law: force sum should be near zero
  double fx_sum = 0.0, fy_sum = 0.0, fz_sum = 0.0;
  for (int i = 0; i < N_ATOMS; ++i) {
    fx_sum += forces(i, 0);
    fy_sum += forces(i, 1);
    fz_sum += forces(i, 2);
  }
  REQUIRE_THAT(fx_sum, WithinAbs(0.0, 1.0));
  REQUIRE_THAT(fy_sum, WithinAbs(0.0, 1.0));
  REQUIRE_THAT(fz_sum, WithinAbs(0.0, 1.0));
}

TEST_CASE("MetatomicPot is deterministic across repeated calls",
          "[metatomic]") {
  rgpot::MetatomicConfig cfg;
  cfg.model_path = "data/lj38/lennard-jones.pt";
  cfg.device = "cpu";
  cfg.length_unit = "angstrom";
  rgpot::MetatomicPot pot(cfg);

  AtomMatrix positions(N_ATOMS, 3);
  for (int i = 0; i < N_ATOMS; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = lj13_pos[i * 3 + j];

  std::vector<int> atmtypes(lj13_atmnrs, lj13_atmnrs + N_ATOMS);
  std::array<std::array<double, 3>, 3> box = {
      {{101.9424, 0.0, 0.0}, {0.0, 103.1426, 0.0}, {0.0, 0.0, 102.6055}}};

  // Second call exercises the cached atomic_types tensor path
  auto [e1, f1, v1] = pot(positions, atmtypes, box);
  auto [e2, f2, v2] = pot(positions, atmtypes, box);
  (void)v1;
  (void)v2;

  REQUIRE_THAT(e1, WithinAbs(e2, 1e-8));
  for (int i = 0; i < N_ATOMS; ++i) {
    for (int j = 0; j < 3; ++j) {
      REQUIRE_THAT(f1(i, j), WithinAbs(f2(i, j), 1e-6));
    }
  }
}

TEST_CASE("MetatomicPot missing model path throws", "[metatomic]") {
  rgpot::MetatomicConfig cfg;
  cfg.model_path = "data/lj38/does-not-exist.pt";
  cfg.device = "cpu";
  REQUIRE_THROWS(rgpot::MetatomicPot(cfg));
}

// Compile-time coverage of vesin_compat traits with synthetic shapes matching
// both historical vesin ABIs. The installed vesin.h is exercised by the
// MetatomicPot LJ energy/forces case (and by parent gpu-scalapack builds).
TEST_CASE("vesin_compat traits distinguish enum vs struct device layouts",
          "[metatomic][vesin_compat]") {
  struct StructDevice {
    int type = 0;
    int device_id = 0;
  };
  enum EnumDevice { EnumCPU = 1 };

  STATIC_REQUIRE(rgpot::vesin_compat::is_device_struct<StructDevice>::value);
  STATIC_REQUIRE_FALSE(rgpot::vesin_compat::is_device_struct<EnumDevice>::value);

  struct OptionsWithAlgorithm {
    int algorithm = 99;
  };
  struct OptionsWithoutAlgorithm {
    double cutoff = 0.0;
  };

  STATIC_REQUIRE(
      rgpot::vesin_compat::has_algorithm_member<OptionsWithAlgorithm>::value);
  STATIC_REQUIRE_FALSE(
      rgpot::vesin_compat::has_algorithm_member<OptionsWithoutAlgorithm>::value);

  OptionsWithAlgorithm with_alg{};
  with_alg.algorithm = 7;
  rgpot::vesin_compat::set_algorithm_default(with_alg);
  REQUIRE(with_alg.algorithm == 0);

  OptionsWithoutAlgorithm without_alg{};
  without_alg.cutoff = 3.5;
  rgpot::vesin_compat::set_algorithm_default(without_alg);
  REQUIRE_THAT(without_alg.cutoff, WithinAbs(3.5, 1e-15));

  // Live VesinDevice from the build's vesin.h must construct without error.
  // Branch via a function template so if constexpr can discard the inactive
  // arm (plain if constexpr in this non-template TEST_CASE type-checks both).
  auto check_live_cpu = [](auto device) {
    using Device = decltype(device);
    if constexpr (rgpot::vesin_compat::is_device_struct<Device>::value) {
      REQUIRE(device.type == VesinCPU);
      REQUIRE(device.device_id == 0);
    } else {
      REQUIRE(device == static_cast<Device>(VesinCPU));
    }
  };
  check_live_cpu(rgpot::vesin_compat::make_cpu_device());
}

namespace {

// Snapshot / restore process-global LibTorch flags so determinism tests do
// not poison later cases (the flags live on at::globalContext()).
struct TorchContextSnapshot {
  bool deterministic = false;
  bool deterministic_warn_only = false;
  bool flash_sdp = true;
  bool mem_efficient_sdp = true;
  bool math_sdp = true;
  bool cudnn_sdp = true;
  bool tf32_cublas = true;
  bool tf32_cudnn = true;

  static TorchContextSnapshot capture() {
    auto &ctx = at::globalContext();
    TorchContextSnapshot s;
    s.deterministic = ctx.deterministicAlgorithms();
    s.deterministic_warn_only = ctx.deterministicAlgorithmsWarnOnly();
    s.flash_sdp = ctx.userEnabledFlashSDP();
    s.mem_efficient_sdp = ctx.userEnabledMemEfficientSDP();
    s.math_sdp = ctx.userEnabledMathSDP();
    s.cudnn_sdp = ctx.userEnabledCuDNNSDP();
    s.tf32_cublas = ctx.allowTF32CuBLAS();
    s.tf32_cudnn = ctx.allowTF32CuDNN();
    return s;
  }

  void restore() const {
    auto &ctx = at::globalContext();
    ctx.setDeterministicAlgorithms(deterministic, deterministic_warn_only);
    ctx.setSDPUseFlash(flash_sdp);
    ctx.setSDPUseMemEfficient(mem_efficient_sdp);
    ctx.setSDPUseMath(math_sdp);
    ctx.setSDPUseCuDNN(cudnn_sdp);
    ctx.setAllowTF32CuBLAS(tf32_cublas);
    ctx.setAllowTF32CuDNN(tf32_cudnn);
  }
};

void seed_nonstrict_torch_context() {
  auto &ctx = at::globalContext();
  ctx.setDeterministicAlgorithms(false, /*warn_only=*/false);
  ctx.setSDPUseFlash(true);
  ctx.setSDPUseMemEfficient(true);
  ctx.setSDPUseCuDNN(true);
  ctx.setSDPUseMath(true);
  // Enable TF32 so Strict must clearly clear both cuBLAS and cuDNN flags.
  ctx.setAllowTF32CuBLAS(true);
  ctx.setAllowTF32CuDNN(true);
}

} // namespace

TEST_CASE("strict torch determinism policy enables det algorithms and math-only SDP",
          "[metatomic][determinism]") {
  const auto snap = TorchContextSnapshot::capture();
  seed_nonstrict_torch_context();

  rgpot::apply_torch_determinism_policy(rgpot::TorchDeterminismPolicy::Strict);

  auto &ctx = at::globalContext();
  REQUIRE(ctx.deterministicAlgorithms());
  REQUIRE_FALSE(ctx.deterministicAlgorithmsWarnOnly());
  REQUIRE_FALSE(ctx.userEnabledFlashSDP());
  REQUIRE_FALSE(ctx.userEnabledMemEfficientSDP());
  REQUIRE_FALSE(ctx.userEnabledCuDNNSDP());
  REQUIRE(ctx.userEnabledMathSDP());
  REQUIRE_FALSE(ctx.allowTF32CuBLAS());
  REQUIRE_FALSE(ctx.allowTF32CuDNN());

  snap.restore();
}

TEST_CASE("fast torch determinism policy leaves process-global state alone",
          "[metatomic][determinism]") {
  const auto snap = TorchContextSnapshot::capture();
  seed_nonstrict_torch_context();

  // Poison one flag so a no-op Fast path is distinguishable from a restore.
  at::globalContext().setSDPUseFlash(false);
  REQUIRE_FALSE(at::globalContext().userEnabledFlashSDP());
  REQUIRE(at::globalContext().allowTF32CuBLAS());
  REQUIRE(at::globalContext().allowTF32CuDNN());

  rgpot::apply_torch_determinism_policy(rgpot::TorchDeterminismPolicy::Fast);

  auto &ctx = at::globalContext();
  REQUIRE_FALSE(ctx.deterministicAlgorithms());
  REQUIRE_FALSE(ctx.userEnabledFlashSDP());
  REQUIRE(ctx.userEnabledMemEfficientSDP());
  REQUIRE(ctx.userEnabledMathSDP());
  // Fast must not clear TF32 either (still the seeded-true values).
  REQUIRE(ctx.allowTF32CuBLAS());
  REQUIRE(ctx.allowTF32CuDNN());

  snap.restore();
}

TEST_CASE("MetatomicConfig defaults to Fast torch determinism",
          "[metatomic][determinism]") {
  rgpot::MetatomicConfig cfg;
  REQUIRE(cfg.torch_determinism == rgpot::TorchDeterminismPolicy::Fast);
}

TEST_CASE("MetatomicPot construction applies configured torch determinism policy",
          "[metatomic][determinism]") {
  const auto snap = TorchContextSnapshot::capture();
  seed_nonstrict_torch_context();

  rgpot::MetatomicConfig cfg;
  cfg.model_path = "data/lj38/lennard-jones.pt";
  cfg.device = "cpu";
  cfg.length_unit = "angstrom";
  cfg.torch_determinism = rgpot::TorchDeterminismPolicy::Strict;
  rgpot::MetatomicPot pot(cfg);

  auto &ctx = at::globalContext();
  REQUIRE(ctx.deterministicAlgorithms());
  REQUIRE_FALSE(ctx.deterministicAlgorithmsWarnOnly());
  REQUIRE_FALSE(ctx.userEnabledFlashSDP());
  REQUIRE_FALSE(ctx.userEnabledMemEfficientSDP());
  REQUIRE_FALSE(ctx.userEnabledCuDNNSDP());
  REQUIRE(ctx.userEnabledMathSDP());
  REQUIRE_FALSE(ctx.allowTF32CuBLAS());
  REQUIRE_FALSE(ctx.allowTF32CuDNN());

  snap.restore();
}
