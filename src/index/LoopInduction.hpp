#pragma once

#include "../region/RegionLoopBounds.hpp"
#include "IrHelpers.hpp"
#include "llvm/ADT/APInt.h"

namespace map::index
{

/** @brief A checked emitted loop and its borrowed controlling PHI. */
struct LoopInduction
{
  llvm::PHINode * variable;
  region::LoopBounds bounds;
};

/**
 * @brief Select the PHI used by the loop's header comparison.
 * @param loop Loop whose header controls entry into its body; non-null,
 * borrowed.
 * @return Borrowed PHI, or nullptr when this loop form is unsupported.
 */
llvm::PHINode * inductionVariable(const llvm::Loop * loop);

/**
 * @brief Resolve a constant loop without substituting default start or step.
 * @param loop Non-null borrowed loop.
 * @param evolution Analysis belonging to the same function.
 * @return Checked bounds and the exact PHI used to emit the Loop variable.
 * @throws std::invalid_argument If bounds, direction or range cannot be proved.
 */
LoopInduction resolveInduction(llvm::Loop * loop,
                               llvm::ScalarEvolution & evolution);

/**
 * @brief Compute the last body IV in wide signed arithmetic.
 * @param bounds Validated constant loop bounds.
 * @return Signed 128-bit value; an empty loop returns its start.
 */
llvm::APInt lastInductionValue(const region::LoopBounds & bounds);

/**
 * @brief Name an IV without capturing constants, formals or another function's
 * IV.
 * @param loop Non-null borrowed loop.
 * @param evolution Analysis belonging to the same function.
 * @param names Borrowed debug-name map, used only after IV selection.
 * @return Deterministic name, qualified by function in modules with inline
 * calls.
 * @throws std::invalid_argument If the controlling PHI is unavailable.
 */
std::string inductionName(llvm::Loop * loop, llvm::ScalarEvolution & evolution,
                          const NameMap & names);

}  // namespace map::index
