#include "IndexResolution.hpp"

#include <algorithm>
#include <stdexcept>

#include "LoopInduction.hpp"
#include "ScalarFormal.hpp"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

using namespace llvm;

namespace lat::index
{
namespace
{

struct IndexForm
{
  Loop * loop;
  int64_t offset;
  APInt minimum;
  APInt maximum;
};

[[noreturn]] void reject(const char * reason)
{
  throw std::invalid_argument(std::string("unsupported affine index: ") +
                              reason);
}

int64_t signedInteger(const APInt & value)
{
  if (!value.isSignedIntN(64)) reject("integer exceeds int64 range");
  return value.getSExtValue();
}

IndexForm checkRange(IndexForm form, unsigned width)
{
  if (!form.minimum.isSignedIntN(width) || !form.maximum.isSignedIntN(width) ||
      !form.minimum.isSignedIntN(64) || !form.maximum.isSignedIntN(64))
    reject("index may wrap or narrow");
  return form;
}

IndexForm resolveExpression(const SCEV * expression,
                            ScalarEvolution & evolution)
{
  if (const auto * constant = dyn_cast<SCEVConstant>(expression))
  {
    const auto value = signedInteger(constant->getAPInt());
    const APInt wide(128, static_cast<uint64_t>(value), true);
    return {nullptr, value, wide, wide};
  }
  if (const auto * cast = dyn_cast<SCEVCastExpr>(expression))
  {
    auto form = resolveExpression(cast->getOperand(), evolution);
    if (isa<SCEVZeroExtendExpr>(cast) && form.minimum.isNegative())
      reject("zero extension changes signed values");
    return checkRange(std::move(form), cast->getType()->getIntegerBitWidth());
  }
  const auto * recurrence = dyn_cast<SCEVAddRecExpr>(expression);
  if (!recurrence || !recurrence->isAffine())
    reject("expression is not a constant or bound IV plus constant");
  auto * loop = const_cast<Loop *>(recurrence->getLoop());
  const auto induction = resolveInduction(loop, evolution);
  const auto * start = dyn_cast<SCEVConstant>(recurrence->getStart());
  const auto * step =
    dyn_cast<SCEVConstant>(recurrence->getStepRecurrence(evolution));
  if (!start || !step)
    reject("index contains multiple or runtime-dependent terms");
  const auto first = signedInteger(start->getAPInt());
  if (signedInteger(step->getAPInt()) != induction.bounds.step)
    reject("index coefficient differs from emitted IV");
  const APInt wideFirst(128, static_cast<uint64_t>(first), true);
  const APInt wideStart(128, static_cast<uint64_t>(induction.bounds.start),
                        true);
  const auto difference = wideFirst - wideStart;
  const auto offset = signedInteger(difference);
  const auto last = lastInductionValue(induction.bounds) + difference;
  return checkRange({loop, offset, APIntOps::smin(wideFirst, last),
                     APIntOps::smax(wideFirst, last)},
                    expression->getType()->getIntegerBitWidth());
}

}  // namespace

std::string resolveSingleIndex(Value * value, ScalarEvolution & evolution,
                               const NameMap & names,
                               const Instruction * useSite, bool unsignedValue)
{
  if (!value->getType()->isIntegerTy()) reject("non-integer index");
  if (auto formal = resolveFormalIndex(value, names, unsignedValue))
    return *formal;
  const auto * expression = evolution.getSCEV(value);
  if (unsignedValue)
    expression = evolution.getZeroExtendExpr(
      expression, IntegerType::get(
                    value->getContext(),
                    std::max(64U, value->getType()->getIntegerBitWidth() + 1)));
  auto form = resolveExpression(expression, evolution);
  if (!form.loop) return std::to_string(form.offset);
  if (!useSite) useSite = dyn_cast<Instruction>(value);
  if (!useSite || !form.loop->contains(useSite->getParent()))
    reject("index is not bound to an emitted loop");
  auto result = inductionName(form.loop, evolution, names);
  if (form.offset > 0) result += "+";
  if (form.offset != 0) result += std::to_string(form.offset);
  return result;
}

}  // namespace lat::index
