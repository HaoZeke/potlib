// MIT License — time rgpot linked XTBPot vs XTBDlopen on water GFN2.
#include <array>
// Usage: xtb_backend_bench [--json out.json] [--warmup N] [--iters N]
// Env: RGPOT_XTB_ENGINE path to libxtb_engine.so (required for dlopen arm)

#include "rgpot/XTBPot/XTBDlopen.hpp"
#include "rgpot/XTBPot/XTBPot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using rgpot::types::AtomMatrix;
using SteadyClock = std::chrono::steady_clock;

static const double water_pos[] = {
    0.00000000, 0.00000000,  0.11779000, 0.00000000, 0.75545000,
    -0.47116000, 0.00000000, -0.75545000, -0.47116000};
static const int water_atmnrs[] = {8, 1, 1};

static double mean_ms(const std::vector<double> &xs) {
  double s = 0;
  for (double x : xs)
    s += x;
  return xs.empty() ? 0.0 : s / static_cast<double>(xs.size());
}

template <typename Eval>
static std::vector<double> time_force(Eval &&eval, int warmup, int iters) {
  for (int i = 0; i < warmup; ++i)
    eval();
  std::vector<double> samples;
  samples.reserve(static_cast<size_t>(iters));
  for (int i = 0; i < iters; ++i) {
    auto t0 = SteadyClock::now();
    eval();
    auto t1 = SteadyClock::now();
    samples.push_back(
        std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  return samples;
}

int main(int argc, char **argv) {
  std::string json_out;
  int warmup = 5;
  int iters = 50;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--json" && i + 1 < argc)
      json_out = argv[++i];
    else if (a == "--warmup" && i + 1 < argc)
      warmup = std::atoi(argv[++i]);
    else if (a == "--iters" && i + 1 < argc)
      iters = std::atoi(argv[++i]);
  }

  AtomMatrix positions(3, 3);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      positions(i, j) = water_pos[i * 3 + j];
  std::vector<int> atmtypes(water_atmnrs, water_atmnrs + 3);
  std::array<std::array<double, 3>, 3> box = {
      {{100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}};

  rgpot::XTBConfig cfg;
  cfg.method = rgpot::GFNMethod::GFN2xTB;
  rgpot::XTBPot linked(cfg);

  const char *eng = std::getenv("RGPOT_XTB_ENGINE");
  rgpot::XTBDlopenConfig dcfg;
  dcfg.xtb = cfg;
  if (eng && *eng)
    dcfg.engine_path = eng;
  rgpot::XTBDlopen dl(dcfg);

  double e_l = 0, e_d = 0;
  auto samples_l = time_force(
      [&] {
        auto [e, f, v] = linked(positions, atmtypes, box);
        e_l = e;
        (void)f;
        (void)v;
      },
      warmup, iters);
  auto samples_d = time_force(
      [&] {
        auto [e, f, v] = dl(positions, atmtypes, box);
        e_d = e;
        (void)f;
        (void)v;
      },
      warmup, iters);

  const double mean_l = mean_ms(samples_l);
  const double mean_d = mean_ms(samples_d);
  const double ratio = mean_l > 0 ? mean_d / mean_l : 0.0;
  const bool as_fast_or_faster = mean_d <= mean_l * 1.05; // 5% tolerance

  std::cout << "rgpot_xtb_bench method=GFN2 water n_atoms=3\n"
            << "linked_mean_ms=" << mean_l << " dlopen_mean_ms=" << mean_d
            << " ratio_dlopen_over_linked=" << ratio
            << " energy_linked=" << e_l << " energy_dlopen=" << e_d
            << " as_fast_or_faster_vs_linked="
            << (as_fast_or_faster ? "true" : "false") << "\n";

  if (!json_out.empty()) {
    std::ofstream o(json_out);
    o << "{\n"
      << "  \"system\": \"water\",\n"
      << "  \"method\": \"GFN2xTB\",\n"
      << "  \"n_atoms\": 3,\n"
      << "  \"warmup\": " << warmup << ",\n"
      << "  \"iters\": " << iters << ",\n"
      << "  \"rgpot_linked_mean_ms\": " << mean_l << ",\n"
      << "  \"rgpot_dlopen_mean_ms\": " << mean_d << ",\n"
      << "  \"ratio_dlopen_over_linked\": " << ratio << ",\n"
      << "  \"energy_linked_eV\": " << e_l << ",\n"
      << "  \"energy_dlopen_eV\": " << e_d << ",\n"
      << "  \"dlopen_as_fast_or_faster_than_linked\": "
      << (as_fast_or_faster ? "true" : "false") << "\n"
      << "}\n";
  }
  return 0;
}
