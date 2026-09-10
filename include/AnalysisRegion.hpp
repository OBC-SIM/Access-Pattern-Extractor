#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"

namespace lat
{

/** @brief A source-selected region with tracked pre-normalization identities.
 */
struct AnalysisRegion
{
  std::vector<llvm::WeakVH> loopHeaders;
  std::vector<llvm::WeakVH> retainedSites;
};

/** @brief Per-invocation selection indexed by unchanged LLVM function names. */
using AnalysisRegions = std::map<std::string, AnalysisRegion>;

/**
 * @brief Match fresh IR against the AST manifest and remove private markers.
 * @param module Borrowed fresh O0 module, modified only after boundary
 * validation.
 * @param expected Source functions with exactly one validated region each.
 * @return Descriptors tracking selected complete loops and retained access
 * sites.
 * @throws std::invalid_argument for lost, extra or inconsistent boundaries.
 */
AnalysisRegions captureAnalysisRegions(llvm::Module & module,
                                       const std::set<std::string> & expected);

/**
 * @brief Verify normalized selection against the captured descriptors.
 * @param module Borrowed canonical module containing the tracked values.
 * @param regions Borrowed descriptors, alive only while module is alive.
 * @param analyses Borrowed function analysis manager rebuilt after
 * normalization.
 * @return Nothing on success.
 * @throws std::invalid_argument for lost, additional or misclassified
 * loops/access sites, including untagged sites added inside selected loops.
 */
void validateAnalysisRegions(llvm::Module & module,
                             const AnalysisRegions & regions,
                             llvm::FunctionAnalysisManager & analyses);

/**
 * @brief Detect recognizable private region transport in legacy plugin input.
 * @param module Borrowed input module.
 * @return True if markers, selection attributes or instruction tags exist.
 */
bool hasRegionTransport(const llvm::Module & module);

}  // namespace lat
