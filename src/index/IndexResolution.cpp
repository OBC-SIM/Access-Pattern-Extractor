#include "IndexResolution.hpp"

#include <algorithm>

#include "AffineResolution.hpp"
#include "ScalarFormal.hpp"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

using namespace llvm;

namespace map::index
{

std::string resolveSingleIndex(Value * value, ScalarEvolution & evolution,
                               const NameMap & names,
                               const Instruction * useSite, bool unsignedValue)
{
  if (!value->getType()->isIntegerTy()) rejectAffine("non-integer index");
  if (auto formal = resolveFormalIndex(value, names, unsignedValue))
    return *formal;
  const auto * expression = evolution.getSCEV(value);
  if (unsignedValue)
    expression = evolution.getZeroExtendExpr(
      expression, IntegerType::get(
                    value->getContext(),
                    std::max(64U, value->getType()->getIntegerBitWidth() + 1)));
  if (!useSite) useSite = dyn_cast<Instruction>(value);
  const auto form = resolveAffine(expression, evolution, names, useSite);
  checkAffineRange(form, expression->getType()->getIntegerBitWidth());
  return formatAffine(form);
}

}  // namespace map::index
