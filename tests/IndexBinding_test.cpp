#include <gtest/gtest.h>

#include "AccessPath.hpp"
#include "helpers/IndexIrFixture.hpp"

namespace map::test
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

TEST(IndexBinding, RelatesSecondaryRecurrenceToControllingPhi)
{
  IndexIr ir(secondaryInduction("2"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"2*i+9"}));
}

TEST(IndexBinding, UsesControllingInductionForSecondaryPhiOffset)
{
  IndexIr ir(secondaryInduction("1"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"i+9"}));
}

TEST(IndexBinding, DebugAliasOfSecondaryPhiCannotSelectAnotherLoopVariable)
{
  IndexIr ir(secondaryInduction("2"));
  EXPECT_EQ(ir.resolve({{ir.value("j"), "i"}}),
            (std::vector<std::string>{"2*i+9"}));
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

std::string triangularIndex(const std::string & body,
                             const std::string & initial = "%i",
                             const std::string & setup = "")
{
  auto source = nestedIndex(body, false);
  const std::string entry = "inner.entry: br";
  source.replace(source.find(entry), entry.size(),
                 "inner.entry: " + setup + "\n br");
  const std::string start = "[0, %inner.entry]";
  source.replace(source.find(start), start.size(),
                 "[" + initial + ", %inner.entry]");
  return source;
}

TEST(IndexBinding, PreservesTriangularInductionAndScaledIndex)
{
  for (const auto * start : {"%i", "%first"})
  {
    IndexIr ir(triangularIndex("%index = mul i64 %j, 2", start,
                               "%first = add i64 %i, 1"));
    EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"2*j"}));
  }
}

TEST(IndexBinding, RelatesSecondaryInductionToTriangularStart)
{
  auto source = triangularIndex(
    "%nextother = add i64 %other, 2\n%index = add i64 %other, 0");
  source.insert(source.find(" %j = phi"),
                " %other = phi i64 [0, %inner.entry], [%nextother, %body]\n");
  IndexIr ir(source);
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"-2*i+2*j"}));
}

TEST(IndexBinding, RejectsNonlinearTriangularStart)
{
  IndexIr ir(triangularIndex("%index = add i64 %j, 0", "%first",
                             "%first = mul i64 %i, %i"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexBinding, RejectsWrappingTriangularStart)
{
  auto source = triangularIndex("%index = sext i8 %j to i64", "%first",
                                 "%first = add i8 %i, 127");
  for (const auto * instruction : {"%i = phi i64", "%j = phi i64",
                                  "icmp slt i64", "add i64 %i", "add i64 %j"})
  {
    std::string old = instruction;
    auto replacement = old;
    replacement.replace(replacement.find("i64"), 3, "i8");
    for (auto pos = source.find(old); pos != std::string::npos;
         pos = source.find(old))
      source.replace(pos, old.size(), replacement);
  }
  IndexIr ir(source);
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexBinding, RejectsOverflowForIntermediateStartResidues)
{
  IndexIr ir(R"(@a = global [64 x i32] zeroinitializer
    define void @kernel() {
    entry: br label %outer
    outer:
      %i = phi i8 [120, %entry], [%inext, %latch]
      %it = icmp slt i8 %i, 124
      br i1 %it, label %inner.entry, label %exit
    inner.entry: br label %inner
    inner:
      %j = phi i8 [%i, %inner.entry], [%jnext, %body]
      %jt = icmp slt i8 %j, 126
      br i1 %jt, label %body, label %latch
    body:
      %index = sext i8 %j to i64
      %p = getelementptr [64 x i32], [64 x i32]* @a, i64 0, i64 %index
      store i32 1, i32* %p
      %jnext = add i8 %j, 3
      br label %inner
    latch:
      %inext = add i8 %i, 1
      br label %outer
    exit: ret void
    })");
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexBinding, PreservesFlattenedExpressionAsOneIndex)
{
  IndexIr ir(nestedIndex(
    "%scaled = mul i64 %i, 8\n%index = add i64 %scaled, %j", false));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"8*i+j"}));
  auto * gep = llvm::cast<llvm::GEPOperator>(ir.value("p"));
  const auto result = describeGepAccess(gep, ir.evolution(), {}, {});
  EXPECT_EQ(result.indices, (std::vector<std::string>{"8*i+j"}));
  ASSERT_EQ(result.access_path.size(), 1U);
  EXPECT_EQ(result.access_path[0].value, "8*i+j");
}

TEST(IndexBinding, ExportsExclusiveOuterAffineBounds)
{
  for (const auto & test : {
         std::make_pair("slt i64 %j, %i", "i"),
         std::make_pair("sle i64 %j, %i", "i+1"),
         std::make_pair("sge i64 %i, %j", "i+1")})
  {
    auto source = nestedIndex("%index = add i64 %j, 0", false);
    const std::string comparison = "slt i64 %j, 3";
    source.replace(source.find(comparison), comparison.size(), test.first);
    IndexIr ir(source);
    const auto document = buildMapModule(*ir.module, ir.modules);
    const auto * body = (*document.getArray("functions"))[0].getAsObject()
                         ->getArray("body");
    const auto * outer = (*body)[0].getAsObject();
    const auto * inner = (*outer->getArray("body"))[0].getAsObject();
    EXPECT_EQ(inner->getString("bound"),
              llvm::Optional<llvm::StringRef>(test.second));
    EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"j"}));
  }
}

TEST(IndexBinding, RejectsSelfDependentLoopEndBeforeResolvingItsRecurrence)
{
  IndexIr ir(indexLoop("%index = add i64 %i, 0", "0", "%i"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexBinding, RejectsOverflowForVariableBoundResidues)
{
  auto source = nestedIndex("%index = sext i8 %j to i64", false);
  for (const auto & replacement : {
         std::make_pair("phi i64 [0, %entry]", "phi i8 [124, %entry]"),
         std::make_pair("slt i64 %i, 2", "slt i8 %i, 127"),
         std::make_pair("phi i64 [0, %inner.entry]", "phi i8 [120, %inner.entry]"),
         std::make_pair("slt i64 %j, 3", "slt i8 %j, %i"),
         std::make_pair("add i64 %j, 1", "add i8 %j, 5"),
         std::make_pair("add i64 %i, 1", "add i8 %i, 1")})
  {
    const std::string old = replacement.first;
    source.replace(source.find(old), old.size(), replacement.second);
  }
  IndexIr ir(source);
  // Bound 124 terminates at 125; the later bound 126 would increment to 130.
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
}  // namespace map::test
