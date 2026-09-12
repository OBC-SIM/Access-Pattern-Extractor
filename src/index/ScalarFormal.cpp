#include "ScalarFormal.hpp"

#include <set>
#include <stdexcept>

#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;

namespace lat::index
{
namespace
{

Argument * scalarArgument(Value * value)
{
  if (auto * argument = dyn_cast<Argument>(value)) return argument;
  auto * load = dyn_cast<LoadInst>(value);
  auto * slot =
    load ? dyn_cast<AllocaInst>(load->getPointerOperand()) : nullptr;
  if (!slot || load->isVolatile() || load->isAtomic()) return nullptr;
  StoreInst * initialization = nullptr;
  for (auto * user : slot->users())
  {
    if (auto * store = dyn_cast<StoreInst>(user))
    {
      if (initialization || store->getPointerOperand() != slot ||
          store->isVolatile() || store->isAtomic())
        return nullptr;
      initialization = store;
    }
    else if (!isa<LoadInst, DbgInfoIntrinsic>(user))
      return nullptr;
  }
  if (!initialization ||
      initialization->getParent() != &load->getFunction()->getEntryBlock() ||
      (initialization->getParent() == load->getParent() &&
       !initialization->comesBefore(load)))
    return nullptr;
  return dyn_cast<Argument>(initialization->getValueOperand());
}

bool unsignedFormal(Argument & argument, std::set<Argument *> & visited)
{
  if (!visited.insert(&argument).second) return false;
  if (argument.hasZExtAttr()) return true;
  for (auto & instruction : instructions(argument.getParent()))
  {
    if (auto * extension = dyn_cast<ZExtInst>(&instruction))
      if (scalarArgument(extension->getOperand(0)) == &argument) return true;
    auto * call = dyn_cast<CallBase>(&instruction);
    auto * callee = call ? call->getCalledFunction() : nullptr;
    if (!callee || !hasFunctionAnnotation(*callee, "ape.inline")) continue;
    for (unsigned i = 0; i < callee->arg_size(); ++i)
      if (scalarArgument(call->getArgOperand(i)) == &argument &&
          unsignedFormal(*callee->getArg(i), visited))
        return true;
  }
  return false;
}

[[noreturn]] void rejectCast()
{
  throw std::invalid_argument(
    "unsupported affine index: formal cast changes integer meaning");
}

bool preserves(const APInt & minimum, const APInt & maximum, unsigned width,
               bool unsignedValue)
{
  return unsignedValue
           ? !minimum.isNegative() && maximum.isIntN(width)
           : minimum.isSignedIntN(width) && maximum.isSignedIntN(width);
}

}  // namespace

bool usesUnsignedFormal(Argument & argument)
{
  std::set<Argument *> visited;
  return unsignedFormal(argument, visited);
}

std::optional<std::string> resolveFormalIndex(Value * value,
                                              const NameMap & names,
                                              bool unsignedValue)
{
  std::vector<CastInst *> extensions;
  auto * scalar = value;
  while (isa<SExtInst, ZExtInst>(scalar))
  {
    auto * extension = cast<CastInst>(scalar);
    extensions.push_back(extension);
    scalar = extension->getOperand(0);
  }
  auto * argument = scalarArgument(scalar);
  if (!argument) return std::nullopt;
  const auto width = argument->getType()->getIntegerBitWidth();
  if (width > 64) rejectCast();
  const bool unsignedFormal = usesUnsignedFormal(*argument);
  const auto minimum =
    unsignedFormal ? APInt(128, 0) : APInt::getSignedMinValue(width).sext(128);
  const auto maximum = unsignedFormal
                         ? APInt::getMaxValue(width).zext(128)
                         : APInt::getSignedMaxValue(width).sext(128);
  if (!preserves(minimum, maximum, 64, false)) rejectCast();
  for (auto it = extensions.rbegin(); it != extensions.rend(); ++it)
    if (!preserves(minimum, maximum, (*it)->getSrcTy()->getIntegerBitWidth(),
                   isa<ZExtInst>(*it)))
      rejectCast();
  if (!preserves(minimum, maximum, value->getType()->getIntegerBitWidth(),
                 unsignedValue))
    rejectCast();
  return getValueName(argument, names);
}

}  // namespace lat::index
