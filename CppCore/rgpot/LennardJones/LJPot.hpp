#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief Header file for the Lennard-Jones potential class.
 *
 * This file defines the @c LJPot class, which implements a standard  12-6
 * Lennard-Jones potential with a shifted cutoff for use in  atomic simulations.
 */

// clang-format off
#include <cmath>
#include <utility>
#include <vector>
#include <stdexcept>
// clang-format on
#include "rgpot/Potential.hpp"
#include "rgpot/types/AtomMatrix.hpp"
using rgpot::types::AtomMatrix;

namespace rgpot {

/**
 * @class LJPot
 * @brief Implementation of a shifted 12-6 Lennard-Jones potential.
 * @ingroup rgpot_potentials
 */
class LJPot : public Potential<LJPot> {
public:
  /**
   * @brief Constructs the potential from well depth, cutoff and length scale.
   * @param u0_ Well depth.
   * @param cuttOffR_ Distance beyond which the potential is truncated.
   * @param psi_ Distance at which the inter-particle potential is zero.
   *
   * @c cuttOffU is the potential evaluated at @c cuttOffR_ and is subtracted
   * from every pair term, so the pair potential vanishes continuously at the
   * cutoff. Members initialize in declaration order, so @c cuttOffU may read
   * the three parameters above it.
   */
  LJPot(double u0_, double cuttOffR_, double psi_)
      : Potential(PotType::LJ), u0{u0_}, cuttOffR{cuttOffR_}, psi{psi_},
        cuttOffU{4 * u0 *
                 (std::pow(psi / cuttOffR, 12) - std::pow(psi / cuttOffR, 6))} {
  }

  /**
   * @brief Default constructor initializing parameters.
   */
  LJPot() : LJPot(1.0, 15.0, 1.0) {}

  /**
   * @brief Computes the forces and energy for a given configuration.
   * @param in Structure containing coordinates and cell info.
   * @param out Pointer to the results structure.
   * @return Void.
   */
  void forceImpl(const ForceInput &in, ForceOut *out) const override;

private:
  double u0;       //!< Well depth parameter.
  double cuttOffR; //!< Distance beyond which potential is truncated.
  double psi;      //!< Distance at which the inter-particle potential is zero.
  double cuttOffU; //!< Potential energy value at the cutoff distance.
};

} // namespace rgpot
