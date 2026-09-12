#include <gtest/gtest.h>

#include "AccessPath.hpp"
#include "helpers/IndexIrFixture.hpp"

namespace lat::test
{
namespace
{

void expectRejected(IndexIr & ir, const NameMap & names = {})
{
  try
  {
    const auto result = ir.resolve(names);
    FAIL() << "lossy expression accepted";
  }
  catch (const std::invalid_argument & error)
  {
    EXPECT_NE(std::string(error.what()).find("unsupported affine index"),
              std::string::npos);
  }
}

TEST(IndexResolution, PreservesPositiveOffset)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 1"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i+1"}));
}

TEST(IndexResolution, PreservesStepTwoInduction)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 0", "2", "8", "2"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i"}));
}

TEST(IndexResolution, PreservesDescendingOffset)
{
  IndexIr ir(indexLoop("%index = sub i64 %i, 1", "5", "0", "-1", "i64", "sgt"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i-1"}));
}

TEST(IndexResolution, PreservesZeroCoefficientAsConstant)
{
  IndexIr ir(indexLoop("%zero = mul i64 %i, 0\n%index = add i64 %zero, 3"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"3"}));
}

TEST(IndexResolution, PreservesScaledInduction)
{
  IndexIr ir(indexLoop("%index = mul i64 %i, 2"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"2*i"}));
}

TEST(IndexResolution, PreservesConstantBeforeInduction)
{
  IndexIr ir(indexLoop("%index = mul i64 2, %i"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"2*i"}));
}

TEST(IndexResolution, PreservesChainedConstantMultiplication)
{
  IndexIr ir(indexLoop("%twice = mul i64 2, %i\n"
                       "%index = mul i64 %twice, 2"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"4*i"}));
}

TEST(IndexResolution, PreservesReassociatedConstantMultiplication)
{
  IndexIr ir(indexLoop("%factor = mul i64 2, 2\n"
                       "%index = mul i64 %i, %factor"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"4*i"}));
}

TEST(IndexResolution, PreservesScaledOffsetWithNonzeroStart)
{
  IndexIr ir(indexLoop("%twice = mul i64 %i, 2\n%index = add i64 %twice, 1",
                       "2", "8", "2"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"2*i+1"}));
}

TEST(IndexResolution, PreservesNegativeCoefficient)
{
  IndexIr ir(indexLoop("%index = sub i64 7, %i"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"-i+7"}));
}

TEST(IndexResolution, DebugNameCannotHideScaledTemporary)
{
  IndexIr ir(indexLoop("%index = mul i64 %i, 2"));
  EXPECT_EQ(ir.resolve({{ir.value("i"), "i"}, {ir.value("index"), "t"}}),
            (std::vector<std::string>{"2*i"}));
}

TEST(IndexResolution, DebugNameCannotReplaceProvenOffset)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 1"));
  EXPECT_EQ(ir.resolve({{ir.value("index"), "t"}}),
            (std::vector<std::string>{"i+1"}));
}

TEST(IndexResolution, RejectsRuntimeCoefficient)
{
  IndexIr ir(indexLoop("%index = mul i64 %i, %n"));
  expectRejected(ir);
}

TEST(IndexResolution, RejectsDivision)
{
  IndexIr ir(indexLoop("%index = sdiv i64 %i, 2"));
  expectRejected(ir);
}

TEST(IndexResolution, RejectsNonlinearInduction)
{
  IndexIr ir(indexLoop("%index = mul i64 %i, %i"));
  expectRejected(ir);
}

TEST(IndexResolution, RejectsUnknownStart)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 0", "%n"));
  expectRejected(ir);
}

TEST(IndexResolution, RejectsUnknownStep)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 0", "0", "4", "%n"));
  expectRejected(ir);
}

TEST(IndexResolution, AccessPathAndLegacyIndicesShareOneOffset)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 1"));
  auto * gep = llvm::cast<llvm::GEPOperator>(ir.value("p"));
  const auto description = describeGepAccess(gep, ir.evolution(), {}, {});
  EXPECT_EQ(description.indices, (std::vector<std::string>{"i+1"}));
  ASSERT_EQ(description.access_path.size(), 1U);
  EXPECT_EQ(description.access_path[0].value, "i+1");
  EXPECT_EQ(getIndexVars(gep, ir.evolution(), {}), description.indices);
}

TEST(IndexResolution, ModuleLoopRetainsDescendingExclusiveBound)
{
  IndexIr ir(indexLoop("%index = sub i64 %i, 1", "5", "0", "-1", "i64", "sgt"));
  const auto document = buildLatModule(*ir.module, ir.modules);
  const auto * functions = document.getArray("functions");
  ASSERT_NE(functions, nullptr);
  const auto * body = (*functions)[0].getAsObject()->getArray("body");
  ASSERT_EQ(body->size(), 1U);
  const auto * loop = (*body)[0].getAsObject();
  EXPECT_EQ(loop->getString("type"), llvm::Optional<llvm::StringRef>("Loop"));
  EXPECT_EQ(loop->getInteger("start"), llvm::Optional<int64_t>(5));
  EXPECT_EQ(loop->getInteger("bound"), llvm::Optional<int64_t>(0));
  EXPECT_EQ(loop->getInteger("step"), llvm::Optional<int64_t>(-1));
}

}  // namespace
}  // namespace lat::test
