#include "RegionOutput.hpp"

#include <stdexcept>

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/raw_ostream.h"

namespace lat::region
{

void writeRegionLat(const llvm::json::Object & document,
                    const std::string & path)
{
  int descriptor = -1;
  llvm::SmallString<256> temporary;
  if (auto error = llvm::sys::fs::createUniqueFile(path + ".tmp-%%%%%%",
                                                   descriptor, temporary))
    throw std::runtime_error("cannot create LAT output: " + error.message());
  llvm::FileRemover cleanup(temporary);
  llvm::raw_fd_ostream output(descriptor, true);
  output << llvm::json::Value(llvm::json::Object(document));
  output.close();
  if (output.has_error())
  {
    output.clear_error();
    throw std::runtime_error("cannot write LAT output");
  }
  if (auto error = llvm::sys::fs::rename(temporary, path))
    throw std::runtime_error("cannot publish LAT output: " + error.message());
  cleanup.releaseFile();
}

}  // namespace lat::region
