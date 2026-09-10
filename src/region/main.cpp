#include <stdexcept>

#include "RegionFrontendAction.hpp"
#include "RegionOutput.hpp"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

/**
 * @brief Compile one original C source to a complete selected LAT document.
 * @param argc Number of command-line arguments.
 * @param argv Borrowed non-null source, output and allowed preprocessing
 * options.
 * @return Zero on success, one on compilation/output failure, two on usage
 * error.
 */
int main(int argc, char ** argv)
{
  llvm::llvm_shutdown_obj shutdown;
  if (argc < 3)
  {
    llvm::errs() << "usage: yarda_region_lat SOURCE.c OUTPUT.json [--] "
                    "[preprocessing-options]\n";
    return 2;
  }
  try
  {
    std::vector<std::string> arguments;
    int first = argc > 3 && std::string(argv[3]) == "--" ? 4 : 3;
    for (int i = first; i < argc; ++i) arguments.emplace_back(argv[i]);
    auto source = llvm::MemoryBuffer::getFile(argv[1]);
    if (!source)
      throw std::runtime_error("cannot read source: " +
                               source.getError().message());
    const auto result = lat::region::compileRegionSource((*source)->getBuffer(),
                                                         argv[1], arguments);
    lat::region::writeRegionLat(result, argv[2]);
    return 0;
  }
  catch (const std::exception & error)
  {
    llvm::errs() << "yarda_region_lat: " << error.what() << '\n';
    return 1;
  }
}
