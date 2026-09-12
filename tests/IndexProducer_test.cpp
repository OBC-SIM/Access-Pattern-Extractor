#include <gtest/gtest.h>

#include "AccessPath.hpp"
#include "helpers/IndexIrFixture.hpp"

namespace lat::test
{
namespace
{

std::string structPointerIr(const std::string & expression)
{
  auto source = indexLoop(expression);
  const auto globalEnd = source.find('\n');
  source.replace(0, globalEnd,
                 "%S = type { i32, i32 }\n@a = global [64 x %S] "
                 "zeroinitializer");
  const auto begin = source.find(" %p = getelementptr");
  const auto end = source.find("\n store", begin);
  source.replace(begin, end - begin,
                 " %base = getelementptr [64 x %S], [64 x %S]* @a, i64 0, i64 "
                 "0\n"
                 " %row = getelementptr %S, %S* %base, i64 %index\n"
                 " %p = getelementptr %S, %S* %row, i64 0, i32 1");
  return source;
}

TEST(IndexProducer, PreservesScaledStructPointerIndexBeforeFieldSelection)
{
  IndexIr ir(structPointerIr("%index = mul i64 %i, 2"));
  auto * gep = llvm::cast<llvm::GEPOperator>(ir.value("p"));
  const auto result = describeGepAccess(gep, ir.evolution(), {}, {});
  EXPECT_EQ(result.indices, (std::vector<std::string>{"2*i"}));
  ASSERT_EQ(result.access_path.size(), 2U);
  EXPECT_EQ(result.access_path[0].value, "2*i");
  EXPECT_EQ(result.access_path[1].kind, "field");
}

TEST(IndexProducer, PreservesStructPointerIndexBeforeFieldSelection)
{
  IndexIr ir(structPointerIr("%index = add i64 %i, 0"));
  auto * gep = llvm::cast<llvm::GEPOperator>(ir.value("row"));
  const auto result = describeGepAccess(gep, ir.evolution(), {}, {});
  // LLVM strips the all-zero base GEP; the pointer step still selects a row.
  EXPECT_EQ(result.indices, (std::vector<std::string>{"i"}));
  ASSERT_EQ(result.access_path.size(), 1U);
  EXPECT_EQ(result.access_path[0].kind, "index");
  EXPECT_EQ(result.access_path[0].value, "i");
}

TEST(IndexProducer, ConstantStructPointerStepIsNotAFieldNumber)
{
  IndexIr ir(structPointerIr("%index = add i64 0, 2"));
  auto * gep = llvm::cast<llvm::GEPOperator>(ir.value("row"));
  const auto result = describeGepAccess(gep, ir.evolution(), {}, {});
  EXPECT_EQ(result.indices, (std::vector<std::string>{"2"}));
}

TEST(IndexProducer, LoadedLoopTemporaryCannotBorrowAnInductionDebugName)
{
  auto source =
    indexLoop("%scaled = mul i64 %i, 2\n"
              "store i64 %scaled, i64* %slot\n%index = load i64, i64* %slot");
  const auto entry = source.find("entry: br");
  source.replace(entry, 7, "entry: %slot = alloca i64\n");
  IndexIr ir(source);
  EXPECT_THROW(ir.resolve({{ir.value("slot"), "i"}}), std::invalid_argument);
}

}  // namespace
}  // namespace lat::test
