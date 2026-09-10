#pragma once

#include <string>

#include "llvm/Support/JSON.h"

namespace lat::region
{

/**
 * @brief Publish a complete LAT document using a sibling temporary file.
 * @param document Borrowed successfully validated document.
 * @param path Destination file; an existing file is replaced only on success.
 * @return Nothing after successful close and rename.
 * @throws std::runtime_error on output failure; temporary output is removed.
 */
void writeRegionLat(const llvm::json::Object & document,
                    const std::string & path);

}  // namespace lat::region
