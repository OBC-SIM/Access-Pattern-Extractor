#pragma once

#include "AccessBuilder.hpp"

namespace map::region
{

/**
 * @brief Preserve selected memory/call sites or reject unsupported
 * interpretation.
 * @param instruction Borrowed canonical instruction.
 * @param evolution Borrowed whole-function scalar evolution.
 * @param names Borrowed debug name bindings.
 * @param metadata Borrowed object/layout metadata.
 * @param inlineFunctions Borrowed expandable helper set.
 * @param function Original function binding.
 * @return Owned statement, or null only for non-access scalar/control
 * instructions.
 * @throws std::invalid_argument when a retained access cannot be represented.
 */
std::unique_ptr<Statement> makeRegionAccess(
  llvm::Instruction & instruction, llvm::ScalarEvolution & evolution,
  const NameMap & names, const AccessMetadata & metadata,
  const std::set<const llvm::Function *> & inlineFunctions,
  const llvm::Function & function);

}  // namespace map::region
