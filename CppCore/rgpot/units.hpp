#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief Physical constants and unit conversion factors for atomistic potentials.
 *
 * All values are CODATA 2018 recommended values.
 * rgpot's native unit system: energy = eV, length = Angstrom, force = eV/Angstrom.
 */

namespace rgpot::units {

// Length
inline constexpr double BOHR_TO_ANGSTROM = 0.529177210903;
inline constexpr double ANGSTROM_TO_BOHR = 1.0 / BOHR_TO_ANGSTROM;

// Energy
inline constexpr double HARTREE_TO_EV = 27.211386245988;
inline constexpr double EV_TO_HARTREE = 1.0 / HARTREE_TO_EV;

// Force / gradient conversion (Hartree/Bohr <-> eV/Angstrom)
inline constexpr double HARTREE_BOHR_TO_EV_ANGSTROM =
    HARTREE_TO_EV / BOHR_TO_ANGSTROM;
// Fused: negate gradient and convert to forces in eV/Angstrom
inline constexpr double NEG_GRAD_TO_FORCE = -HARTREE_BOHR_TO_EV_ANGSTROM;

// Temperature / Boltzmann
inline constexpr double KB_HARTREE = 3.1668115634556e-6; // k_B in Hartree/K
inline constexpr double KB_EV = 8.617333262e-5;          // k_B in eV/K

} // namespace rgpot::units
