#pragma once

#include "IrHelpers.hpp"

namespace lat::index
{

/**
 * @brief Preserve one GEP index as a proved integer affine expression.
 * @param value Non-null borrowed integer index.
 * @param evolution Analysis belonging to the index's function.
 * @param names Borrowed debug-name map; names do not establish equivalence.
 * @param useSite Borrowed consuming instruction; null uses the value's
 * location.
 * @param unsignedValue Interpret an inline actual in its formal's unsigned
 * domain.
 * @return One canonical sum of constant-coefficient IV/formal terms.
 * @throws std::invalid_argument If an index would lose terms, wrap or binding.
 */
std::string resolveSingleIndex(llvm::Value * value,
                               llvm::ScalarEvolution & evolution,
                               const NameMap & names,
                               const llvm::Instruction * useSite = nullptr,
                               bool unsignedValue = false);

}  // namespace lat::index
