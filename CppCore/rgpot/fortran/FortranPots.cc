// MIT License
// Copyright 2023--present rgpot developers

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "rgpot/fortran/FortranPots.hpp"

namespace rgpot {
namespace fortranpots {

namespace {

extern "C" {
/// Kernels needing only geometry.
int rgpot_sw_force(int32_t natoms, const double *positions, const double *cell,
                   double *forces, double *energy);
int rgpot_edip_force(int32_t natoms, const double *positions,
                     const double *cell, double *forces, double *energy);
int rgpot_lenosky_force(int32_t natoms, const double *positions,
                        const double *cell, double *forces, double *energy);
int rgpot_tersoff_force(int32_t natoms, const double *positions,
                        const double *cell, double *forces, double *energy);
int rgpot_eam_al_force(int32_t natoms, const double *positions,
                       const double *cell, double *forces, double *energy);

/// Kernels dispatching on atomic number.
int rgpot_fehe_force(int32_t natoms, const double *positions,
                     const int32_t *atomic_numbers, const double *cell,
                     double *forces, double *energy);
int rgpot_cuh2_force(int32_t natoms, const double *positions,
                     const int32_t *atomic_numbers, const double *cell,
                     double *forces, double *energy);
int rgpot_water_h_force(int32_t natoms, const double *positions,
                        const int32_t *atomic_numbers, const double *cell,
                        double *forces, double *energy);

int rgpot_fortran_last_error(char *buffer, int buffer_len);
}

/// Throw carrying the kernel's own message.
[[noreturn]] void raise(const char *pot, int status) {
  std::array<char, 512> buffer{};
  const int written =
      rgpot_fortran_last_error(buffer.data(), static_cast<int>(buffer.size()));

  std::string message = std::string(pot) + " potential failed (status " +
                        std::to_string(status) + ")";
  if (written > 0) {
    message += ": ";
    message.append(buffer.data(), static_cast<std::size_t>(written));
  }
  throw std::runtime_error(message);
}

} // namespace

#define RGPOT_FORTRAN_POT_IMPL(ClassName, Entry, Label)                        \
  void ClassName::forceImpl(const ForceInput &in, ForceOut *out) const {       \
    const int status = Entry(static_cast<int32_t>(in.nAtoms), in.pos, in.box,  \
                             out->F, &out->energy);                            \
    if (status != 0) {                                                         \
      raise(Label, status);                                                    \
    }                                                                          \
    out->variance = 0.0;                                                       \
  }

RGPOT_FORTRAN_POT_IMPL(SWPot, rgpot_sw_force, "SW")
RGPOT_FORTRAN_POT_IMPL(EDIPPot, rgpot_edip_force, "EDIP")
RGPOT_FORTRAN_POT_IMPL(LenoskyPot, rgpot_lenosky_force, "Lenosky")
RGPOT_FORTRAN_POT_IMPL(TersoffPot, rgpot_tersoff_force, "Tersoff")
RGPOT_FORTRAN_POT_IMPL(EAMAlPot, rgpot_eam_al_force, "EAM-Al")

#undef RGPOT_FORTRAN_POT_IMPL

#define RGPOT_FORTRAN_SPECIES_POT_IMPL(ClassName, Entry, Label)                \
  void ClassName::forceImpl(const ForceInput &in, ForceOut *out) const {       \
    const int status =                                                         \
        Entry(static_cast<int32_t>(in.nAtoms), in.pos, in.atmnrs, in.box,      \
              out->F, &out->energy);                                           \
    if (status != 0) {                                                         \
      raise(Label, status);                                                    \
    }                                                                          \
    out->variance = 0.0;                                                       \
  }

RGPOT_FORTRAN_SPECIES_POT_IMPL(FeHePot, rgpot_fehe_force, "FeHe")
RGPOT_FORTRAN_SPECIES_POT_IMPL(CuH2FortranPot, rgpot_cuh2_force, "CuH2")
RGPOT_FORTRAN_SPECIES_POT_IMPL(WaterHPot, rgpot_water_h_force, "Water-H")

#undef RGPOT_FORTRAN_SPECIES_POT_IMPL

} // namespace fortranpots
} // namespace rgpot
