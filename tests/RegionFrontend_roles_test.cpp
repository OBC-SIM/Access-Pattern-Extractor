#include "helpers/RegionTestSupport.hpp"

namespace map::test
{
namespace
{

const std::string selected = R"(
void selected(void) {
#pragma APE_ANALYZE_BEGIN
  a[1]++;
#pragma APE_ANALYZE_END
}
)";

TEST(RegionRoles, RejectsConditionalFallbackWithoutAnySelection)
{
  expectSourceRejected(
    "int a[8], n; void plain(void) {if(n) a[0]=1; else a[1]=2;}",
    "plain: unsupported control flow");
}

TEST(RegionRoles, PreservesSupportedFallbackWithoutInventingTaskRoots)
{
  auto result = region::compileRegionSource(
    "int a[8]; void plain(void) {a[0]++; return;}", "plain.c");
  const auto & functions = *result.getArray("functions");
  ASSERT_EQ(functions.size(), 1U);
  const auto & function = *functions[0].getAsObject();
  EXPECT_EQ(*function.getString("function"), "plain");
  EXPECT_TRUE(function.getArray("annotations")->empty());
  EXPECT_EQ(function.getObject("analysis_scope"), nullptr);
  EXPECT_EQ(function.getArray("body")->size(), 2U);
}

TEST(RegionRoles, IgnoresExcludedBodiesRegardlessOfDeclarationOrder)
{
  const std::string unused = "void unused(void) {if(n) a[0]++;}";
  for (bool first : {false, true})
  {
    auto result = region::compileRegionSource(
      "int a[8], n;" + (first ? unused + selected : selected + unused),
      "mixed.c");
    const auto & functions = *result.getArray("functions");
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(*functions[0].getAsObject()->getString("function"), "selected");
  }
}

TEST(RegionRoles, IgnoresUnemittedStaticBodyInFallback)
{
  auto result = region::compileRegionSource(
    "int a[8], n; static void unused(void) {if(n) a[0]++;}"
    "void plain(void) {a[1]++;}",
    "static.c");
  const auto & functions = *result.getArray("functions");
  ASSERT_EQ(functions.size(), 1U);
  EXPECT_EQ(*functions[0].getAsObject()->getString("function"), "plain");
}

TEST(RegionRoles, RejectsWholeRootControlWithAndWithoutRegion)
{
  const std::string whole =
    "int a[8], n; __attribute__((annotate(\"ape.analyze\"))) "
    "void whole(void) {if(n) a[0]++;}";
  for (const auto & source : {whole, whole + selected})
    expectSourceRejected(source, "whole: unsupported control flow");
}

}  // namespace
}  // namespace map::test
