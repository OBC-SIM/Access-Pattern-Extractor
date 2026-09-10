#pragma once

#include <string>
#include <vector>

namespace lat::region
{

/**
 * @brief Append allowed preprocessing/target options to the fixed compiler
 * flags.
 * @param arguments Borrowed user options, without a source or output filename.
 * @return Owned complete Clang argument vector.
 * @throws std::invalid_argument for every unsupported or incomplete option.
 */
std::vector<std::string>
regionCompilerArguments(const std::vector<std::string> & arguments);

}  // namespace lat::region
