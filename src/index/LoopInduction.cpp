#include "LoopInduction.hpp"

#include <cctype>
#include <set>
#include <stdexcept>

#include "AffineResolution.hpp"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

using namespace llvm;

namespace map::index
{
namespace
{

std::string inlineFunctionPrefix(Function & current)
{
  bool hasInline = false;
  unsigned ordinal = 0;
  std::string prefix;
  for (auto & function : *current.getParent())
  {
    if (&function == &current) prefix = "iv_f" + std::to_string(ordinal) + "_";
    hasInline |= hasFunctionAnnotation(function, "ape.inline");
    ++ordinal;
  }
  return hasInline ? prefix : "";
}

}  // namespace

PHINode * inductionVariable(const Loop * loop)
{
  const auto * branch =
    dyn_cast<BranchInst>(loop->getHeader()->getTerminator());
  const auto * comparison = branch && branch->isConditional()
                              ? dyn_cast<ICmpInst>(branch->getCondition())
                              : nullptr;
  if (!comparison) return nullptr;
  Value * variable = comparison->getOperand(0);
  if (isa<ConstantInt>(variable)) variable = comparison->getOperand(1);
  auto * phi = dyn_cast<PHINode>(variable);
  return phi && phi->getParent() == loop->getHeader() ? phi : nullptr;
}

LoopInduction resolveInduction(Loop * loop, ScalarEvolution & evolution)
{
  auto * variable = inductionVariable(loop);
  const auto * recurrence =
    variable ? dyn_cast<SCEVAddRecExpr>(evolution.getSCEV(variable)) : nullptr;
  const auto * step =
    recurrence && recurrence->isAffine()
      ? dyn_cast<SCEVConstant>(recurrence->getStepRecurrence(evolution))
      : nullptr;
  if (!recurrence || recurrence->getLoop() != loop || !step ||
      !step->getAPInt().isSignedIntN(64))
    throw std::invalid_argument("unsupported affine index: unresolved loop IV");
  const auto * start = recurrence->getStart();
  const auto form = resolveAffine(start, evolution, {}, variable);
  std::set<std::string> ancestors;
  for (auto * parent = loop->getParentLoop(); parent;
       parent = parent->getParentLoop())
    ancestors.insert(inductionName(parent, evolution, {}));
  for (const auto & term : form.terms)
    if (!ancestors.count(term.first))
      rejectAffine("loop start is not bound to an outer loop");
  checkAffineRange(form, variable->getType()->getIntegerBitWidth());
  auto [minimum, maximum] = affineRange(form);
  region::LoopBounds bounds;
  try
  {
    bounds = region::resolveLoopBounds(
      *loop, evolution,
      std::make_pair(affineInteger(minimum), affineInteger(maximum)));
  }
  catch (const std::invalid_argument & error)
  {
    throw std::invalid_argument(std::string("unsupported affine index: ") +
                                error.what());
  }
  if (minimum == maximum)
  {
    const auto last = lastInductionValue(bounds);
    minimum = APIntOps::smin(minimum, last);
    maximum = APIntOps::smax(maximum, last);
  }
  else if (bounds.step > 0 && minimum.slt(affineWide(bounds.bound)))
    maximum = APIntOps::smax(maximum, affineWide(bounds.bound) - 1);
  else if (bounds.step < 0 && maximum.sgt(affineWide(bounds.bound)))
    minimum = APIntOps::smin(minimum, affineWide(bounds.bound) + 1);
  return {variable, bounds, start, minimum, maximum};
}

APInt lastInductionValue(const region::LoopBounds & bounds)
{
  const APInt start(128, static_cast<uint64_t>(bounds.start), true);
  const APInt bound(128, static_cast<uint64_t>(bounds.bound), true);
  const APInt step(128, static_cast<uint64_t>(bounds.step), true);
  const bool increasing = bounds.step > 0;
  if ((increasing && start.sge(bound)) || (!increasing && start.sle(bound)))
    return start;
  const auto distance = increasing ? bound - start : start - bound;
  const auto magnitude = increasing ? step : -step;
  const auto count = (distance + magnitude - 1).udiv(magnitude);
  return start + (count - 1) * step;
}

std::string inductionName(Loop * loop, ScalarEvolution & evolution,
                          const NameMap & names)
{
  auto * variable = inductionVariable(loop);
  if (!variable)
    throw std::invalid_argument("unsupported affine index: unresolved loop IV");
  auto name = getValueName(variable, names);
  if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front())))
    name = "iv_" + name;
  for (char & character : name)
    if (!std::isalnum(static_cast<unsigned char>(character)) &&
        character != '_')
      character = '_';
  auto & current = *variable->getFunction();
  const auto prefix = inlineFunctionPrefix(current);
  name = prefix + name;
  std::set<std::string> ancestors;
  // Inline actuals can refer to formals from any caller in the module.
  for (auto & function : *current.getParent())
  {
    if (prefix.empty() && &function != &current) continue;
    const auto argumentNames =
      &function == &current ? names : buildDebugNameMap(function);
    for (auto & argument : function.args())
      ancestors.insert(getValueName(&argument, argumentNames));
  }
  for (auto * parent = loop->getParentLoop(); parent;
       parent = parent->getParentLoop())
    ancestors.insert(inductionName(parent, evolution, names));
  while (ancestors.count(name)) name += "_";
  return name;
}

}  // namespace map::index
