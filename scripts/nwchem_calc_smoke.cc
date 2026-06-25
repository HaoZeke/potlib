// Smoke: load NWChemPot via engine, run water energy/forces once.
// Build+run via meson test or: c++ -std=c++17 ... (see README)
#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/types/AtomMatrix.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
  rgpot::NWChemConfig cfg;
  cfg.basis = "sto-3g";
  cfg.theory = "scf";
  cfg.scf_type = "rhf";
  cfg.charge = 0;
  cfg.multiplicity = 1;

  rgpot::NWChemPot pot(cfg);
  if (!pot.available()) {
    std::cerr << "NWChemPot engine not loaded (set RGPOT_NWCHEM_ENGINE / LD_LIBRARY_PATH)\n";
    return 2;
  }
  if (!rgpot::NWChemPot::abi_available()) {
    std::cerr << "nwchem executable not found (set RGPOT_NWCHEM_EXE or PATH)\n";
    return 3;
  }

  static const double water_pos[] = {
      0.0, 0.0, 0.11779, 0.0, 0.75545, -0.47116, 0.0, -0.75545, -0.47116};
  rgpot::types::AtomMatrix pos(3, 3);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      pos(i, j) = water_pos[i * 3 + j];
  std::vector<int> z = {8, 1, 1};
  std::array<std::array<double, 3>, 3> box = {
      {{100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}};

  try {
    auto [energy, forces] = pot(pos, z, box);
    std::cout << "NWChemPot water energy (eV) = " << energy << "\n";
    std::cout << "forces (eV/A):\n";
    for (int i = 0; i < 3; ++i)
      std::cout << "  atom " << i << ": " << forces(i, 0) << " " << forces(i, 1)
                << " " << forces(i, 2) << "\n";
    if (!std::isfinite(energy) || energy >= 0.0) {
      std::cerr << "unexpected energy (want finite negative eV)\n";
      return 4;
    }
    std::cout << "OK\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "calculation failed: " << ex.what() << "\n";
    return 1;
  }
}
