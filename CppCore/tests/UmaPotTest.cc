// MIT License — UmaPot loads an AOTI .pt2, not a metatomic checkpoint.

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include "rgpot/UmaPot/UmaPot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

using Catch::Matchers::WithinAbs;
using rgpot::types::AtomMatrix;
namespace fs = std::filesystem;

// ASE FAIRChemCalculator uma-s-1p1 / omol on Baker 01_hcn reactant.con.
static constexpr double kHcnAseOmolEnergy = -2542.4200325775496;
static constexpr double kHcnAseOmolForces[3][3] = {
    {0.029140297323465347, 0.011646071448922157, -1.4753634929656982},
    {-0.016493169590830803, -0.0065111019648611546, 1.7600582838058472},
    {-0.012647130526602268, -0.0051349685527384281, -0.28469470143318176},
};

static std::string resolve_uma_omol_pt2() {
  if (const char *env = std::getenv("RGPOT_UMA_OMOL_PT2"); env && *env) {
    if (fs::exists(env))
      return std::string(env);
    FAIL("RGPOT_UMA_OMOL_PT2 is set but the file is missing: "
         << env << ". Export an AOTI package with scripts/export_uma_aoti.py");
  }
  // workdir is CppCore/tests; also accept repo-root-relative paths.
  static const char *const kCandidates[] = {
      "data/uma/uma-s-1p1-omol-hcn.pt2",
      "CppCore/tests/data/uma/uma-s-1p1-omol-hcn.pt2",
      "../../bench_data/uma/uma-s-1p1-omol-hcn.pt2",
      "bench_data/uma/uma-s-1p1-omol-hcn.pt2",
  };
  for (const char *path : kCandidates) {
    if (fs::exists(path))
      return std::string(path);
  }
  FAIL("UmaPot HCN fixture needs uma-s-1p1-omol-hcn.pt2. Export with "
       "scripts/export_uma_aoti.py and set RGPOT_UMA_OMOL_PT2, or place the "
       "file at CppCore/tests/data/uma/uma-s-1p1-omol-hcn.pt2 or "
       "bench_data/uma/uma-s-1p1-omol-hcn.pt2");
  return {};
}

TEST_CASE("UmaPot empty model_path throws", "[UmaPot]") {
  rgpot::UmaConfig cfg;
  REQUIRE_THROWS_WITH(rgpot::UmaPot(cfg),
                      Catch::Matchers::ContainsSubstring("model_path"));
}

TEST_CASE("UmaPot rejects a metatomic checkpoint", "[UmaPot]") {
  rgpot::UmaConfig cfg;
  cfg.model_path = "data/lj38/lennard-jones.pt";
  REQUIRE_THROWS_WITH(rgpot::UmaPot(cfg),
                      Catch::Matchers::ContainsSubstring(".pt2"));
}

TEST_CASE("UmaPot paramsKey changes with charge and path", "[UmaPot]") {
  rgpot::UmaConfig cfg;
  cfg.model_path = "no-such-uma.pt2";
  rgpot::UmaPot a(cfg);
  const uint64_t k0 = a.paramsKey();
  a.setChargeSpin(-1, 1);
  REQUIRE(a.paramsKey() != k0);
  cfg.model_path = "other-uma.pt2";
  rgpot::UmaPot b(cfg);
  REQUIRE(b.paramsKey() != k0);
}

TEST_CASE("UmaPot Baker HCN matches ASE FAIRChem omol", "[UmaPot][omol]") {
  const std::string model = resolve_uma_omol_pt2();

  rgpot::UmaConfig cfg;
  cfg.model_path = model;
  cfg.device = "cpu";
  cfg.task_name = "omol";
  cfg.charge = 0;
  cfg.spin = 1;
  rgpot::UmaPot pot(cfg);

  // Baker 01_hcn reactant.con, Angstrom, 25 A cubic cell, PBC.
  const AtomMatrix positions{
      {12.49734736216627162, 12.49892801474515913, 12.54059929828148512},
      {12.50115413363106498, 12.50036504272228832, 11.38209979880783251},
      {12.50149850420264563, 12.50069809648255514, 13.61514544631068446},
  };
  const std::vector<int> atmtypes{6, 7, 1};
  const std::array<std::array<double, 3>, 3> box{
      {{25.0, 0.0, 0.0}, {0.0, 25.0, 0.0}, {0.0, 0.0, 25.0}}};

  auto [energy, forces, variance] = pot(positions, atmtypes, box);
  (void)variance;

  std::printf("backend=UmaPot\n");
  std::printf("model=%s\n", model.c_str());
  std::printf("energy=%.17g\n", energy);
  double max_df = 0.0;
  REQUIRE(forces.rows() == 3);
  REQUIRE(forces.cols() == 3);
  for (size_t i = 0; i < 3; ++i) {
    std::printf("force %zu %.17g %.17g %.17g\n", i, forces(i, 0), forces(i, 1),
                forces(i, 2));
    for (size_t j = 0; j < 3; ++j) {
      max_df =
          std::max(max_df, std::abs(forces(i, j) - kHcnAseOmolForces[i][j]));
    }
  }
  std::printf("max_dF=%.17g\n", max_df);

  REQUIRE_THAT(energy, WithinAbs(kHcnAseOmolEnergy, 1e-4));
  REQUIRE_THAT(max_df, WithinAbs(0.0, 1e-4));
}
