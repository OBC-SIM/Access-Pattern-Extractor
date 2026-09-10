#pragma once

#include <gtest/gtest.h>

#include "region/RegionFrontendAction.hpp"

namespace lat::test
{

inline llvm::json::Object
compileBody(const std::string & body,
            const std::string & declaration = "void kernel(void)")
{
  return region::compileRegionSource("int a[8], n = 3;\n" + declaration +
                                       " {\n" + body + "\n}\n",
                                     "validation.c");
}

inline void
expectSourceRejected(const std::string & source, const std::string & diagnostic,
                     const std::string & filename = "validation.c",
                     const std::vector<std::string> & arguments = {})
{
  try
  {
    region::compileRegionSource(source, filename, arguments);
    FAIL() << "source must be rejected: " << source;
  }
  catch (const std::invalid_argument & error)
  {
    EXPECT_NE(std::string(error.what()).find(diagnostic), std::string::npos)
      << error.what();
  }
}

inline void expectRejected(const std::string & body,
                           const std::string & diagnostic)
{
  expectSourceRejected(
    "int a[8], n = 3;\nvoid kernel(void) {\n" + body + "\n}\n", diagnostic);
}

}  // namespace lat::test
