// MIT License
// Copyright 2023--present rgpot developers
//
// Time in-process LJPot operator() on the ExprPotTest two-atom Ar fixture.
// Optional ExprPot("lj") when built with -Dwith_expr=true.
// Prints ns/call. Does not seed numbers.

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

#ifdef RGPOT_HAS_EXPR
#include "rgpot/ExprPot/ExprPot.hpp"
#endif

using rgpot::types::AtomMatrix;

namespace {

AtomMatrix twoAtomPositions() {
  return AtomMatrix{{0.0, 0.0, 0.0}, {1.5, 0.0, 0.0}};
}

std::vector<int> twoAtomTypes() { return {18, 18}; }

std::array<std::array<double, 3>, 3> wideCell() {
  return {{{40.0, 0.0, 0.0}, {0.0, 40.0, 0.0}, {0.0, 0.0, 40.0}}};
}

int parsePositive(const char *s, int fallback) {
  if (s == nullptr || *s == '\0') {
    return fallback;
  }
  char *end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || v < 1 || v > 100000000) {
    return fallback;
  }
  return static_cast<int>(v);
}

template <typename Fn>
double nsPerCall(int nCalls, Fn &&fn) {
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < nCalls; ++i) {
    fn();
  }
  const auto t1 = std::chrono::steady_clock::now();
  const auto ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  return static_cast<double>(ns) / static_cast<double>(nCalls);
}

} // namespace

int main(int argc, char **argv) {
  const int nCalls = parsePositive(argc > 1 ? argv[1] : nullptr, 20000);
  const int warmup = parsePositive(argc > 2 ? argv[2] : nullptr, 50);
  const auto positions = twoAtomPositions();
  const auto types = twoAtomTypes();
  const auto box = wideCell();

  rgpot::LJPot lj;
  double energy = 0.0;
  for (int i = 0; i < warmup; ++i) {
    auto [e, forces, var] = lj(positions, types, box);
    energy = e;
    (void)forces;
    (void)var;
  }
  {
    auto [e, forces, var] = lj(positions, types, box);
    energy = e;
    (void)forces;
    (void)var;
  }
  const double aNs = nsPerCall(nCalls, [&]() {
    auto [e, forces, var] = lj(positions, types, box);
    energy = e;
    (void)forces;
    (void)var;
  });

  std::printf("fixture=exprpot_two_atom_ar\n");
  std::printf("n_atoms=2\n");
  std::printf("n_calls=%d\n", nCalls);
  std::printf("warmup=%d\n", warmup);
  std::printf("A_ljpot_energy_eV=%.12e\n", energy);
  std::printf("A_ljpot_ns_per_call=%.3f\n", aNs);

#ifdef RGPOT_HAS_EXPR
  std::vector<rgpot::ExprPot::Term> terms;
  terms.emplace_back("lj", std::make_unique<rgpot::LJPot>());
  rgpot::ExprPot expr("lj", std::move(terms));
  double cEnergy = 0.0;
  for (int i = 0; i < warmup; ++i) {
    auto [e, forces, var] = expr(positions, types, box);
    cEnergy = e;
    (void)forces;
    (void)var;
  }
  const double cNs = nsPerCall(nCalls, [&]() {
    auto [e, forces, var] = expr(positions, types, box);
    cEnergy = e;
    (void)forces;
    (void)var;
  });
  std::printf("C_exprpot_lj_energy_eV=%.12e\n", cEnergy);
  std::printf("C_exprpot_lj_ns_per_call=%.3f\n", cNs);
  if (aNs > 0.0) {
    std::printf("C_over_A_factor=%.3f\n", cNs / aNs);
  }
#else
  std::printf("C_exprpot_lj_ns_per_call=skipped\n");
#endif
  std::printf("note=XcKernel is not a Potential; not timed\n");
  return EXIT_SUCCESS;
}
