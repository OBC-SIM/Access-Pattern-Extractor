#include "../include/AccessBuilder.hpp"

#include <algorithm>
#include <stdexcept>

#include "../include/AccessMetadataBuilder.hpp"
#include "../include/AccessPath.hpp"
#include "access/AccessOperands.hpp"
#include "access/PointerInduction.hpp"
#include "index/IndexResolution.hpp"
#include "index/ScalarFormal.hpp"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"

using namespace llvm;

namespace map
{

static bool isStorageObject(Value * V)
{
  if (!V->getType()->isPointerTy()) return false;
  Value * Base = V->stripPointerCasts();
  return isa<Argument>(Base) || isa<GlobalVariable>(Base) ||
         isa<AllocaInst>(Base);
}

static std::unique_ptr<Statement> makeCall(CallBase & Call,
                                           const NameMap & names,
                                           const Function & current,
                                           ScalarEvolution * evolution)
{
  Function * Callee = Call.getCalledFunction();
  if (!Callee) return nullptr;

  std::vector<std::string> args;
  std::vector<std::string> argObjects;
  unsigned position = 0;
  for (Value * Arg : Call.args())
  {
    const bool unsignedValue =
      evolution && Arg->getType()->isIntegerTy() &&
      position < Callee->arg_size() &&
      index::usesUnsignedFormal(*Callee->getArg(position));
    args.push_back(evolution && Arg->getType()->isIntegerTy()
                     ? index::resolveSingleIndex(Arg, *evolution, names, &Call,
                                                 unsignedValue)
                     : getValueName(Arg, names));
    if (isStorageObject(Arg))
      argObjects.push_back(getObjectId(Arg, current, names));
    else
      argObjects.push_back(args.back());
    ++position;
  }
  return std::make_unique<CallStmt>(Callee->getName().str(), args,
                                    std::move(argObjects));
}

std::unique_ptr<Statement> makeDirectCall(CallBase & Call,
                                          const NameMap & names,
                                          const Function & current)
{
  return makeCall(Call, names, current, nullptr);
}

static Value * loadStorePointer(Instruction & I, std::string & op)
{
  if (auto * Load = dyn_cast<LoadInst>(&I))
  {
    op = "load";
    return Load->getPointerOperand();
  }
  if (auto * Store = dyn_cast<StoreInst>(&I))
  {
    op = "store";
    return Store->getPointerOperand();
  }
  return nullptr;
}

static std::string registeredObjectId(Value * pointer, const Function & current,
                                      const NameMap & names,
                                      const AccessMetadata & metadata)
{
  auto objectId = getObjectId(pointer, current, names);
  if (metadata.objects.count(objectId) == 0) return {};
  return objectId;
}

static std::unique_ptr<Statement> makeArrayOrScalar(
  Value * ptr, Instruction & I, ScalarEvolution & SE, const NameMap & names,
  const AccessMetadata & metadata, const Function & current, std::string op)
{
  const auto & layout = I.getModule()->getDataLayout();
  auto * accessed = accessedType(I);
  const auto width = static_cast<int64_t>(layout.getTypeStoreSize(accessed));
  if (auto access = makePointerInductionAccess(ptr, I, SE, names, metadata,
                                               current, op))
    return access;
  const auto pointerRoot = accessPointerRoot(ptr);
  if (auto * GEP = dyn_cast<GEPOperator>(ptr))
  {
    // A cast may change row strides even when the scalar element size agrees.
    // Runtime allocations have no proved storage layout; preserve their legacy
    // unresolved-object path so ELF consumers diagnose unsupported storage.
    if (pointerRoot.has_cast && isStorageObject(pointerRoot.value))
      throw std::invalid_argument("unsupported array pointer cast");
    auto desc = describeGepAccess(GEP, SE, names, metadata, &I);
    std::string base = std::move(desc.name);
    auto indices = std::move(desc.indices);
    auto accessPath = std::move(desc.access_path);
    if (indices.empty() && accessPath.empty())
    {
      auto access = std::make_unique<ScalarAccess>(base, std::move(op), width);
      access->setObjectId(
        registeredObjectId(GEP->getPointerOperand(), current, names, metadata));
      return access;
    }
    const auto arrayLayout = getArrayMetadata(GEP, layout);
    const bool field = std::any_of(accessPath.begin(), accessPath.end(),
      [](const auto & segment) { return segment.kind == "field"; });
    if ((!field && arrayLayout.elem_size != width) ||
        layout.getTypeAllocSize(accessed) != static_cast<uint64_t>(width))
      throw std::invalid_argument("array stride differs from access width");
    auto access = std::make_unique<ArrayAccess>(base, indices, arrayLayout,
                                               std::move(op));
    access->setObjectId(
      registeredObjectId(GEP->getPointerOperand(), current, names, metadata));
    access->setAccessPath(std::move(accessPath));
    return access;
  }

  Value * base = ptr->stripPointerCasts();
  if (isa<GEPOperator>(base) && isStorageObject(pointerRoot.value))
    throw std::invalid_argument("unsupported array pointer cast");
  if (!isa<Argument>(base) && !isa<GlobalVariable>(base) &&
      !isa<AllocaInst>(base))
    // Keep the source access, without asserting a relative offset or identity.
    // Resolved-address consumers reject the absent object before using layout.
    return std::make_unique<ScalarAccess>(getValueName(ptr, names),
                                          std::move(op), width);

  std::string name = getBaseName(ptr, names);
  for (const User * U : base->users())
  {
    if (ptr == base && isa<GetElementPtrInst>(U))
    {
      if (layout.getTypeAllocSize(accessed) != static_cast<uint64_t>(width))
        throw std::invalid_argument("array stride differs from access width");
      auto access = std::make_unique<ArrayAccess>(
        name, std::vector<std::string>{"0"}, ArrayMetadata{{}, width},
        std::move(op));
      access->setObjectId(registeredObjectId(ptr, current, names, metadata));
      return access;
    }
  }
  auto access = std::make_unique<ScalarAccess>(name, std::move(op), width);
  access->setObjectId(registeredObjectId(ptr, current, names, metadata));
  return access;
}

std::unique_ptr<Statement> makeAccessFromInstr(
  Instruction & I, ScalarEvolution & SE, const NameMap & names,
  const AccessMetadata & metadata,
  const std::set<const Function *> & inlineFuncs, const Function & current)
{
  if (auto * Call = dyn_cast<CallBase>(&I))
  {
    auto * callee = Call->getCalledFunction();
    if (!callee || callee == &current || !inlineFuncs.count(callee))
      return nullptr;
    return makeCall(*Call, names, current, &SE);
  }

  std::string op;
  Value * ptr = loadStorePointer(I, op);
  if (!ptr) return nullptr;

  return makeArrayOrScalar(ptr, I, SE, names, metadata, current, std::move(op));
}

}  // namespace map
