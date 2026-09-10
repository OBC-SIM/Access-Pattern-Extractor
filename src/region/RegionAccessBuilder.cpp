#include "RegionAccessBuilder.hpp"

#include <stdexcept>

#include "RegionInstructions.hpp"
#include "RegionLoopBounds.hpp"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;

namespace lat::region
{
namespace
{

void validateIndex(Value * value, ScalarEvolution & evolution,
                   const NameMap & names)
{
  const auto * expression = evolution.getSCEV(value);
  if (isa<SCEVConstant>(expression)) return;
  const auto * recurrence = dyn_cast<SCEVAddRecExpr>(expression);
  if (!recurrence || !recurrence->isAffine())
    throw std::invalid_argument("unresolved or unsupported region index");
  const auto * start = dyn_cast<SCEVConstant>(recurrence->getStart());
  const auto * step =
    dyn_cast<SCEVConstant>(recurrence->getStepRecurrence(evolution));
  auto * loop = const_cast<Loop *>(recurrence->getLoop());
  const auto bounds = resolveLoopBounds(*loop, evolution);
  if (!start || !step || step->getAPInt().getSExtValue() != bounds.step)
    throw std::invalid_argument("unsupported scaled region index");
  const auto resolved = resolveIndex(value, evolution, names);
  const auto variable = getInductionVarName(loop, evolution, names);
  std::int64_t offset = 0;
  if (__builtin_sub_overflow(start->getAPInt().getSExtValue(), bounds.start,
                             &offset))
    throw std::invalid_argument("region index offset overflows");
  const auto expected =
    variable +
    (offset == 0 ? "" : (offset > 0 ? "+" : "") + std::to_string(offset));
  if (resolved.size() != 1 || resolved[0] != expected)
    throw std::invalid_argument("unresolved region index binding");
}

void validatePointer(Value * pointer, ScalarEvolution & evolution,
                     const NameMap & names)
{
  if (const auto * operation = dyn_cast<Operator>(pointer))
    if (operation->getOpcode() == Instruction::BitCast ||
        operation->getOpcode() == Instruction::AddrSpaceCast)
      throw std::invalid_argument("unsupported region pointer cast");
  if (auto * gep = dyn_cast<GEPOperator>(pointer))
  {
    for (auto & index : gep->indices())
      validateIndex(index.get(), evolution, names);
    validatePointer(gep->getPointerOperand(), evolution, names);
    return;
  }
  if (!isa<Argument, GlobalVariable, AllocaInst>(pointer))
    throw std::invalid_argument("unresolved region access object");
}

}  // namespace

std::unique_ptr<Statement> makeRegionAccess(
  Instruction & instruction, ScalarEvolution & evolution, const NameMap & names,
  const AccessMetadata & metadata,
  const std::set<const Function *> & inlineFunctions, const Function & function)
{
  if (isNonAccessIntrinsic(instruction)) return nullptr;
  if (isa<IntrinsicInst>(instruction))
  {
    throw std::invalid_argument("unsupported intrinsic in selected body");
  }
  if (auto * call = dyn_cast<CallBase>(&instruction))
  {
    if (auto result = makeAccessFromInstr(instruction, evolution, names,
                                          metadata, inlineFunctions, function))
      return result;
    const auto * callee = call->getCalledFunction();
    if (!callee)
      throw std::invalid_argument("unsupported indirect selected call");
    return makeDirectCall(*call, names, function);
  }
  Value * pointer = nullptr;
  if (auto * load = dyn_cast<LoadInst>(&instruction))
  {
    if (load->isAtomic())
      throw std::invalid_argument("unsupported atomic access");
    pointer = load->getPointerOperand();
  }
  if (auto * store = dyn_cast<StoreInst>(&instruction))
  {
    if (store->isAtomic())
      throw std::invalid_argument("unsupported atomic access");
    pointer = store->getPointerOperand();
  }
  if (pointer)
  {
    validatePointer(pointer, evolution, names);
    auto result = makeAccessFromInstr(instruction, evolution, names, metadata,
                                      inlineFunctions, function);
    if (!result)
      throw std::invalid_argument("unsupported selected memory access");
    return result;
  }
  if (instruction.mayReadOrWriteMemory())
    throw std::invalid_argument("unsupported selected memory instruction");
  return nullptr;
}

}  // namespace lat::region
