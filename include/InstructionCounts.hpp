#pragma once

#include "llvm/IR/PassManager.h"
#include "llvm/Support/JSON.h"

namespace map
{

/**
 * @brief Describe exact block executions for one normal function invocation.
 *
 * @param function Borrowed definition at the MAP extraction IR stage.
 * @param analyses Borrowed registered function analysis manager.
 * @return Versioned block/opcode counts, or unsupported with a reason. Counts
 * exclude PHIs and debug/lifetime intrinsics. Each remaining call counts once
 * and records its target/inline role for backend composition of callee bodies.
 * No IR is modified. Only single-path CFGs and constant-count natural loops
 * with one exit are supported; integer overflow is reported as unsupported.
 */
llvm::json::Object buildInstructionCounts(
  llvm::Function & function, llvm::FunctionAnalysisManager & analyses);

}  // namespace map
