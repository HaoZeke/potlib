// MIT License
// Copyright 2023--present rgpot developers

#include "rgpot/ExprPot/ExprPot.hpp"

#include <array>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rgpot/ParamHash.hpp"

namespace rgpot {
namespace {

bool isLeptonIdentifier(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  const unsigned char lead = static_cast<unsigned char>(name[0]);
  if (!(std::isalpha(lead) != 0 || lead == '_')) {
    return false;
  }
  for (std::size_t i = 1; i < name.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    if (!(std::isalnum(c) != 0 || c == '_')) {
      return false;
    }
  }
  return true;
}

[[noreturn]] void failClosed(const std::string &why) {
  throw std::invalid_argument("ExprPot: " + why);
}

} // namespace

ExprPot::ExprPot(std::string expression, std::vector<ExprTerm> terms)
    : Potential(PotType::Expr), m_expression(std::move(expression)) {
  if (m_expression.empty()) {
    failClosed("empty expression");
  }
  if (terms.empty()) {
    failClosed("zero terms");
  }

  std::unordered_set<std::string> names;
  names.reserve(terms.size());
  m_terms.reserve(terms.size());
  for (auto &term : terms) {
    if (!term.second) {
      failClosed("null term '" + term.first + "'");
    }
    if (!isLeptonIdentifier(term.first)) {
      failClosed("name '" + term.first +
                 "' is not a Lepton identifier [A-Za-z_][A-Za-z0-9_]*");
    }
    if (!names.insert(term.first).second) {
      failClosed("duplicate name '" + term.first + "'");
    }
    m_terms.push_back(
        Term{std::move(term.first), std::move(term.second), nullptr});
  }

  Lepton::ParsedExpression parsed;
  try {
    parsed = Lepton::Parser::parse(m_expression);
    m_energy = parsed.createCompiledExpression();
  } catch (const Lepton::Exception &ex) {
    failClosed(std::string("parse failed: ") + ex.what());
  }

  const auto &vars = m_energy.getVariables();
  for (const auto &term : m_terms) {
    if (vars.find(term.name) == vars.end()) {
      failClosed("name '" + term.name + "' missing from the expression");
    }
  }
  for (const auto &var : vars) {
    if (names.find(var) == names.end()) {
      failClosed("identifier '" + var + "' in the expression has no term");
    }
  }

  for (auto &term : m_terms) {
    term.energyRef = &m_energy.getVariableReference(term.name);
  }

  // CompiledExpression is not thread-safe, so reentrancy is at least
  // PerInstance and otherwise the worst of the children. periodic is the
  // AND of the children. batched is false.
  Reentrancy reentrancy = Reentrancy::PerInstance;
  bool periodic = true;
  Fnv1a fp;
  fp.u64(kKernelVersion);
  fp.str(m_expression);
  for (const auto &term : m_terms) {
    const PotCaps childCaps = term.pot->caps();
    if (static_cast<uint8_t>(childCaps.reentrancy) >
        static_cast<uint8_t>(reentrancy)) {
      reentrancy = childCaps.reentrancy;
    }
    periodic = periodic && childCaps.periodic;
    fp.str(term.name);
    fp.u64(term.pot->paramsKey());
  }
  m_caps = PotCaps{.reentrancy = reentrancy,
                   .perImageInstances = false,
                   .batched = false,
                   .periodic = periodic};
  m_paramsKey = fp.h;
}

void ExprPot::forceImpl(const ForceInput &in, ForceOut *out) const {
  if (out == nullptr) {
    return;
  }
  out->energy = 0.0;
  out->variance = 0.0;
  if (out->F != nullptr && in.nAtoms > 0) {
    std::memset(out->F, 0, in.nAtoms * 3 * sizeof(double));
  }

  types::AtomMatrix positions(in.nAtoms, 3);
  if (in.nAtoms > 0 && in.pos != nullptr) {
    std::memcpy(positions.data(), in.pos, in.nAtoms * 3 * sizeof(double));
  }
  std::vector<int> atmtypes;
  if (in.nAtoms > 0 && in.atmnrs != nullptr) {
    atmtypes.assign(in.atmnrs,
                    in.atmnrs + static_cast<std::ptrdiff_t>(in.nAtoms));
  }
  std::array<std::array<double, 3>, 3> box{};
  if (in.box != nullptr) {
    std::memcpy(static_cast<void *>(&box), in.box, sizeof(box));
  }

  for (auto &term : m_terms) {
    auto [energy, forces, variance] = (*term.pot)(positions, atmtypes, box);
    (void)forces;
    (void)variance;
    *term.energyRef = energy;
  }
  out->energy = m_energy.evaluate();
}

} // namespace rgpot
