#pragma once

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"

namespace map
{

struct AccessPointerRoot
{
  llvm::Value * value;
  bool has_cast = false;
};

// Stop at PHIs and runtime values; only the induction proof may cross a PHI.
inline AccessPointerRoot accessPointerRoot(llvm::Value * pointer)
{
  AccessPointerRoot result{pointer};
  while (true)
  {
    if (auto * gep = llvm::dyn_cast<llvm::GEPOperator>(result.value))
      result.value = gep->getPointerOperand();
    else if (auto * operation = llvm::dyn_cast<llvm::Operator>(result.value))
    {
      if (operation->getOpcode() != llvm::Instruction::BitCast &&
          operation->getOpcode() != llvm::Instruction::AddrSpaceCast)
        return result;
      result.has_cast = true;
      result.value = operation->getOperand(0);
    }
    else return result;
  }
}

inline llvm::Type * accessedType(llvm::Instruction & instruction)
{
  return llvm::isa<llvm::LoadInst>(instruction)
           ? instruction.getType()
           : llvm::cast<llvm::StoreInst>(instruction).getValueOperand()->getType();
}

}  // namespace map
