#include "InstructionCounts.hpp"

#include <gtest/gtest.h>

#include "MapModuleBuilder.hpp"
#include "helpers/RegionIrFixture.hpp"

namespace map::test
{
namespace
{

const char * loopIr = R"(
define void @kernel() {
entry: br label %header
header:
  %i = phi i64 [0, %entry], [%next, %body]
  %test = icmp slt i64 %i, 3
  br i1 %test, label %body, label %exit
body:
  %next = add nsw i64 %i, 1
  br label %header
exit: ret void
}
)";

llvm::json::Object model(const std::string & source)
{
  RegionIr ir(source);
  return buildInstructionCounts(*ir.module->getFunction("kernel"),
                                ir.functions);
}

std::uint64_t executions(const llvm::json::Object & result,
                         const std::string & name)
{
  const auto * blocks = result.getArray("blocks");
  if (!blocks) throw std::runtime_error("missing blocks");
  for (const auto & block : *blocks)
    if (block.getAsObject()->getString("name").getValueOr("") == name)
      return *block.getAsObject()->get("executions")->getAsUINT64();
  throw std::runtime_error("missing block: " + name);
}

TEST(InstructionCounts, PretestHeaderIncludesFinalFailedCondition)
{
  RegionIr ir(loopIr);
  auto document = buildMapModule(*ir.module, ir.modules);
  const auto * counts = document.getArray("functions")
                          ->front()
                          .getAsObject()
                          ->getObject("ir_instructions");
  ASSERT_NE(counts, nullptr);
  const auto & result = *counts;
  EXPECT_EQ(result.getString("status").getValueOr(""), "exact");
  EXPECT_EQ(executions(result, "entry"), 1U);
  EXPECT_EQ(executions(result, "header"), 4U);
  EXPECT_EQ(executions(result, "body"), 3U);
  EXPECT_EQ(executions(result, "exit"), 1U);
}

TEST(InstructionCounts, ZeroTripStillExecutesHeaderAndReturn)
{
  std::string source = loopIr;
  source.replace(source.find("%i, 3"), 5, "%i, 0");
  const auto result = model(source);
  EXPECT_EQ(executions(result, "header"), 1U);
  EXPECT_EQ(executions(result, "body"), 0U);
}

TEST(InstructionCounts, LatchExitExecutesEveryLoopBlockThreeTimes)
{
  const auto result = model(R"(
define void @kernel() {
entry: br label %body
body:
  %i = phi i64 [0, %entry], [%next, %body]
  %next = add nuw nsw i64 %i, 1
  %test = icmp slt i64 %next, 3
  br i1 %test, label %body, label %exit
exit: ret void
})");
  EXPECT_EQ(result.getString("status").getValueOr(""), "exact");
  EXPECT_EQ(executions(result, "body"), 3U);
}

TEST(InstructionCounts, NestedLoopCountsAreMultipliedWithoutUnrolling)
{
  const auto result = model(R"(
define void @kernel() {
entry: br label %outer
outer:
  %i = phi i64 [0, %entry], [%inext, %latch]
  %itest = icmp slt i64 %i, 800
  br i1 %itest, label %pre, label %exit
pre: br label %inner
inner:
  %j = phi i64 [0, %pre], [%jnext, %body]
  %jtest = icmp slt i64 %j, 1100
  br i1 %jtest, label %body, label %latch
body:
  %jnext = add nuw nsw i64 %j, 1
  br label %inner
latch:
  %inext = add nuw nsw i64 %i, 1
  br label %outer
exit: ret void
})");
  EXPECT_EQ(executions(result, "outer"), 801U);
  EXPECT_EQ(executions(result, "inner"), 880800U);
  EXPECT_EQ(executions(result, "body"), 880000U);
}

TEST(InstructionCounts, DataDependentBranchIsUnsupported)
{
  const auto result = model(R"(
define void @kernel(i1 %flag) {
entry: br i1 %flag, label %yes, label %no
yes: ret void
no: ret void
})");
  EXPECT_EQ(result.getString("status").getValueOr(""), "unsupported");
  EXPECT_FALSE(result.getString("reason")->empty());
  EXPECT_EQ(result.getArray("blocks"), nullptr);
}

TEST(InstructionCounts, CallsCountOnceAndLifetimeIntrinsicsAreExcluded)
{
  const auto result = model(R"(
declare void @helper()
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture)
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture)
define void @kernel() {
entry:
  %p = alloca i8
  call void @llvm.lifetime.start.p0i8(i64 1, i8* %p)
  call void @helper()
  call void @llvm.lifetime.end.p0i8(i64 1, i8* %p)
  ret void
})");
  const auto * counts =
    result.getArray("blocks")->front().getAsObject()->getObject("opcodes");
  EXPECT_EQ(*counts->get("call")->getAsUINT64(), 1);
  EXPECT_EQ(*counts->get("alloca")->getAsUINT64(), 1);
  EXPECT_EQ(*counts->get("ret")->getAsUINT64(), 1);
}

