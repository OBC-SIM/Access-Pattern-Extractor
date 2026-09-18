#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "clang/Basic/SourceLocation.h"

namespace clang
{
class Preprocessor;
}
namespace map::region
{

/** @brief One literal main-file directive, matched against its injected AST. */
struct SourceBoundary
{
  clang::SourceLocation location;
  bool begin;
  bool matched = false;
};

/** @brief Per-invocation source manifest, independent of surviving IR markers.
 */
struct SourceRegions
{
  std::vector<SourceBoundary> boundaries;
  std::set<std::string> functions;
  // Defer whole-body errors until the emitted function selection is known.
  std::map<std::string, std::string> functionErrors;
};

/**
 * @brief Register private annotation transport on one owned preprocessor.
 * @param preprocessor Compiler-owned preprocessor; owns the installed handlers.
 * @param source Borrowed manifest, alive until preprocessing and AST validation
 * end.
 * @return Nothing.
 */
void registerRegionPragmas(clang::Preprocessor & preprocessor,
                           SourceRegions & source);

}  // namespace map::region
