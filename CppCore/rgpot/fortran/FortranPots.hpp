#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief C++ faces for the Fortran 2018 potential kernels.
 *
 * Each kernel exposes one `bind(c)` entry taking flat buffers and
 * returning a status. Those entries stay inside librgpot: `forceImpl` is
 * defined out of line, and the archive holding the Fortran objects is
 * linked with `--exclude-libs`, so no Fortran symbol reaches the dynamic
 * table and consumers reach the kernels only through these classes.
 *
 * All of them report `Reentrancy::ProcessSerial`: the kernels carry no
 * saved physics state, but each keeps one neighbour table in module
 * storage so vesin can reuse its buffers across calls, and that table is
 * process-global. Handing the table to the caller as an opaque handle
 * would lift them to `SharedInstance`.
 */

#include "rgpot/ForceStructs.hpp"
#include "rgpot/Potential.hpp"
#include "rgpot/pot_caps.hpp"
#include "rgpot/pot_types.hpp"

namespace rgpot {
namespace fortranpots {

#define RGPOT_FORTRAN_POT_CLASS(ClassName, PotTypeValue)                       \
  class ClassName : public Potential<ClassName> {                              \
  public:                                                                      \
    ClassName() : Potential(PotType::PotTypeValue) {}                          \
    void forceImpl(const ForceInput &in, ForceOut *out) const override;        \
    [[nodiscard]] PotCaps caps() const noexcept override {                     \
      return {.reentrancy = Reentrancy::ProcessSerial};                        \
    }                                                                          \
  }

RGPOT_FORTRAN_POT_CLASS(SWPot, SWSi);
RGPOT_FORTRAN_POT_CLASS(EDIPPot, EDIP);
RGPOT_FORTRAN_POT_CLASS(LenoskyPot, LenoskySi);
RGPOT_FORTRAN_POT_CLASS(TersoffPot, TersoffSi);
RGPOT_FORTRAN_POT_CLASS(EAMAlPot, EAMAl);
RGPOT_FORTRAN_POT_CLASS(FeHePot, FeHe);
RGPOT_FORTRAN_POT_CLASS(CuH2FortranPot, CuH2);
RGPOT_FORTRAN_POT_CLASS(WaterHPot, WaterH);

#undef RGPOT_FORTRAN_POT_CLASS

} // namespace fortranpots
} // namespace rgpot
