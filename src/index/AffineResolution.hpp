#pragma once

#include "AffineExpression.hpp"

namespace map::index
{

/**
 * @brief Prove a restricted SCEV expression relative to emitted loop IVs.
 * @param expression Non-null borrowed integer SCEV from evolution.
 * @param evolution Borrowed ScalarEvolution for the current function.
 * @param names Borrowed debug names; these never establish value equivalence.
 * @param useSite Borrowed consuming instruction, nullable for constant/formal
 * expressions.
 * @return Affine expression and proved domains after checking every loop use.
 * @throws std::invalid_argument For nonlinear, runtime or unproved arithmetic.
 */
AffineExpression resolveAffine(const llvm::SCEV * expression,
                               llvm::ScalarEvolution & evolution,
                               const NameMap & names,
                               const llvm::Instruction * useSite);

}  // namespace map::index
