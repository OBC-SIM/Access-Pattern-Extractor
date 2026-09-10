#pragma once

#include "AnalysisRegion.hpp"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/JSON.h"

namespace lat
{

/**
 * @brief Select exactly the defined functions exported by the LAT builder.
 * @param module Borrowed module; returned pointers remain owned by it.
 * @param regions Nullable borrowed region selection, independent of
 * annotations.
 * @return Selected roots/helpers, or all definitions when no role is present.
 */
std::set<const llvm::Function *> exportedLatFunctions(
  llvm::Module & module, const AnalysisRegions * regions = nullptr);

/**
 * @brief Build a LAT v2 document without opening an output file.
 * @param module Borrowed LLVM module; function/object identities are preserved.
 * @param analyses Borrowed registered module/function analysis managers.
 * @param regions Nullable borrowed region map; null retains legacy builder
 * behavior.
 * @pre Non-null regions must have passed validateAnalysisRegions for the
 * current module after its last normalization or mutation.
 * @return Owned complete LAT document.
 * @throws std::invalid_argument for invalid selected headers or unsupported
 * region access/loop representations.
 */
llvm::json::Object buildLatModule(llvm::Module & module,
                                  llvm::ModuleAnalysisManager & analyses,
                                  const AnalysisRegions * regions = nullptr);

}  // namespace lat
