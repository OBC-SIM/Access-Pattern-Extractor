#include "RegionLoopBounds.hpp"

#include <stdexcept>

#include "llvm/Analysis/ScalarEvolutionExpressions.h"

using namespace llvm;

namespace map::region
{
namespace
{

void validateInductionRange(const LoopBounds & bounds, unsigned width,
                            bool unsignedType)
{
  const APInt start(128, static_cast<std::uint64_t>(bounds.start), true);
  const APInt bound(128, static_cast<std::uint64_t>(bounds.bound), true);
  const APInt step(128, static_cast<std::uint64_t>(bounds.step), true);
  const bool ascending = bounds.step > 0;
  if ((ascending && start.sge(bound)) || (!ascending && start.sle(bound)))
    return;
  const auto distance = ascending ? bound - start : start - bound;
  const auto magnitude = ascending ? step : -step;
  const auto count = (distance + magnitude - 1).udiv(magnitude);
  const auto afterLast = start + count * step;
  if (unsignedType ? (afterLast.isNegative() || !afterLast.isIntN(width))
                   : !afterLast.isSignedIntN(width))
    throw std::invalid_argument("region loop induction overflows its type");
}

}  // namespace

LoopBounds resolveLoopBounds(
  Loop & loop, ScalarEvolution & evolution,
  std::optional<std::pair<std::int64_t, std::int64_t>> startRange)
{
  auto * branch = dyn_cast<BranchInst>(loop.getHeader()->getTerminator());
  auto * comparison = branch && branch->isConditional()
                        ? dyn_cast<ICmpInst>(branch->getCondition())
                        : nullptr;
  if (!comparison || loop.contains(branch->getSuccessor(0)) ==
                       loop.contains(branch->getSuccessor(1)))
    throw std::invalid_argument("unresolved or unsupported region loop bound");
  auto predicate = comparison->getPredicate();
  Value * variable = comparison->getOperand(0);
  Value * limit = comparison->getOperand(1);
  if (isa<ConstantInt>(variable))
  {
    std::swap(variable, limit);
    predicate = ICmpInst::getSwappedPredicate(predicate);
  }
  if (!loop.contains(branch->getSuccessor(0)))
    predicate = ICmpInst::getInversePredicate(predicate);
  const auto * bound = dyn_cast<ConstantInt>(limit);
  const auto * recurrence =
    dyn_cast<SCEVAddRecExpr>(evolution.getSCEV(variable));
  if (!isa<PHINode>(variable) || !bound || !recurrence ||
      recurrence->getLoop() != &loop || !recurrence->isAffine())
    throw std::invalid_argument("unresolved or unsupported region loop bound");
  const auto * start = dyn_cast<SCEVConstant>(recurrence->getStart());
  const auto * step =
    dyn_cast<SCEVConstant>(recurrence->getStepRecurrence(evolution));
  if ((!start && !startRange) || !step || bound->getBitWidth() > 64 ||
      !step->getAPInt().isSignedIntN(64) || step->getAPInt().isZero() ||
      (start && !start->getAPInt().isSignedIntN(64)))
    throw std::invalid_argument("unresolved region loop start or step");
  LoopBounds result{startRange ? startRange->first
                               : start->getAPInt().getSExtValue(),
                    bound->getSExtValue(),
                    step->getAPInt().getSExtValue()};
  const bool increasing =
    predicate == ICmpInst::ICMP_SLT || predicate == ICmpInst::ICMP_SLE ||
    predicate == ICmpInst::ICMP_ULT || predicate == ICmpInst::ICMP_ULE;
  const bool decreasing =
    predicate == ICmpInst::ICMP_SGT || predicate == ICmpInst::ICMP_SGE ||
    predicate == ICmpInst::ICMP_UGT || predicate == ICmpInst::ICMP_UGE;
  if ((!increasing && !decreasing) || (increasing != (result.step > 0)) ||
      (ICmpInst::isUnsigned(predicate) &&
       (result.start < 0 || result.bound < 0)))
    throw std::invalid_argument("unsupported region loop direction");
  if (predicate == ICmpInst::ICMP_SLE || predicate == ICmpInst::ICMP_ULE ||
      predicate == ICmpInst::ICMP_SGE || predicate == ICmpInst::ICMP_UGE)
  {
    if (__builtin_add_overflow(result.bound, increasing ? 1LL : -1LL,
                               &result.bound))
      throw std::invalid_argument("region loop bound overflows");
  }
  validateInductionRange(result, variable->getType()->getIntegerBitWidth(),
                         ICmpInst::isUnsigned(predicate));
  if (startRange && startRange->first != startRange->second)
  {
    const auto width = variable->getType()->getIntegerBitWidth();
    const APInt maximum(128, static_cast<uint64_t>(startRange->second), true);
    if (startRange->first > startRange->second || !maximum.isSignedIntN(width))
      throw std::invalid_argument("region loop start overflows its type");
    // Different starts can have different residues modulo the step. Bound
    // the increment after the last body iteration for every such residue.
    const bool executes = increasing ? result.start < result.bound
                                    : startRange->second > result.bound;
    if (executes)
    {
      const APInt limit(128, static_cast<uint64_t>(result.bound), true);
      const APInt stride(128, static_cast<uint64_t>(result.step), true);
      const auto afterLast = increasing ? limit + stride - 1
                                        : limit + stride + 1;
      if (ICmpInst::isUnsigned(predicate)
            ? (afterLast.isNegative() || !afterLast.isIntN(width))
            : !afterLast.isSignedIntN(width))
        throw std::invalid_argument("region loop induction overflows its type");
    }
  }
  return result;
}

}  // namespace map::region