TEST(InstructionCounts, DescendingLoopUsesExactBackedgeCount)
{
  const auto result = model(R"(
define void @kernel() {
entry: br label %header
header:
  %i = phi i64 [5, %entry], [%next, %body]
  %test = icmp sgt i64 %i, -1
  br i1 %test, label %body, label %exit
body:
  %next = sub nsw i64 %i, 2
  br label %header
exit: ret void
})");
  EXPECT_EQ(executions(result, "header"), 4U);
  EXPECT_EQ(executions(result, "body"), 3U);
}

TEST(InstructionCounts, RuntimeBoundIsUnsupported)
{
  std::string source = loopIr;
  source.replace(source.find("@kernel()"), 9, "@kernel(i64 %n)");
  source.replace(source.find("%i, 3"), 5, "%i, %n");
  const auto result = model(source);
  EXPECT_EQ(result.getString("status").getValueOr(""), "unsupported");
}

TEST(InstructionCounts, ConditionalLoopBodyIsNotCountedAsUnconditional)
{
  const auto result = model(R"(
define void @kernel(i1 %flag) {
entry: br label %header
header:
  %i = phi i64 [0, %entry], [%next, %latch]
  %test = icmp slt i64 %i, 3
  br i1 %test, label %body, label %exit
body: br i1 %flag, label %yes, label %latch
yes: br label %latch
latch:
  %next = add nsw i64 %i, 1
  br label %header
exit: ret void
})");
  EXPECT_EQ(result.getString("status").getValueOr(""), "unsupported");
  EXPECT_EQ(result.getString("reason").getValueOr(""),
            "data-dependent IR branch");
}

TEST(InstructionCounts, UnreachableBlocksHaveZeroDynamicExecutions)
{
  const auto result = model(R"(
define void @kernel() {
entry: br i1 true, label %yes, label %no
yes: ret void
no: ret void
})");
  EXPECT_EQ(executions(result, "yes"), 1U);
  EXPECT_EQ(executions(result, "no"), 0U);
}

TEST(InstructionCounts, SelectedRegionDoesNotReceiveWholeFunctionCounts)
{
  RegionIr ir;
  const auto regions = captureAnalysisRegions(*ir.module, {"kernel"});
  validateAnalysisRegions(*ir.module, regions, ir.functions);
  auto document = buildMapModule(*ir.module, ir.modules, &regions);
  const auto * counts = document.getArray("functions")
                          ->front()
                          .getAsObject()
                          ->getObject("ir_instructions");
  ASSERT_NE(counts, nullptr);
  EXPECT_EQ(counts->getString("status").getValueOr(""), "unsupported");
  EXPECT_EQ(counts->getArray("blocks"), nullptr);
}

TEST(InstructionCounts, NestedExecutionOverflowIsUnsupported)
{
  const auto result = model(R"(
define void @kernel() {
entry: br label %outer
outer:
  %i = phi i64 [0, %entry], [%inext, %latch]
  %itest = icmp slt i64 %i, 4294967296
  br i1 %itest, label %pre, label %exit
pre: br label %inner
inner:
  %j = phi i64 [0, %pre], [%jnext, %body]
  %jtest = icmp slt i64 %j, 4294967296
  br i1 %jtest, label %body, label %latch
body:
  %jnext = add nuw nsw i64 %j, 1
  br label %inner
latch:
  %inext = add nuw nsw i64 %i, 1
  br label %outer
exit: ret void
})");
  EXPECT_EQ(result.getString("status").getValueOr(""), "unsupported");
  EXPECT_EQ(result.getArray("blocks"), nullptr);
}

}  // namespace
}  // namespace map::test
