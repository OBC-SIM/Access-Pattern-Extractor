#pragma once

#include "llvm/IR/PassManager.h"
#include "llvm/Support/JSON.h"

namespace map
{

/**
 * @brief Describe exact block executions for one normal function invocation.
 * @param function Borrowed definition at the MAP extraction IR stage.
 * @param analyses Borrowed registered function analysis manager.
 * @return Versioned block/opcode counts, or unsupported with a reason. Counts
 * exclude debug/lifetime intrinsics; calls count once, excluding callee bodies.
 * No IR is modified. Only single-path CFGs and constant-count natural loops
 * with one exit are supported; integer overflow is reported as unsupported.
 */
llvm::json::Object buildInstructionCounts(
  llvm::Function & function, llvm::FunctionAnalysisManager & analyses);

}  // namespace map
