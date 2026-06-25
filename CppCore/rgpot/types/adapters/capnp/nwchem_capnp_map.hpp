#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief Optional RPC shim only: Cap'n Proto NWChemParams <-> NWChemConfig.
 *
 * Primary option path is C ABI RgpotNWChemParams via NWChemPot::setConfig /
 * toAbiParams (no RPC). This adapter exists solely for potserv configure().
 */

#include <sstream>
#include <string>

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/rpc/Potentials.capnp.h"

namespace rgpot {
namespace types {
namespace adapt {
namespace capnp {

/** Cap'n Proto NWChemParams -> C++ NWChemConfig (all fields). */
inline NWChemConfig nwchemConfigFromCapnp(const NWChemParams::Reader &p) {
  NWChemConfig cfg;
  // NWChemParams.basis        -> cfg.basis
  cfg.basis = p.getBasis().cStr();
  // NWChemParams.theory       -> cfg.theory
  cfg.theory = p.getTheory().cStr();
  // NWChemParams.scfType      -> cfg.scf_type
  cfg.scf_type = p.getScfType().cStr();
  // NWChemParams.charge       -> cfg.charge
  cfg.charge = p.getCharge();
  // NWChemParams.multiplicity -> cfg.multiplicity
  cfg.multiplicity = p.getMultiplicity();
  // NWChemParams.enginePath   -> cfg.engine_path (dlopen target)
  cfg.engine_path = p.getEnginePath().cStr();
  // NWChemParams.nwchemRoot   -> cfg.nwchem_root (NWCHEM_TOP)
  cfg.nwchem_root = p.getNwchemRoot().cStr();
  return cfg;
}

/** C++ NWChemConfig -> Cap'n Proto NWChemParams (all fields). */
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

/** Human-readable summary of applied NWChemConfig (for configure() message). */
inline std::string nwchemConfigSummary(const NWChemConfig &cfg) {
  std::ostringstream os;
  os << "basis=" << cfg.basis << " theory=" << cfg.theory
     << " scfType=" << cfg.scf_type << " charge=" << cfg.charge
     << " mult=" << cfg.multiplicity;
  if (!cfg.engine_path.empty())
    os << " enginePath=" << cfg.engine_path;
  if (!cfg.nwchem_root.empty())
    os << " nwchemRoot=" << cfg.nwchem_root;
  return os.str();
}

/**
 * Apply PotentialConfig to NWChemPot.
 * - none: success, no change
 * - nwchem: full NWChemParams -> NWChemConfig -> setConfig (all options mapped)
 */
inline bool applyPotentialConfig(NWChemPot &pot,
                                 const PotentialConfig::Reader &cfg,
                                 std::string *message_out = nullptr) {
  switch (cfg.which()) {
  case PotentialConfig::NONE:
    if (message_out)
      *message_out = "no-op (PotentialConfig.none)";
    return true;
  case PotentialConfig::NWCHEM: {
    const NWChemConfig nc = nwchemConfigFromCapnp(cfg.getNwchem());
    const bool ok = pot.setConfig(nc);
    if (message_out) {
      if (ok)
        *message_out = "nwchem config applied: " + nwchemConfigSummary(nc);
      else if (!pot.available())
        *message_out =
            "nwchem setConfig failed (engine not loaded): " +
            nwchemConfigSummary(nc);
      else
        *message_out =
            "nwchem setConfig rejected by engine ABI: " + nwchemConfigSummary(nc);
    }
    return ok;
  }
  default:
    if (message_out)
      *message_out = "unsupported PotentialConfig arm (expected none|nwchem)";
    return false;
  }
}

} // namespace capnp
} // namespace adapt
} // namespace types
} // namespace rgpot
