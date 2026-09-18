#include "AffineResolution.hpp"

#include "ScalarFormal.hpp"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

using namespace llvm;

namespace map::index
{
namespace
{

AffineExpression recurrenceForm(const SCEVAddRecExpr & recurrence,
                                ScalarEvolution & evolution,
                                const NameMap & names,
                                const Instruction * useSite)
{
  if (!recurrence.isAffine()) rejectAffine("nonlinear recurrence");
  auto * loop = const_cast<Loop *>(recurrence.getLoop());
  // Validate before normalization can merge identically named sibling IVs.
  if (!useSite || !loop->contains(useSite->getParent()))
    rejectAffine("index is not bound to an emitted loop");
  const auto induction = resolveInduction(loop, evolution);
  const auto * step =
    dyn_cast<SCEVConstant>(recurrence.getStepRecurrence(evolution));
  if (!step) rejectAffine("runtime-dependent coefficient");
  const auto delta = affineWide(affineInteger(step->getAPInt()));
  const auto stride = affineWide(induction.bounds.step);
  if (stride.isZero() || !delta.srem(stride).isZero())
    rejectAffine("nonintegral emitted IV coefficient");
  const auto coefficient = affineInteger(delta.sdiv(stride));
  auto form = resolveAffine(recurrence.getStart(), evolution, names, useSite);
  const auto first = affineWide(induction.bounds.start);
  const auto last = lastInductionValue(induction.bounds);
  AffineExpression variable;
  variable.terms.emplace(
    inductionName(loop, evolution, names),
    AffineTerm{1, APIntOps::smin(first, last), APIntOps::smax(first, last)});
  addAffine(form, variable, coefficient);
  form.constant =
    affineInteger(affineWide(form.constant) - affineWide(coefficient) * first);
  return form;
}

AffineExpression castForm(const SCEVIntegralCastExpr & cast,
                          ScalarEvolution & evolution, const NameMap & names,
                          const Instruction * useSite)
{
  auto form = resolveAffine(cast.getOperand(), evolution, names, useSite);
  const auto [minimum, maximum] = affineRange(form);
  const auto sourceWidth = cast.getOperand()->getType()->getIntegerBitWidth();
  if (isa<SCEVZeroExtendExpr>(cast))
  {
    if (minimum.isNegative() || !maximum.isIntN(sourceWidth))
      rejectAffine("zero extension changes signed values");
  }
  else if (isa<SCEVSignExtendExpr>(cast) &&
           (!minimum.isSignedIntN(sourceWidth) ||
            !maximum.isSignedIntN(sourceWidth)))
    rejectAffine("sign extension changes unsigned values");
  return form;
}

}  // namespace

AffineExpression resolveAffine(const SCEV * expression,
                               ScalarEvolution & evolution,
                               const NameMap & names,
                               const Instruction * useSite)
{
  if (const auto * constant = dyn_cast<SCEVConstant>(expression))
    return {affineInteger(constant->getAPInt()), {}};
  if (const auto * unknown = dyn_cast<SCEVUnknown>(expression))
  {
    const auto formal = resolveFormalValue(unknown->getValue(), names);
    if (!formal) rejectAffine("unbound runtime value");
    return {0, {{formal->name, {1, formal->minimum, formal->maximum}}}};
  }
  AffineExpression form;
  if (const auto * cast = dyn_cast<SCEVIntegralCastExpr>(expression))
    form = castForm(*cast, evolution, names, useSite);
  else if (const auto * recurrence = dyn_cast<SCEVAddRecExpr>(expression))
    form = recurrenceForm(*recurrence, evolution, names, useSite);
  else if (const auto * sum = dyn_cast<SCEVAddExpr>(expression))
  {
    for (const auto * operand : sum->operands())
      addAffine(form, resolveAffine(operand, evolution, names, useSite));
  }
  else if (const auto * product = dyn_cast<SCEVMulExpr>(expression))
  {
    int64_t coefficient = 1;
    const SCEV * variable = nullptr;
    for (const auto * operand : product->operands())
    {
      if (const auto * constant = dyn_cast<SCEVConstant>(operand))
        coefficient =
          affineInteger(affineWide(coefficient) *
                        affineWide(affineInteger(constant->getAPInt())));
      else
      {
        if (variable) rejectAffine("nonlinear or runtime-dependent product");
        variable = operand;
      }
    }
    if (variable)
      addAffine(form, resolveAffine(variable, evolution, names, useSite),
                coefficient);
    else
      form.constant = coefficient;
  }
  else
    rejectAffine("expression is outside the affine grammar");
  checkAffineRange(form, expression->getType()->getIntegerBitWidth());
  return form;
}

}  // namespace map::index
