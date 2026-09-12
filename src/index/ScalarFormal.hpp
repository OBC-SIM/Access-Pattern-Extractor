#pragma once

#include <optional>

#include "IrHelpers.hpp"

namespace lat::index
{

/**
 * @brief Choose the integer interpretation shared by a formal and its callers.
 * @param argument Borrowed integer formal, with its defining module alive.
 * @return True for a zero-extended formal, including inline forwarding chains.
 */
bool usesUnsignedFormal(llvm::Argument & argument);

/**
 * @brief Preserve a formal name only when every extension preserves its value.
 * @param value Non-null borrowed integer value or extension chain.
 * @param names Borrowed debug names; these do not prove a formal binding.
 * @param unsignedValue Whether the caller interprets the result as unsigned.
 * @return Formal name, or nullopt when this is not a supported formal binding.
 * @throws std::invalid_argument If cast interpretations conflict or exceed
 * int64.
 */
std::optional<std::string> resolveFormalIndex(llvm::Value * value,
                                              const NameMap & names,
                                              bool unsignedValue);

}  // namespace lat::index
