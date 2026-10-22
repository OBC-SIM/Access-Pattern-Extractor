#pragma once

#include "AccessBuilder.hpp"

namespace map
{

/**
 * @brief Lower a pointer PHI rooted in one known scalar-element storage object.
 * @param pointer Borrowed load/store address; no IR is modified.
 * @param instruction Borrowed memory access and scope for induction proofs.
 * @param evolution Borrowed scalar evolution for the enclosing function.
 * @param names Borrowed source names; never used to infer value equivalence.
 * @param metadata Registered storage objects.
 * @param current Function owning local storage identities.
 * @param operation Load/store label copied into the statement.
 * @return Owned array access, or null when no pointer PHI is involved or an
 * affine offset has a unique runtime call/load base. The caller must preserve
 * that runtime access as unresolved or reject it, never silently omit it.
 * @throws std::invalid_argument If the pointer recurrence cannot be proved.
 */
std::unique_ptr<Statement> makePointerInductionAccess(
  llvm::Value * pointer, llvm::Instruction & instruction,
  llvm::ScalarEvolution & evolution, const NameMap & names,
  const AccessMetadata & metadata, const llvm::Function & current,
  const std::string & operation);

}  // namespace map
