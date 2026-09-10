#include "RegionDriverOptions.hpp"

#include <stdexcept>

#include "llvm/ADT/StringRef.h"

namespace lat::region
{

std::vector<std::string>
regionCompilerArguments(const std::vector<std::string> & arguments)
{
  std::vector<std::string> result{"-xc", "-std=c11", "-O0",
                                  "-g",  "-Xclang",  "-disable-O0-optnone"};
  for (std::size_t i = 0; i < arguments.size(); ++i)
  {
    const llvm::StringRef option(arguments[i]);
    if (option == "-O0") continue;
    const bool takesValue = option == "-I" || option == "-D" ||
                            option == "-U" || option == "-isystem" ||
                            option == "-iquote" || option == "-include" ||
                            option == "--target";
    if (takesValue)
    {
      if (i + 1 == arguments.size() || arguments[i + 1].empty() ||
          arguments[i + 1][0] == '-' || arguments[i + 1][0] == '@')
        throw std::invalid_argument("missing or invalid region option value: " +
                                    option.str());
      result.push_back(arguments[i]);
      result.push_back(arguments[++i]);
    }
    else if ((option.size() > 2 &&
              (option.startswith("-I") || option.startswith("-D") ||
               option.startswith("-U"))) ||
             (option.startswith("--target=") && option.size() > 9))
    {
      result.push_back(arguments[i]);
    }
    else
    {
      throw std::invalid_argument("unsupported region compiler option: " +
                                  option.str());
    }
  }
  return result;
}

}  // namespace lat::region
