// MIT License
// Copyright 2023--present rgpot developers
//
// Wall time per force call for the classical pair kernels on physical
// systems at a chosen atom count. Not a meson test; scripts/bench_pair_scaling.sh
// drives it across sizes and prints one CSV row per (system, mode).
//
// Systems:
//   pt-np    Pt fcc nanoparticle (a = 3.92 A), MorsePot defaults (Pt).
//   pt-bulk  periodic Pt fcc, m x m x m cubic cells, MorsePot defaults.
//   ar-np    Ar fcc nanoparticle (a = 5.26 A), LJPot with Ar parameters
//            (u0 = 0.0104 eV, psi = 3.40 A, cutoff = 8.5 A).
//   ar-cl    the same nanoparticle through LJClusterPot.
//   al-bulk  periodic Al fcc (a = 4.05 A) through the Fortran EAM kernel,
//            when the build carries the Fortran potentials.
//
// Modes:
//   cold   every call sees a geometry the pair-list pool has never matched
//          (rigid translations 1 A apart, cycling through more than the
//          pool's slots), so each call pays a full pair search.
//   warm   the same geometry every call: the cached list always hits.
//   walk   a random walk of 0.02 A per coordinate per call, the step
//          size of a converging optimiser, reflected 0.3 A from the start;
//          the list is rebuilt whenever an atom leaves its skin.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "rgpot/ForceStructs.hpp"
#include "rgpot/LennardJones/LJClusterPot.hpp"
#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/Morse/MorsePot.hpp"
#ifdef RGPOT_HAS_FORTRAN_POTS
#include "rgpot/fortran/FortranPots.hpp"
#endif

namespace {

using clock_type = std::chrono::steady_clock;

/// Largest excursion of a walk coordinate from its starting value (A).
constexpr double kWalkBound = 0.3;

struct System {
  std::vector<double> pos;
  std::vector<int> types;
  std::array<double, 9> box{};
};

/// fcc sites of an m x m x m block of cubic cells with lattice constant a.
std::vector<double> fccBlock(int m, double a) {
  static const double basis[4][3] = {
      {0.0, 0.0, 0.0}, {0.0, 0.5, 0.5}, {0.5, 0.0, 0.5}, {0.5, 0.5, 0.0}};
  std::vector<double> R;
  R.reserve(static_cast<std::size_t>(12 * m * m * m));
  for (int ix = 0; ix < m; ++ix)
    for (int iy = 0; iy < m; ++iy)
      for (int iz = 0; iz < m; ++iz)
        for (const auto &b : basis) {
          R.push_back((ix + b[0]) * a);
          R.push_back((iy + b[1]) * a);
          R.push_back((iz + b[2]) * a);
        }
  return R;
}

/// The n fcc sites nearest the centre of a large block: a near-spherical
/// nanoparticle of exactly n atoms, centred in a vacuum box whose side
/// leaves `vacuum` of empty space on every face.
System nanoparticle(std::size_t n, double a, double vacuum, int z) {
  int m = 2;
  while (4.0 * m * m * m < 2.0 * static_cast<double>(n) + 64.0)
    ++m;
  auto R = fccBlock(m, a);
  const double c = 0.5 * (m - 1) * a + 0.25 * a;
  std::vector<std::pair<double, std::size_t>> d;
  for (std::size_t i = 0; i < R.size() / 3; ++i) {
    const double dx = R[3 * i] - c, dy = R[3 * i + 1] - c,
                 dz = R[3 * i + 2] - c;
    d.emplace_back(dx * dx + dy * dy + dz * dz, i);
  }
  std::sort(d.begin(), d.end());
  System s;
  double lo = 1e300, hi = -1e300;
  for (std::size_t k = 0; k < n; ++k) {
    const std::size_t i = d[k].second;
    for (int q = 0; q < 3; ++q) {
      s.pos.push_back(R[3 * i + q]);
      lo = std::min(lo, R[3 * i + q]);
      hi = std::max(hi, R[3 * i + q]);
    }
  }
  const double side = (hi - lo) + 2.0 * vacuum;
  for (double &x : s.pos)
    x += vacuum - lo;
  s.types.assign(n, z);
  s.box = {side, 0.0, 0.0, 0.0, side, 0.0, 0.0, 0.0, side};
  return s;
}

System bulk(int m, double a, int z) {
  System s;
  s.pos = fccBlock(m, a);
  // Thermal-scale noise so no two pair distances coincide exactly.
  std::mt19937_64 rng(7);
  std::uniform_real_distribution<double> u(-0.03, 0.03);
  for (double &x : s.pos)
    x += u(rng);
  s.types.assign(s.pos.size() / 3, z);
  const double side = m * a;
  s.box = {side, 0.0, 0.0, 0.0, side, 0.0, 0.0, 0.0, side};
  return s;
}

int cellsFor(std::size_t n) {
  int m = 1;
  while (4.0 * (m + 1) * (m + 1) * (m + 1) <= 1.15 * static_cast<double>(n))
    ++m;
  return std::max(m, 1);
}

double callOnce(const rgpot::PotentialBase &potBase, const System &s,
                const std::vector<double> &pos, std::vector<double> &F) {
  // forceImpl through the concrete type: the timed path is the kernel and
  // its neighbour bookkeeping, not an AtomMatrix allocation per call.
  rgpot::ForceInput fi{.nAtoms = s.types.size(),
                       .pos = pos.data(),
                       .atmnrs = s.types.data(),
                       .box = s.box.data()};
  rgpot::ForceOut fo{.F = F.data(),
                     .energy = 0.0,
                     .variance = 0.0,
                     .stress = {},
                     .has_stress = 0};
  if (auto *p = dynamic_cast<const rgpot::MorsePot *>(&potBase))
    p->forceImpl(fi, &fo);
  else if (auto *p = dynamic_cast<const rgpot::LJPot *>(&potBase))
    p->forceImpl(fi, &fo);
  else if (auto *p = dynamic_cast<const rgpot::LJClusterPot *>(&potBase))
    p->forceImpl(fi, &fo);
#ifdef RGPOT_HAS_FORTRAN_POTS
  else if (auto *p =
               dynamic_cast<const rgpot::fortranpots::EAMAlPot *>(&potBase))
    p->forceImpl(fi, &fo);
#endif
  return fo.energy;
}

} // namespace

