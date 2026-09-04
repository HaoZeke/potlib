#pragma once
// MIT License
// Copyright 2023--present rgpot developers

/**
 * @brief Named-child PES algebra compiled with OpenMM Lepton.
 *
 * @c ExprPot is a @c Potential whose energy is a scalar expression over
 * named child potentials. Children stay owned @c unique_ptr<PotentialBase>.
 * The constructor accepts those potentials only.
 */

#ifndef RGPOT_HAS_EXPR
#error "ExprPot requires meson -Dwith_expr=true"
#endif

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Lepton.h"
#include "rgpot/Potential.hpp"

namespace rgpot {

/**
 * One named child of an @c ExprPot. The name is the Lepton identifier
 * bound to that child's energy.
 */
using ExprTerm = std::pair<std::string, std::unique_ptr<PotentialBase>>;

/**
 * @class ExprPot
 * @brief Expression over named @c PotentialBase children.
 * @ingroup rgpot_potentials
 *
 * Constructor parses @p expression with @c Lepton::Parser::parse, compiles
 * it once, and binds each term name through
 * @c CompiledExpression::getVariableReference. Unknown, duplicate, unused,
 * or illegal names fail closed at construct. Energy is evaluated from the
 * compiled expression; forces are left at zero.
 */
class ExprPot : public Potential<ExprPot> {
public:
  ExprPot(std::string expression, std::vector<ExprTerm> terms);

  ExprPot(const ExprPot &) = delete;
  ExprPot &operator=(const ExprPot &) = delete;
  ExprPot(ExprPot &&) = delete;
  ExprPot &operator=(ExprPot &&) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

  [[nodiscard]] PotCaps caps() const noexcept override { return m_caps; }

  [[nodiscard]] uint64_t paramsKey() const noexcept override {
    return m_paramsKey;
  }

  [[nodiscard]] const std::string &expression() const noexcept {
    return m_expression;
  }

  [[nodiscard]] std::size_t nTerms() const noexcept { return m_terms.size(); }

private:
  static constexpr uint64_t kKernelVersion = 1;

  struct Term {
    std::string name;
    std::unique_ptr<PotentialBase> pot;
    double *energyRef = nullptr;
  };

  std::string m_expression;
  mutable std::vector<Term> m_terms;
  mutable Lepton::CompiledExpression m_energy;
  PotCaps m_caps{};
  uint64_t m_paramsKey{0};
};

} // namespace rgpot
