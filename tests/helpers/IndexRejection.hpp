#pragma once

#include <gtest/gtest.h>

#include "IndexIrFixture.hpp"

namespace lat::test
{

/**
 * @brief Require the intended index rejection instead of any exception.
 * @param ir Borrowed parsed fixture, with all analyses alive.
 * @param reason Expected diagnostic suffix after the affine index prefix.
 * @param names Borrowed optional debug bindings.
 * @return Nothing; records a test failure on success or a different reason.
 */
inline void expectIndexRejected(IndexIr & ir, const std::string & reason,
                                const NameMap & names = {})
{
  try
  {
    ir.resolve(names);
    FAIL() << "expected affine index rejection";
  }
  catch (const std::invalid_argument & error)
  {
    EXPECT_EQ(error.what(), "unsupported affine index: " + reason);
  }
}

}  // namespace lat::test
