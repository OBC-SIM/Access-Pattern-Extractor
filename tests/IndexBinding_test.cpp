#include <gtest/gtest.h>

#include "AccessPath.hpp"
#include "helpers/IndexIrFixture.hpp"

namespace lat::test
{
namespace
{

std::string secondaryInduction(const std::string & step)
{
  auto source =
    indexLoop("%other = add i64 %j, " + step + "\n%index = add i64 %j, 0");
  const auto position = source.find(" %i = phi");
  source.insert(position, " %j = phi i64 [9, %entry], [%other, %body]\n");
  return source;
}

TEST(IndexBinding, RejectsDifferentRecurrenceEvenWhenPhiAppearsFirst)
{
  IndexIr ir(secondaryInduction("2"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexBinding, UsesControllingInductionForSecondaryPhiOffset)
{
  IndexIr ir(secondaryInduction("1"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i+9"}));
}

TEST(IndexBinding, DebugAliasOfSecondaryPhiCannotSelectAnotherLoopVariable)
{
  IndexIr ir(secondaryInduction("2"));
  EXPECT_THROW(ir.resolve({{ir.value("j"), "i"}}), std::invalid_argument);
}

std::string nestedIndex(const std::string & body, bool matrix)
{
  const std::string type = matrix ? "[2 x [3 x i32]]" : "[64 x i32]";
  return "@a = global " + type +
         " zeroinitializer\n"
         "define void @kernel() {\nentry: br label %outer\nouter:\n"
         " %i = phi i64 [0, %entry], [%inext, %latch]\n"
         " %it = icmp slt i64 %i, 2\n"
         " br i1 %it, label %inner.entry, label %exit\n"
         "inner.entry: br label %inner\ninner:\n"
         " %j = phi i64 [0, %inner.entry], [%jnext, %body]\n"
         " %jt = icmp slt i64 %j, 3\n"
         " br i1 %jt, label %body, label %latch\nbody:\n" +
         body +
         "\n"
         " %p = getelementptr " +
         type + ", " + type + "* @a, i64 0, " +
         (matrix ? "i64 %i, i64 %j" : "i64 %index") +
         "\n"
         " store i32 1, i32* %p\n %jnext = add i64 %j, 1\n"
         " br label %inner\nlatch:\n %inext = add i64 %i, 1\n"
         " br label %outer\nexit: ret void\n}\n";
}

TEST(IndexBinding, RejectsFlattenedIndexInsteadOfInventingDimensions)
{
  IndexIr ir(nestedIndex(
    "%scaled = mul i64 %i, 8\n%index = add i64 %scaled, %j", false));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexBinding, PreservesActualTwoDimensionalGep)
{
  IndexIr ir(nestedIndex("", true));
  auto * gep = llvm::cast<llvm::GEPOperator>(ir.value("p"));
  const auto result = describeGepAccess(gep, ir.evolution(), {}, {});
  EXPECT_EQ(result.indices, (std::vector<std::string>{"i", "j"}));
  EXPECT_EQ(getIndexVars(gep, ir.evolution(), {}), result.indices);
  ASSERT_EQ(result.access_path.size(), 2U);
  EXPECT_EQ(result.access_path[0].value, "i");
  EXPECT_EQ(result.access_path[1].value, "j");
}

TEST(IndexBinding, DisambiguatesNestedInductionDebugNames)
{
  IndexIr ir(nestedIndex("", true));
  const NameMap names{{ir.value("i"), "same"}, {ir.value("j"), "same"}};
  auto * gep = llvm::cast<llvm::GEPOperator>(ir.value("p"));
  const auto indices = getIndexVars(gep, ir.evolution(), names);
  ASSERT_EQ(indices.size(), 2U);
  EXPECT_NE(indices[0], indices[1]);
}

TEST(IndexBinding, NumericInductionNameCannotCaptureConstantIndex)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 1"));
  const NameMap names{{ir.value("i"), "3"}};
  auto & loops =
    ir.functions.getResult<llvm::LoopAnalysis>(*ir.module->getFunction("kerne"
                                                                       "l"));
  EXPECT_NE(getInductionVarName(*loops.begin(), ir.evolution(), names), "3");
}

TEST(IndexBinding, UnnamedScalarFormalCannotCaptureConstantIndex)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 1"));
  auto * argument = ir.module->getFunction("kernel")->getArg(0);
  argument->setName("");
  EXPECT_EQ(getValueName(argument, {}), "arg0");
}

TEST(IndexBinding, LoopNameCannotBeCapturedByInlineFormalSubstitution)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 1"));
  const NameMap names{{ir.value("i"), "n"}};
  EXPECT_NE(ir.resolve(names), (std::vector<std::string>{"n+1"}));
}

}  // namespace
}  // namespace lat::test
