#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief Map Cap'n Proto NWChemParams / PotentialConfig to rgpot::NWChemConfig.
 */

#include <string>

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/rpc/Potentials.capnp.h"

namespace rgpot {
namespace types {
namespace adapt {
namespace capnp {

/**
 * @brief Convert an NWChemParams reader into a native NWChemConfig.
 */
inline NWChemConfig
nwchemConfigFromCapnp(const NWChemParams::Reader &p) {
  NWChemConfig cfg;
  cfg.basis = p.getBasis().cStr();
  cfg.theory = p.getTheory().cStr();
  cfg.scf_type = p.getScfType().cStr();
  cfg.charge = p.getCharge();
  cfg.multiplicity = p.getMultiplicity();
  cfg.engine_path = p.getEnginePath().cStr();
  cfg.nwchem_root = p.getNwchemRoot().cStr();
  return cfg;
}

/**
 * @brief Populate an NWChemParams builder from a native NWChemConfig.
 */
inline void nwchemConfigToCapnp(NWChemParams::Builder b,
                                const NWChemConfig &cfg) {
  b.setBasis(cfg.basis);
  b.setTheory(cfg.theory);
  b.setScfType(cfg.scf_type);
  b.setCharge(cfg.charge);
  b.setMultiplicity(cfg.multiplicity);
  b.setEnginePath(cfg.engine_path);
  b.setNwchemRoot(cfg.nwchem_root);
}

/**
 * @brief Apply PotentialConfig to an NWChemPot instance if the union is nwchem.
 * @return true if config applied (or was none); false if setConfig failed.
 */
inline bool applyPotentialConfig(NWChemPot &pot,
                                 const PotentialConfig::Reader &cfg,
                                 std::string *message_out = nullptr) {
  switch (cfg.which()) {
  case PotentialConfig::NONE:
    if (message_out)
      *message_out = "no-op";
    return true;
  case PotentialConfig::NWCHEM: {
    auto nc = nwchemConfigFromCapnp(cfg.getNwchem());
    bool ok = pot.setConfig(nc);
    if (message_out)
      *message_out = ok ? "nwchem config applied" : "nwchem setConfig failed";
    return ok;
  }
  default:
    if (message_out)
      *message_out = "unsupported PotentialConfig arm";
    return false;
  }
}

} // namespace capnp
} // namespace adapt
} // namespace types
} // namespace rgpot
