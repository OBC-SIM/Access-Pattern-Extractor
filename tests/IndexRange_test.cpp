#include <gtest/gtest.h>

#include "helpers/IndexIrFixture.hpp"

namespace lat::test
{
namespace
{

TEST(IndexRange, PreservesSignedExtension)
{
  IndexIr ir(indexLoop("%index = sext i32 %i to i64", "-3", "0", "1", "i32"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i"}));
}

TEST(IndexRange, PreservesNonnegativeZeroExtension)
{
  IndexIr ir(
    indexLoop("%index = zext i32 %i to i64", "0", "4", "1", "i32", "ult"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i"}));
}

TEST(IndexRange, RejectsZeroExtensionAcrossSignedBoundary)
{
  IndexIr ir(indexLoop("%index = zext i8 %i to i64", "-2", "2", "1", "i8"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexRange, PreservesProvenLosslessNarrowing)
{
  IndexIr ir(indexLoop("%index = trunc i64 %i to i8", "0", "4", "1", "i64",
                       "slt", "i8"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i"}));
}

TEST(IndexRange, RejectsNarrowingAcrossSignedBoundary)
{
  IndexIr ir(indexLoop("%index = trunc i64 %i to i8", "126", "130", "1", "i64",
                       "slt", "i8"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexRange, RejectsWrappingIndexRecurrence)
{
  IndexIr ir(
    indexLoop("%index = add i8 %i, 100", "20", "30", "1", "i8", "slt", "i8"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexRange, RejectsWrappingInductionUpdate)
{
  IndexIr ir(
    indexLoop("%index = sext i8 %i to i64", "126", "127", "1", "i8", "sle"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexRange, RejectsUnsignedInductionOutsideSignedDomain)
{
  IndexIr ir(
    indexLoop("%index = add i64 %i, 0", "-4", "-1", "1", "i64", "ult"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexRange, RejectsUnknownBoundBeforeClaimingNoWrap)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 1", "0", "%n"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexRange, PreservesMinimumSignedOffset)
{
  IndexIr ir(indexLoop("%index = add i64 %i, -9223372036854775808", "0", "2"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i-9223372036854775808"}));
}

TEST(IndexRange, RejectsOffsetLargerThanInt64)
{
  IndexIr ir(indexLoop("%index = add i64 %i, -9223372036854775808",
                       "-9223372036854775808", "-9223372036854775806"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexRange, PreservesWideConstantsWithinInt64)
{
  for (const auto * constant : {"-9223372036854775808", "9223372036854775807"})
  {
    IndexIr ir(indexLoop(std::string("%index = add i128 0, ") + constant, "0",
                         "4", "1", "i64", "slt", "i128"));
    EXPECT_EQ(ir.resolve(), (std::vector<std::string>{constant}));
  }
}

TEST(IndexRange, RejectsWideConstantsOutsideInt64)
{
  for (const auto * constant : {"9223372036854775808", "-9223372036854775809"})
  {
    IndexIr ir(indexLoop(std::string("%index = add i128 0, ") + constant, "0",
                         "4", "1", "i64", "slt", "i128"));
    EXPECT_THROW(ir.resolve(), std::invalid_argument);
  }
}

}  // namespace
}  // namespace lat::test
