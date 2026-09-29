#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"

namespace map::region
{

/** @brief Numeric control envelope; affine endpoints are conservative limits. */
struct LoopBounds
{
  std::int64_t start;
  std::int64_t bound;
  std::int64_t step;
};

/**
 * @brief Resolve a finite canonical for loop without fallback sentinel values.
 * @param loop Borrowed normalized loop.
 * @param evolution Borrowed whole-function scalar evolution.
 * @param startRange Optional proved signed range of an affine start. Without
 * it, the start must be constant.
 * @param boundRange Optional proved signed range of the comparison limit.
 * Without it, the limit must be constant.
 * @return Minimum start, exclusive bound and nonzero signed step. For affine
 * endpoints these are analysis limits, with the bound furthest along step.
 * @throws std::invalid_argument for unresolved or unsupported iteration
 * control.
 */
LoopBounds resolveLoopBounds(llvm::Loop & loop,
                             llvm::ScalarEvolution & evolution,
                             std::optional<std::pair<std::int64_t, std::int64_t>>
                               startRange = std::nullopt,
                             std::optional<std::pair<std::int64_t, std::int64_t>>
                               boundRange = std::nullopt);

}  // namespace map::region
