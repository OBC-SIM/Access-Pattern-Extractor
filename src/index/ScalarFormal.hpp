#pragma once

#include <optional>

#include "IrHelpers.hpp"

namespace map::index
{

/** @brief Proven scalar binding and its mathematical integer domain. */
struct FormalValue
{
  std::string name;
  llvm::APInt minimum;
  llvm::APInt maximum;
};

/**
 * @brief Resolve a direct formal or its uniquely initialized load.
 * @param value Non-null borrowed LLVM value, without a cast chain.
 * @param names Borrowed debug names, used only after proving the binding.
 * @return Integer binding with signed 128-bit bounds, or nullopt for a
 * non-integer or unbound value.
 * @throws std::invalid_argument If the formal domain exceeds signed int64.
 */
std::optional<FormalValue> resolveFormalValue(llvm::Value * value,
                                              const NameMap & names);

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

}  // namespace map::index