int main(int argc, char **argv) {
  std::string system = "pt-np";
  std::string mode = "warm";
  std::size_t n = 1000;
  long calls = 200;
  int repeats = 5;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> const char * {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--system")
      system = next();
    else if (a == "--mode")
      mode = next();
    else if (a == "--n")
      n = std::strtoull(next(), nullptr, 10);
    else if (a == "--calls")
      calls = std::strtol(next(), nullptr, 10);
    else if (a == "--repeats")
      repeats = std::atoi(next());
    else {
      std::fprintf(stderr,
                   "usage: time_pair_scaling --system "
                   "pt-np|pt-bulk|ar-np|ar-cl|al-bulk --mode cold|warm|walk "
                   "--n N [--calls K] [--repeats R]\n");
      return 2;
    }
  }

  std::unique_ptr<rgpot::PotentialBase> pot;
  System s;
  if (system == "pt-np" || system == "pt-bulk") {
    pot = std::make_unique<rgpot::MorsePot>();
    s = system == "pt-np" ? nanoparticle(n, 3.92, 10.5, 78)
                          : bulk(cellsFor(n), 3.92, 78);
  } else if (system == "ar-np" || system == "ar-cl") {
    const double u0 = 0.0104, psi = 3.40, rc = 8.5;
    if (system == "ar-np")
      pot = std::make_unique<rgpot::LJPot>(
          rgpot::LJConfig{.u0 = u0, .cutoff = rc, .psi = psi});
    else
      pot = std::make_unique<rgpot::LJClusterPot>(
          rgpot::LJClusterConfig{.u0 = u0, .cutoff = rc, .psi = psi});
    s = nanoparticle(n, 5.26, 9.5, 18);
#ifdef RGPOT_HAS_FORTRAN_POTS
  } else if (system == "al-bulk") {
    pot = std::make_unique<rgpot::fortranpots::EAMAlPot>();
    s = bulk(cellsFor(n), 4.05, 13);
#endif
  } else {
    std::fprintf(stderr, "unknown or unbuilt system %s\n", system.c_str());
    return 2;
  }

  const std::size_t natoms = s.types.size();
  std::vector<double> F(3 * natoms, 0.0);
  std::vector<double> pos = s.pos;
  std::mt19937_64 rng(1234);
  std::uniform_real_distribution<double> step(-0.02, 0.02);

  std::vector<double> perCall;
  double energy = 0.0;
  for (int r = 0; r < repeats; ++r) {
    pos = s.pos;
    // Warm the pool so "warm" and "walk" start from a captured list; "cold"
    // runs from distinct translations so the warm-up never helps it.
    if (mode != "cold") {
      callOnce(*pot, s, pos, F);
      callOnce(*pot, s, pos, F);
    }
    const auto t0 = clock_type::now();
    for (long c = 0; c < calls; ++c) {
      if (mode == "cold") {
        const double shift = 1.0 * static_cast<double>((c + 17 * r) % 64);
        for (std::size_t k = 0; k < pos.size(); k += 3)
          pos[k] = s.pos[k] + shift;
      } else if (mode == "walk") {
        // Reflect at kWalkBound from the start so the geometry stays near
        // its lattice and the energy stays physical over many calls.
        for (std::size_t k = 0; k < pos.size(); ++k) {
          double x = pos[k] + step(rng);
          const double off = x - s.pos[k];
          if (off > kWalkBound)
            x = s.pos[k] + 2.0 * kWalkBound - off;
          else if (off < -kWalkBound)
            x = s.pos[k] - 2.0 * kWalkBound - off;
          pos[k] = x;
        }
      }
      energy = callOnce(*pot, s, pos, F);
    }
    const auto t1 = clock_type::now();
    perCall.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count() /
                      static_cast<double>(calls));
  }
  std::sort(perCall.begin(), perCall.end());
  std::printf("system,mode,natoms,calls,repeats,us_per_call_median,us_per_"
              "call_min,us_per_call_max,energy_last\n");
  std::printf("%s,%s,%zu,%ld,%d,%.3f,%.3f,%.3f,%.12g\n", system.c_str(),
              mode.c_str(), natoms, calls, repeats,
              perCall[perCall.size() / 2], perCall.front(), perCall.back(),
              energy);
  return 0;
}
