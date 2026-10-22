#include <gtest/gtest.h>

#include "../src/region/RegionLoopBounds.hpp"
#include "AccessMetadataBuilder.hpp"
#include "AccessPath.hpp"
#include "StatementBuilder.hpp"
#include "helpers/IndexIrFixture.hpp"

namespace map::test
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
  IndexIr ir(
    indexLoop("%twice = mul i64 2, %i\n"
              "%index = mul i64 %twice, 2"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"4*i"}));
}

TEST(IndexResolution, PreservesReassociatedConstantMultiplication)
{
  IndexIr ir(
    indexLoop("%factor = mul i64 2, 2\n"
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

TEST(IndexResolution, StrictRuntimeBoundRetainsRegionDiagnostic)
{
  auto source = indexLoop("%index = add i64 %i, 0", "0", "%limit");
  source.insert(0, "@limit = global i64 4\n");
  source.insert(source.find(" %test ="), " %limit = load i64, i64* @limit\n");
  IndexIr ir(source);
  auto & function = *ir.module->getFunction("kernel");
  auto & loops = ir.functions.getResult<llvm::LoopAnalysis>(function);
  std::vector<std::unique_ptr<Statement>> statements;
  try
  {
    buildRootStatements(function, loops, ir.evolution(), {}, {},
                        buildAccessMetadata(*ir.module), statements, nullptr,
                        true);
    FAIL() << "runtime loop bound accepted";
  }
  catch (const std::invalid_argument & error)
  {
    EXPECT_STREQ(error.what(), "unresolved or unsupported region loop bound");
  }
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
  const auto document = buildMapModule(*ir.module, ir.modules);
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

region::LoopBounds loopBounds(
  IndexIr & ir, std::optional<std::pair<int64_t, int64_t>> start = std::nullopt,
  std::optional<std::pair<int64_t, int64_t>> bound = std::nullopt)
{
  auto & loops =
    ir.functions.getResult<llvm::LoopAnalysis>(*ir.module->getFunction("kerne"
                                                                       "l"));
  return region::resolveLoopBounds(**loops.begin(), ir.evolution(), start,
                                   bound);
}

TEST(IndexResolution, NotEqualPreservesReachableExclusiveEndpoints)
{
  const int64_t cases[][3] = {{0, 8, 2},
                              {8, 0, -2},
                              {-7, -1, 2},
                              {-1, -7, -2},
                              {3, 3, 2},
                              {3, 3, -2},
                              {0, INT64_MIN, INT64_MIN}};
  for (const auto & values : cases)
  {
    SCOPED_TRACE(::testing::Message()
                 << values[0] << ", " << values[1] << ", " << values[2]);
    IndexIr ir(indexLoop("%index = add i64 %i, 0", std::to_string(values[0]),
                         std::to_string(values[1]), std::to_string(values[2]),
                         "i64", "ne"));
    const auto bounds = loopBounds(ir);
    EXPECT_EQ(bounds.start, values[0]);
    EXPECT_EQ(bounds.bound, values[1]);
    EXPECT_EQ(bounds.step, values[2]);
    EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i"}));
    const auto document = buildMapModule(*ir.module, ir.modules);
    const auto * functions = document.getArray("functions");
    ASSERT_NE(functions, nullptr);
    const auto * body = (*functions)[0].getAsObject()->getArray("body");
    ASSERT_EQ(body->size(), 1U);
    const auto * loop = (*body)[0].getAsObject();
    EXPECT_EQ(loop->getInteger("start"), llvm::Optional<int64_t>(values[0]));
    EXPECT_EQ(loop->getInteger("bound"), llvm::Optional<int64_t>(values[1]));
    EXPECT_EQ(loop->getInteger("step"), llvm::Optional<int64_t>(values[2]));
  }
}

TEST(IndexResolution, NotEqualNormalizesSwappedOperandsAndExitBranch)
{
  auto source = indexLoop("%index = add i64 %i, 0", "0", "8", "2", "i64", "ne");
  const std::string compare = "icmp ne i64 %i, 8";
  source.replace(source.find(compare), compare.size(), "icmp eq i64 8, %i");
  const std::string branch = "br i1 %test, label %body, label %exit";
  source.replace(source.find(branch), branch.size(),
                 "br i1 %test, label %exit, label %body");
  IndexIr ir(source);
  const auto bounds = loopBounds(ir);
  EXPECT_EQ(bounds.start, 0);
  EXPECT_EQ(bounds.bound, 8);
  EXPECT_EQ(bounds.step, 2);
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i"}));
}

TEST(IndexResolution, NotEqualRejectsUnreachableEndAndWrongDirection)
{
  const int64_t cases[][3] = {{0, 7, 2}, {7, 0, -2}, {0, 8, -2}, {8, 0, 2}};
  for (const auto & values : cases)
  {
    IndexIr ir(indexLoop("%index = add i64 %i, 0", std::to_string(values[0]),
                         std::to_string(values[1]), std::to_string(values[2]),
                         "i64", "ne"));
    EXPECT_THROW(loopBounds(ir), std::invalid_argument);
    expectRejected(ir);
  }
}

TEST(IndexResolution, NotEqualRejectsZeroStepLoop)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 0", "0", "8", "0", "i64", "ne"));
  EXPECT_THROW(loopBounds(ir), std::invalid_argument);
  EXPECT_THROW(buildMapModule(*ir.module, ir.modules), std::invalid_argument);
}

TEST(IndexResolution, NotEqualRejectsTerminationThroughSignedWrap)
{
  for (const auto & values : {std::vector<std::string>{"126", "-128", "2"},
                              std::vector<std::string>{"-127", "127", "-2"}})
  {
    IndexIr ir(indexLoop("%index = sext i8 %i to i64", values[0], values[1],
                         values[2], "i8", "ne"));
    EXPECT_THROW(loopBounds(ir), std::invalid_argument);
    expectRejected(ir);
  }
}

TEST(IndexResolution, NotEqualSupportsDistanceBeyondSignedInt64)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 0", "-9223372036854775808",
                       "9223372036854775807", "3", "i64", "ne"));
  const auto bounds = loopBounds(ir);
  EXPECT_EQ(bounds.start, INT64_MIN);
  EXPECT_EQ(bounds.bound, INT64_MAX);
  EXPECT_EQ(bounds.step, 3);
}

TEST(IndexResolution, NotEqualAcceptsOnlySingletonEndpointRanges)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 0", "0", "%n", "2", "i64", "ne"));
  EXPECT_EQ(loopBounds(ir, std::make_pair(0, 0), std::make_pair(8, 8)).bound,
            8);
  EXPECT_THROW(loopBounds(ir, std::make_pair(0, 2), std::make_pair(8, 8)),
               std::invalid_argument);
  EXPECT_THROW(loopBounds(ir, std::make_pair(0, 0), std::make_pair(6, 8)),
               std::invalid_argument);
  EXPECT_THROW(loopBounds(ir), std::invalid_argument);
}

TEST(IndexResolution, NotEqualRejectsEndpointOutsideInductionType)
{
  IndexIr ir(
    indexLoop("%index = sext i8 %i to i64", "%n", "4", "1", "i8", "ne"));
  EXPECT_THROW(loopBounds(ir, std::make_pair(-129, -129)),
               std::invalid_argument);
}

}  // namespace
}  // namespace map::test
