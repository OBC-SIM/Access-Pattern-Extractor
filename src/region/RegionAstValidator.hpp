#pragma once

#include <memory>

#include "RegionPragmas.hpp"
#include "clang/AST/ASTConsumer.h"

namespace lat::region
{

/**
 * @brief Create a validator that records expected per-function region pairs.
 * @param source Borrowed preprocessing manifest, alive through AST consumption.
 * @return Owned consumer; validation errors are emitted through Clang
 * diagnostics.
 */
std::unique_ptr<clang::ASTConsumer>
makeRegionAstValidator(SourceRegions & source);

}  // namespace lat::region
