#pragma once

#include <cstdint>

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"

namespace lat::region
{

/** @brief Exact constant iteration parameters for the supported header test. */
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
 * @return Exact start, exclusive bound and nonzero signed step.
 * @throws std::invalid_argument for unresolved or unsupported iteration
 * control.
 */
LoopBounds resolveLoopBounds(llvm::Loop & loop,
                             llvm::ScalarEvolution & evolution);

}  // namespace lat::region
