#pragma once

#include <string>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

namespace lat::region
{

/**
 * @brief Compile C11 to selected LAT using the fixed Clang 14 O0 pipeline.
 * @param source Borrowed complete source; outside value definitions are
 * retained.
 * @param filename Main-file identity used for includes and diagnostics.
 * @param arguments Explicitly allowed preprocessing/target arguments.
 * @return Owned complete LAT only after source/IR validation succeeds.
 * @throws std::invalid_argument for compiler errors or unsupported input.
 */
llvm::json::Object
compileRegionSource(llvm::StringRef source, const std::string & filename,
                    const std::vector<std::string> & arguments = {});

}  // namespace lat::region
