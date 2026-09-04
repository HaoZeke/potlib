// MIT License
// Copyright 2023--present rgpot developers

#include "rgpot/ExprPot/ExprPot.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "Lepton.h"
#include "lepton/Exception.h"

#include "rgpot/ParamHash.hpp"
#include "rgpot/types/AtomMatrix.hpp"

namespace rgpot {
namespace {

bool isAsciiIdentChar(char c, bool first) noexcept {
  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
    return true;
  }
  return !first && (c >= '0' && c <= '9');
}

bool isLeptonIdentifier(std::string_view name) noexcept {
  if (name.empty() || !isAsciiIdentChar(name[0], true)) {
    return false;
  }
  for (size_t i = 1; i < name.size(); ++i) {
    if (!isAsciiIdentChar(name[i], false)) {
      return false;
    }
  }
  return true;
}

bool isBlank(std::string_view s) noexcept {
  for (char c : s) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

Reentrancy worseReentrancy(Reentrancy a, Reentrancy b) noexcept {
  return static_cast<Reentrancy>(
      std::max(static_cast<uint8_t>(a), static_cast<uint8_t>(b)));
}

} // namespace

struct ExprPot::Impl {
  struct NamedChild {
    std::string name;
    std::unique_ptr<PotentialBase> child;
    double *var = nullptr;
  };

  std::string expression;
  std::vector<NamedChild> terms;
  Lepton::CompiledExpression energy;
  uint64_t paramsKey = 0;
  PotCaps caps{};

  void bindVars() {
    for (auto &term : terms) {
      term.var = &energy.getVariableReference(term.name);
    }
  }
};

ExprPot::ExprPot(std::string expression, std::vector<Term> terms)
    : Potential(PotType::Expr), m_impl(std::make_unique<Impl>()) {
  if (isBlank(expression)) {
    throw std::invalid_argument("ExprPot expression is empty");
  }
  if (terms.empty()) {
    throw std::invalid_argument("ExprPot has zero terms");
  }

  std::set<std::string> seen;
  m_impl->terms.reserve(terms.size());
  for (auto &term : terms) {
    if (!isLeptonIdentifier(term.first)) {
      throw std::invalid_argument("ExprPot name '" + term.first +
                                  "' is not a Lepton identifier");
    }
    if (!seen.insert(term.first).second) {
      throw std::invalid_argument("ExprPot duplicate name '" + term.first +
                                  "'");
    }
    if (!term.second) {
      throw std::invalid_argument("ExprPot term '" + term.first + "' is null");
    }
    m_impl->terms.push_back(Impl::NamedChild{std::move(term.first),
                                             std::move(term.second), nullptr});
  }

  Lepton::ParsedExpression parsed;
  try {
    parsed = Lepton::Parser::parse(expression);
  } catch (const Lepton::Exception &ex) {
    throw std::invalid_argument(std::string("ExprPot parse failed: ") +
                                ex.what());
  }

  try {
    m_impl->energy = parsed.createCompiledExpression();
  } catch (const Lepton::Exception &ex) {
    throw std::invalid_argument(std::string("ExprPot compile failed: ") +
                                ex.what());
  }

  const std::set<std::string> &vars = m_impl->energy.getVariables();
  for (const auto &term : m_impl->terms) {
    if (vars.find(term.name) == vars.end()) {
      throw std::invalid_argument("ExprPot name '" + term.name +
                                  "' is missing from the expression");
    }
  }
  for (const auto &var : vars) {
    if (seen.find(var) == seen.end()) {
      throw std::invalid_argument("ExprPot identifier '" + var +
                                  "' has no term");
    }
  }

  m_impl->bindVars();
  m_impl->expression = std::move(expression);

  Fnv1a fp;
  fp.u64(/*kKernelVersion=*/1);
  fp.str(m_impl->expression);
  PotCaps caps;
  caps.reentrancy = Reentrancy::PerInstance;
  caps.batched = false;
  caps.periodic = true;
  for (const auto &term : m_impl->terms) {
    fp.str(term.name);
    fp.u64(term.child->paramsKey());
    const PotCaps child = term.child->caps();
    caps.reentrancy = worseReentrancy(caps.reentrancy, child.reentrancy);
    caps.periodic = caps.periodic && child.periodic;
    caps.perImageInstances =
        caps.perImageInstances || child.perImageInstances;
  }
  m_impl->paramsKey = fp.h;
  m_impl->caps = caps;
}

ExprPot::~ExprPot() = default;

ExprPot::ExprPot(ExprPot &&other) noexcept
    : Potential(PotType::Expr), m_impl(std::move(other.m_impl)) {}

ExprPot &ExprPot::operator=(ExprPot &&other) noexcept {
  if (this != &other) {
    m_impl = std::move(other.m_impl);
  }
  return *this;
}

void ExprPot::forceImpl(const ForceInput &in, ForceOut *out) const {
  const size_t n3 = 3 * in.nAtoms;
  if (out->F != nullptr) {
    for (size_t i = 0; i < n3; ++i) {
      out->F[i] = 0.0;
    }
  }
  out->energy = 0.0;
  out->variance = 0.0;

  types::AtomMatrix positions(in.nAtoms, 3);
  if (in.nAtoms > 0 && in.pos != nullptr) {
    std::memcpy(positions.data(), in.pos, n3 * sizeof(double));
  }
  std::vector<int> atmtypes;
  if (in.atmnrs != nullptr) {
    atmtypes.assign(in.atmnrs, in.atmnrs + static_cast<std::ptrdiff_t>(in.nAtoms));
  } else {
    atmtypes.assign(in.nAtoms, 0);
  }
  std::array<std::array<double, 3>, 3> box{};
  if (in.box != nullptr) {
    std::memcpy(&box, in.box, sizeof(box));
  }

  for (auto &term : m_impl->terms) {
    auto [energy, forces, variance] =
        (*term.child)(positions, atmtypes, box);
    (void)forces;
    (void)variance;
    *term.var = energy;
  }
  out->energy = m_impl->energy.evaluate();
}

PotCaps ExprPot::caps() const noexcept { return m_impl->caps; }

uint64_t ExprPot::paramsKey() const noexcept { return m_impl->paramsKey; }

const std::string &ExprPot::expression() const noexcept {
  return m_impl->expression;
}

} // namespace rgpot
