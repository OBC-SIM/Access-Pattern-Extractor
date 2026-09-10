#pragma once

#include <memory>
#include <set>
#include <vector>

#include "AccessMetadata.hpp"
#include "AnalysisRegion.hpp"
#include "IrHelpers.hpp"
#include "Statement.hpp"

namespace lat
{

/**
 * @brief Build statements using whole-function value and loop analyses.
 * @param F Borrowed source function, retaining original object identities.
 * @param LI Borrowed loop information for F.
 * @param SE Borrowed scalar evolution for F.
 * @param names Borrowed debug name map.
 * @param inlineFuncs Borrowed set of expandable helper functions.
 * @param metadata Borrowed module object/layout metadata.
 * @param root Owned output statements appended in execution order.
 * @param selection Nullable borrowed descriptor; null selects the whole
 * function.
 * @param strict Require exact supported loops/accesses for the region frontend.
 * @return Nothing; statement ownership transfers to root.
 */
void buildRootStatements(llvm::Function & F, llvm::LoopInfo & LI,
                         llvm::ScalarEvolution & SE, const NameMap & names,
                         const std::set<const llvm::Function *> & inlineFuncs,
                         const AccessMetadata & metadata,
                         std::vector<std::unique_ptr<Statement>> & root,
                         const AnalysisRegion * selection = nullptr,
                         bool strict = false);

}  // namespace lat
