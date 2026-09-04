// Dump independent LJ / Morse (and optional D3) energy and forces for the
// committed two-atom fixture. Pins are eV and eV/A. Overwrites the npy files.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "npy_io.hpp"
#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/Morse/MorsePot.hpp"
#ifdef RGPOT_HAS_DFTD3
#include "rgpot/D3Pot/D3Pot.hpp"
#endif
#include "rgpot/types/AtomMatrix.hpp"

using rgpot::testio::save_npy;
using rgpot::types::AtomMatrix;

namespace {

constexpr const char *kData = "CppCore/tests/data/expr";

AtomMatrix twoAtomPositions() {
  return AtomMatrix{{0.0, 0.0, 0.0}, {1.5, 0.0, 0.0}};
}

std::vector<int> twoAtomTypes() { return {18, 18}; }

std::array<std::array<double, 3>, 3> wideCell() {
  return {{{40.0, 0.0, 0.0}, {0.0, 40.0, 0.0}, {0.0, 0.0, 40.0}}};
}

std::vector<double> flattenForces(const AtomMatrix &f) {
  return {f.data(), f.data() + f.size()};
}

void write_energy_forces(const std::string &stem, double energy,
                         const AtomMatrix &forces) {
  save_npy(std::string(kData) + "/" + stem + "_energy.npy", {energy}, {});
  save_npy(std::string(kData) + "/" + stem + "_forces.npy", flattenForces(forces),
           {forces.rows(), forces.cols()});
  std::printf("%s  %.16e eV\n", stem.c_str(), energy);
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

void write_geometry(const AtomMatrix &pos, const std::vector<int> &nums,
                    const std::array<std::array<double, 3>, 3> &box) {
  std::vector<double> p(pos.data(), pos.data() + pos.size());
  std::vector<double> n(nums.size());
  for (size_t i = 0; i < nums.size(); ++i) {
    n[i] = static_cast<double>(nums[i]);
  }
  std::vector<double> b(9);
  for (size_t i = 0; i < 3; ++i) {
    for (size_t j = 0; j < 3; ++j) {
      b[3 * i + j] = box[i][j];
    }
  }
  save_npy(std::string(kData) + "/positions.npy", p, {pos.rows(), pos.cols()});
  save_npy(std::string(kData) + "/numbers.npy", n, {n.size()});
  save_npy(std::string(kData) + "/box.npy", b, {3, 3});
}

} // namespace

int main() {
  try {
    std::filesystem::create_directories(kData);
    const auto pos = twoAtomPositions();
    const auto nums = twoAtomTypes();
    const auto box = wideCell();
    write_geometry(pos, nums, box);

    rgpot::LJPot lj;
    rgpot::MorsePot morse;
    auto [e_lj, f_lj, v_lj] = lj(pos, nums, box);
    auto [e_m, f_m, v_m] = morse(pos, nums, box);
    (void)v_lj;
    (void)v_m;

    write_energy_forces("lj", e_lj, f_lj);
    write_energy_forces("morse", e_m, f_m);
    write_energy_forces("identity_lj", e_lj, f_lj);
    write_energy_forces("half_lj_plus_morse", 0.5 * e_lj + e_m,
                        scaleAdd(0.5, f_lj, 1.0, f_m));
    write_energy_forces("two_lj_minus_lj", e_lj, f_lj);
    write_energy_forces("half_paren_lj_morse", 0.5 * (e_lj + e_m),
                        scaleAdd(0.5, f_lj, 0.5, f_m));

#ifdef RGPOT_HAS_DFTD3
    rgpot::D3Pot d3;
    auto [e_d3, f_d3, v_d3] = d3(pos, nums, box);
    (void)v_d3;
    write_energy_forces("d3", e_d3, f_d3);
    write_energy_forces("half_lj_plus_d3", 0.5 * e_lj + e_d3,
                        scaleAdd(0.5, f_lj, 1.0, f_d3));
#endif
    return 0;
  } catch (const std::exception &ex) {
    std::fprintf(stderr, "dump_expr_goldens: %s\n", ex.what());
    return 1;
  }
}
