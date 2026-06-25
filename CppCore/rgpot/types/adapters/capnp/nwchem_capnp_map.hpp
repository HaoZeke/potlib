#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief rgpot user params: PotentialConfig (Cap'n Proto) → embed RgpotNWChemParams.
 *
 * No intermediate C++ config type. Future MetatomicParams/XTBParams get siblings.
 */

#include <capnp/message.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#include "rgpot/NWChemPot/NWChemPot.hpp"
#include "rgpot/NWChemPot/nwchem_c_abi.h"
#include "rgpot/rpc/Potentials.capnp.h"

namespace rgpot {
namespace types {
namespace adapt {
namespace capnp {

namespace detail {
inline void copy_cstr(char *dst, size_t n, const char *src) {
  if (!dst || n == 0)
    return;
  if (!src || !src[0]) {
    dst[0] = '\0';
    return;
  }
  std::snprintf(dst, n, "%s", src);
}
} // namespace detail

/** Cap'n Proto NWChemParams → embed-boundary POD (only hop on user path). */
inline void nwchemParamsToAbi(const ::NWChemParams::Reader &p,
                              RgpotNWChemParams *out) {
  if (!out)
    return;
  std::memset(out, 0, sizeof(*out));
  detail::copy_cstr(out->basis, sizeof(out->basis), p.getBasis().cStr());
  detail::copy_cstr(out->theory, sizeof(out->theory), p.getTheory().cStr());
  detail::copy_cstr(out->scf_type, sizeof(out->scf_type), p.getScfType().cStr());
  out->charge = p.getCharge();
  out->multiplicity = p.getMultiplicity();
  detail::copy_cstr(out->engine_path, sizeof(out->engine_path),
                    p.getEnginePath().cStr());
  detail::copy_cstr(out->nwchem_root, sizeof(out->nwchem_root),
                    p.getNwchemRoot().cStr());
}

/** Embed POD → Cap'n Proto out (getParams / roundtrip). */
inline void nwchemAbiToParams(const RgpotNWChemParams &p,
                              ::NWChemParams::Builder b) {
  b.setBasis(p.basis);
  b.setTheory(p.theory);
  b.setScfType(p.scf_type);
  b.setCharge(p.charge);
  b.setMultiplicity(p.multiplicity);
  b.setEnginePath(p.engine_path);
  b.setNwchemRoot(p.nwchem_root);
}

/** Schema defaults into embed POD (empty NWChemParams reader / default ctor). */
inline void nwchemAbiDefaults(RgpotNWChemParams *out) {
  ::capnp::MallocMessageBuilder msg;
  auto root = msg.initRoot<::NWChemParams>();
  nwchemParamsToAbi(root.asReader(), out);
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

inline std::string nwchemAbiSummary(const RgpotNWChemParams &p) {
  std::ostringstream os;
  os << "basis=" << p.basis << " theory=" << p.theory
     << " scfType=" << p.scf_type << " charge=" << p.charge
     << " mult=" << p.multiplicity;
  if (p.engine_path[0])
    os << " enginePath=" << p.engine_path;
  if (p.nwchem_root[0])
    os << " nwchemRoot=" << p.nwchem_root;
  return os.str();
}

/**
 * Apply rgpot PotentialConfig to NWChemPot (only none | nwchem arms valid here).
 */
inline bool applyPotentialConfig(NWChemPot &pot,
                                 const ::PotentialConfig::Reader &cfg,
                                 std::string *message_out = nullptr) {
  switch (cfg.which()) {
  case ::PotentialConfig::NONE:
    if (message_out)
      *message_out = "no-op (rgpot params: none)";
    return true;
  case ::PotentialConfig::NWCHEM: {
    const auto nw = cfg.getNwchem();
    const bool ok = pot.setParams(nw);
    if (message_out) {
      const std::string sum = nwchemParamsSummary(nw);
      if (ok)
        *message_out = "rgpot params applied (nwchem arm): " + sum;
      else if (!pot.available())
        *message_out =
            "rgpot params nwchem arm failed (engine not loaded): " + sum;
      else
        *message_out = "rgpot params nwchem arm rejected by embed: " + sum;
    }
    return ok;
  }
  default:
    if (message_out)
      *message_out =
          "rgpot params arm not handled by NWChemPot (use matching backend pot)";
    return false;
  }
}

} // namespace capnp
} // namespace adapt
} // namespace types
} // namespace rgpot
