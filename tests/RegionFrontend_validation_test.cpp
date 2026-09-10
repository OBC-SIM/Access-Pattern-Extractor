#include "helpers/RegionTestSupport.hpp"

namespace lat::test
{
namespace
{

const std::string begin = "\n#pragma APE_ANALYZE_BEGIN\n";
const std::string end = "\n#pragma APE_ANALYZE_END\n";

TEST(RegionValidation, RejectsMissingReversedDuplicateAndNestedPairs)
{
  for (const auto & body : {begin, end, end + begin, begin + begin + end + end,
                            begin + end + begin + end})
    expectRejected(body, "one ordered begin/end pair");
}

TEST(RegionValidation, RejectsNonLiteralAndArgumentBoundaries)
{
  expectRejected("_Pragma(\"APE_ANALYZE_BEGIN\")\na[0]++;" + end,
                 "literal main-file");
  expectRejected("#define START _Pragma(\"APE_ANALYZE_BEGIN\")\nSTART\n" + end,
                 "literal main-file");
  expectRejected("#pragma APE_ANALYZE_BEGIN extra\n" + end,
                 "takes no arguments");
  expectRejected("#define EMPTY\n#pragma APE_ANALYZE_BEGIN EMPTY\n" + end,
                 "takes no arguments");
}

TEST(RegionValidation, RejectsReservedTransportUsedWithoutPragma)
{
  expectRejected(
    "__builtin_annotation(0, \"yarda.region.begin.v1\");\n"
    "__builtin_annotation(0, \"yarda.region.end.v1\");",
    "reserved region transport");
}

TEST(RegionValidation, RejectsPartialStatementsLoopsAndNestedBlocks)
{
  expectRejected("for (int i=0;i<3;++i) {" + begin + "a[i]++;" + end + "}",
                 "top-level complete");
  expectRejected("if (n)" + begin + "a[0]++;" + end, "top-level complete");
  expectRejected("{" + begin + "a[0]++;" + end + "}", "top-level complete");
}

TEST(RegionValidation, RejectsSelectedConditionalAndJumpControl)
{
  for (const auto & selected :
       {"if(n) a[0]++;", "switch(n) {case 0: a[0]++;}", "while(n) a[0]++;",
        "do {a[0]++;} while(n);", "return;", "a[0] += n ? 1 : 2;",
        "n && a[0]++;", "for(int i=0;i<3;++i) {break;}",
        "for(int i=0;i<3;++i) {continue;}"})
    expectRejected(begin + selected + end,
                   std::string(selected) == "n && a[0]++;" ? "conditional "
                                                             "expression"
                                                           : "unsupported "
                                                             "control flow");
  expectRejected("goto after;" + begin + "a[0]++;" + end + "after: a[1]++;",
                 "asm, goto and labels");
  expectRejected("if (n) return;" + begin + "a[0]++;" + end,
                 "unavoidable boundaries");
}

TEST(RegionValidation, RejectsRuntimeBoundsAndIndicesWithInitializers)
{
  expectRejected(begin + "for(int i=0;i<n;++i) a[i]++;" + end,
                 "region loop bound");
  expectRejected(begin + "a[n]++;" + end, "region index");
  expectRejected(begin + "for(int i=0;i<3;++i) a[2*i]++;" + end,
                 "scaled region index");
}

TEST(RegionValidation, RejectsInlineRegionAndCustomAssemblerNames)
{
  expectSourceRejected(
    "__attribute__((annotate(\"ape.inline\"))) void kernel(void) {" + begin +
      end + "}",
    "region conflicts with inline annotation");
  expectSourceRejected(
    "void kernel(void) __asm__(\"renamed\");\n"
    "void kernel(void) {" +
      begin + end + "}",
    "custom assembler symbols");
  expectRejected(begin + "__asm__(\"\");" + end, "asm, goto and labels");
}

TEST(RegionValidation, PreservesUnusedStaticRegionFunction)
{
  auto result = compileBody(begin + "a[0]++;" + end,
                            "static void "
                            "kernel(void)");
  ASSERT_EQ(result.getArray("functions")->size(), 1U);
  EXPECT_EQ(*(*result.getArray("functions"))[0].getAsObject()->getString("funct"
                                                                         "ion"),
            "kernel");
}

TEST(RegionValidation, KeepsZeroIterationAndDescendingLoops)
{
  auto result = compileBody(begin +
                            "for(int i=0;i<0;++i) a[i]++;\n"
                            "for(int j=3;j>0;--j) a[j]++;" +
                            end + "return;");
  auto & body =
    *(*result.getArray("functions"))[0].getAsObject()->getArray("body");
  ASSERT_EQ(body.size(), 2U);
  EXPECT_EQ(*body[0].getAsObject()->getInteger("bound"), 0);
  EXPECT_EQ(*body[1].getAsObject()->getInteger("start"), 3);
  EXPECT_EQ(*body[1].getAsObject()->getInteger("bound"), 0);
  EXPECT_EQ(*body[1].getAsObject()->getInteger("step"), -1);
}

TEST(RegionValidation, RejectsCompilerOptionsOutsideTheAllowlist)
{
  for (const auto * option : {"-O1",
                              "-O2",
                              "-O3",
                              "-Os",
                              "-Oz",
                              "-Ofast",
                              "-Og",
                              "-flto",
                              "-flto=thin",
                              "-fpass-plugin=x.so",
                              "-fsanitize=undefined",
                              "--coverage",
                              "-fprofile-arcs",
                              "-Xclang",
                              "-mllvm",
                              "@flags",
                              "-std=c++17",
                              "-include-pch",
                              "-save-temps",
                              "-emit-llvm"})
    expectSourceRejected("void k(void) {}",
                         "unsupported region compiler option", "options.c",
                         {option});
  expectSourceRejected("", "requires a C source", "input.ll");
  expectSourceRejected("", "missing", "input.c", {"-I"});
}

TEST(RegionValidation, RejectsPairAcrossFunctionBodies)
{
  expectSourceRejected("void first(void) {" + begin +
                         "}\n"
                         "void second(void) {" +
                         end + "}",
                       "one ordered begin/end pair");
}

TEST(RegionValidation, RejectsRegionCombinedWithBothFunctionRoles)
{
  expectSourceRejected(
    "__attribute__((annotate(\"ape.analyze\"), annotate(\"ape.inline\"))) "
    "void kernel(void) {" +
      begin + end + "}",
    "region conflicts with inline annotation");
}

TEST(RegionValidation, AppliesAllowedPreprocessingToLoopBounds)
{
  auto result = region::compileRegionSource(
    "int a[8]; void kernel(void) {" + begin +
      "for(int i=0;i<BOUND;++i) a[i]++;" + end + "}",
    "options.c", {"-DBOUND=3", "-O0"});
  auto & body =
    *(*result.getArray("functions"))[0].getAsObject()->getArray("body");
  EXPECT_EQ(*body[0].getAsObject()->getInteger("bound"), 3);
}

TEST(RegionValidation, RejectsPointerReinterpretationWithDifferentAccessWidth)
{
  expectRejected(begin + "((char*)a)[1] = 1;" + end, "pointer cast");
}

TEST(RegionValidation, RejectsIterationThatWrapsItsInductionType)
{
  expectRejected(begin + "for(int i=2147483646;i<=2147483647;++i) a[0]++;" +
                   end,
                 "loop induction overflows");
}

TEST(RegionValidation, RejectsConditionalControlHiddenInInlineHelper)
{
  const std::string source =
    "int a[8], n;\n"
    "__attribute__((annotate(\"ape.inline\"))) void helper(int* p) {\n"
    "if (n) p[0]++; }\nvoid kernel(void) {" +
    begin + "helper(a);" + end + "}";
  EXPECT_THROW(region::compileRegionSource(source, "inline.c"),
               std::invalid_argument);
}

TEST(RegionValidation, RejectsRuntimeParameterIndexBeforeLatOutput)
{
  EXPECT_THROW(
    compileBody(begin + "a[index]++;" + end, "void kernel(long index)"),
    std::invalid_argument);
}

}  // namespace
}  // namespace lat::test
