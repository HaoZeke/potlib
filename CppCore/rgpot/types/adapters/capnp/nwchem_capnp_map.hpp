#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief rgpot user params: PotentialConfig (Cap'n Proto), NWChem arm only here.
 *
 * Top-level user contract is PotentialConfig (extensible union). NWChemParams is
 * one backend payload. This adapter maps nwchem arm <-> internal NWChemConfig
 * <-> embed buffers. Future MetatomicParams/XTBParams get sibling adapters.
 */

#include <sstream>
#include <string>

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/rpc/Potentials.capnp.h"

namespace rgpot {
namespace types {
namespace adapt {
namespace capnp {

/** User NWChemParams (Cap'n Proto) -> internal mirror. */
inline NWChemConfig nwchemConfigFromCapnp(const ::NWChemParams::Reader &p) {
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

/** Internal mirror -> user NWChemParams (Cap'n Proto out). */
inline void nwchemConfigToCapnp(::NWChemParams::Builder b,
                                const NWChemConfig &cfg) {
  b.setBasis(cfg.basis);
  b.setTheory(cfg.theory);
  b.setScfType(cfg.scf_type);
  b.setCharge(cfg.charge);
  b.setMultiplicity(cfg.multiplicity);
  b.setEnginePath(cfg.engine_path);
  b.setNwchemRoot(cfg.nwchem_root);
}

inline std::string nwchemParamsSummary(const ::NWChemParams::Reader &p) {
  std::ostringstream os;
  os << "basis=" << p.getBasis().cStr() << " theory=" << p.getTheory().cStr()
     << " scfType=" << p.getScfType().cStr() << " charge=" << p.getCharge()
     << " mult=" << p.getMultiplicity();
  if (p.getEnginePath().size() > 0)
    os << " enginePath=" << p.getEnginePath().cStr();
  if (p.getNwchemRoot().size() > 0)
    os << " nwchemRoot=" << p.getNwchemRoot().cStr();
  return os.str();
}

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
 * Apply PotentialConfig using Cap'n Proto as the only user option carrier.
 * nwchem arm: setParams(reader) on NWChemPot.
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
    const auto nw = cfg.getNwchem();
    const bool ok = pot.setParams(nw);
    if (message_out) {
      const std::string sum = nwchemParamsSummary(nw);
      if (ok)
        *message_out = "nwchem params applied (Cap'n Proto): " + sum;
      else if (!pot.available())
        *message_out =
            "nwchem setParams failed (engine not loaded): " + sum;
      else
        *message_out = "nwchem setParams rejected by embed: " + sum;
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
