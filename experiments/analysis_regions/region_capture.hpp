#pragma once

#include "llvm/IR/Module.h"

namespace region_probe {

/**
 * @brief Capture one expected fixture region before any optimization.
 *
 * Tags reachable enclosed instructions with yarda.region and erases the two
 * annotation calls. Later normalization may create untagged instructions.
 * This experiment does not validate the C AST or produce region LAT.
 *
 * @param module Borrowed mutable module; must contain one region in one function.
 * @return Nothing on success.
 * @throws std::invalid_argument on invalid boundaries.
 */
void capture(llvm::Module& module);

} // namespace region_probe
