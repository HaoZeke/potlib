#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief C++ faces for the Fortran 2018 potential kernels.
 *
 * Each kernel exposes one `bind(c)` entry taking flat buffers and
 * returning a status; failures carry a message retrievable through
 * `rgpot_fortran_last_error`. The entries are internal to librgpot (the
 * Fortran objects build with hidden visibility), so these classes are
 * the only way to reach them.
 *
 * All of them report `Reentrancy::ProcessSerial`: the kernels themselves
 * are free of saved physics state, but each keeps one neighbour table in
 * module storage so vesin can reuse its buffers across calls, and that
 * table is process-global. Handing the table to the caller as an opaque
 * handle would lift them to `SharedInstance`.
 */

#include <cstddef>
#include <cstdint>

#include "rgpot/ForceStructs.hpp"
#include "rgpot/Potential.hpp"
#include "rgpot/pot_caps.hpp"
#include "rgpot/pot_types.hpp"

namespace rgpot {
namespace fortranpots {

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

/// Throw a runtime_error carrying the kernel's own message.
[[noreturn]] void raise(const char *pot, int status);

namespace detail {

/// Shared body: dispatch, check, and normalise the variance slot.
template <typename Fn>
inline void invoke(const char *pot, const ForceInput &in, ForceOut *out,
                   Fn &&call) {
  const auto natoms = static_cast<int32_t>(in.nAtoms);
  const int status = call(natoms);
  if (status != 0) {
    raise(pot, status);
  }
  out->variance = 0.0;
}

} // namespace detail

/// Geometry-only kernels.
#define RGPOT_FORTRAN_POT(ClassName, PotTypeValue, Entry, Label)               \
  class ClassName : public Potential<ClassName> {                              \
  public:                                                                      \
    ClassName() : Potential(PotType::PotTypeValue) {}                          \
    void forceImpl(const ForceInput &in, ForceOut *out) const override {       \
      detail::invoke(Label, in, out, [&](int32_t n) {                          \
        return Entry(n, in.pos, in.box, out->F, &out->energy);                 \
      });                                                                      \
    }                                                                          \
    [[nodiscard]] PotCaps caps() const noexcept override {                     \
      return {.reentrancy = Reentrancy::ProcessSerial};                        \
    }                                                                          \
  }

RGPOT_FORTRAN_POT(SWPot, SWSi, rgpot_sw_force, "SW");
RGPOT_FORTRAN_POT(EDIPPot, EDIP, rgpot_edip_force, "EDIP");
RGPOT_FORTRAN_POT(LenoskyPot, LenoskySi, rgpot_lenosky_force, "Lenosky");
RGPOT_FORTRAN_POT(TersoffPot, TersoffSi, rgpot_tersoff_force, "Tersoff");
RGPOT_FORTRAN_POT(EAMAlPot, EAMAl, rgpot_eam_al_force, "EAM-Al");

#undef RGPOT_FORTRAN_POT

/// Kernels dispatching on atomic number.
#define RGPOT_FORTRAN_SPECIES_POT(ClassName, PotTypeValue, Entry, Label)       \
  class ClassName : public Potential<ClassName> {                              \
  public:                                                                      \
    ClassName() : Potential(PotType::PotTypeValue) {}                          \
    void forceImpl(const ForceInput &in, ForceOut *out) const override {       \
      detail::invoke(Label, in, out, [&](int32_t n) {                          \
        return Entry(n, in.pos, in.atmnrs, in.box, out->F, &out->energy);      \
      });                                                                      \
    }                                                                          \
    [[nodiscard]] PotCaps caps() const noexcept override {                     \
      return {.reentrancy = Reentrancy::ProcessSerial};                        \
    }                                                                          \
  }

RGPOT_FORTRAN_SPECIES_POT(FeHePot, FeHe, rgpot_fehe_force, "FeHe");
RGPOT_FORTRAN_SPECIES_POT(CuH2FortranPot, CuH2, rgpot_cuh2_force, "CuH2");
RGPOT_FORTRAN_SPECIES_POT(WaterHPot, WaterH, rgpot_water_h_force, "Water-H");

#undef RGPOT_FORTRAN_SPECIES_POT

} // namespace fortranpots
} // namespace rgpot
