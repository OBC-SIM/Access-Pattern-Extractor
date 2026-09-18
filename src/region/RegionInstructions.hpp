#pragma once

#include "llvm/IR/IntrinsicInst.h"

namespace map::region
{

/**
 * @brief Recognize compiler scalar/debug/lifetime intrinsics that emit no MAP
 * site.
 * @param instruction Borrowed IR instruction.
 * @return True only for non-observable intrinsic work, never ordinary calls.
 */
inline bool isNonAccessIntrinsic(const llvm::Instruction & instruction)
{
  const auto * intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(&instruction);
  return intrinsic && (llvm::isa<llvm::DbgInfoIntrinsic>(intrinsic) ||
                       intrinsic->isLifetimeStartOrEnd() ||
                       (!intrinsic->mayReadOrWriteMemory() &&
                        !intrinsic->mayHaveSideEffects()));
}

}  // namespace map::region
