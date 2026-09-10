#include <gtest/gtest.h>

#include "region/RegionFrontendAction.hpp"

namespace
{

TEST(RegionFrontend, SelectsLoopAndPreservesOutsideConstantDefinitions)
{
  auto result = lat::region::compileRegionSource(R"(
int before[8], inside[8], after[8];
void kernel(void) {
  const int bound = 3, offset = 1;
  before[0] = 11;
#pragma APE_ANALYZE_BEGIN
  for (int i = 0; i < bound; ++i) inside[i + offset] += 1;
#pragma APE_ANALYZE_END
  after[0] = 22;
}
)",
                                                 "boundary.c");
  auto * functions = result.getArray("functions");
  ASSERT_NE(functions, nullptr);
  ASSERT_EQ(functions->size(), 1U);
  auto & root = *(*functions)[0].getAsObject();
  EXPECT_EQ(*root.getString("function"), "kernel");
  const auto * scope = root.getObject("analysis_scope");
  ASSERT_NE(scope, nullptr);
  EXPECT_EQ(*scope->getString("name"), "APE_ANALYZE");
  auto & body = *root.getArray("body");
  ASSERT_EQ(body.size(), 1U);
  auto & loop = *body[0].getAsObject();
  EXPECT_EQ(*loop.getInteger("start"), 0);
  EXPECT_EQ(*loop.getInteger("bound"), 3);
  EXPECT_EQ(*loop.getInteger("step"), 1);
  EXPECT_EQ(*loop.getString("var"), "i");
  auto & accesses = *loop.getArray("body");
  ASSERT_EQ(accesses.size(), 2U);
  EXPECT_EQ(*accesses[0].getAsObject()->getString("object"), "global::inside");
  EXPECT_EQ(*accesses[0].getAsObject()->getString("op"), "load");
  EXPECT_EQ(*accesses[1].getAsObject()->getString("op"), "store");
  EXPECT_EQ(*accesses[1].getAsObject()->getString("object"), "global::inside");
  EXPECT_EQ(*(*accesses[0].getAsObject()->getArray("indices"))[0].getAsString(),
            "i+1");
  EXPECT_EQ(*(*accesses[1].getAsObject()->getArray("indices"))[0].getAsString(),
            "i+1");
}

TEST(RegionFrontend, KeepsEmptyAndMultipleAutomaticRegionRoots)
{
  auto result = lat::region::compileRegionSource(R"(
int a;
void first(void) {
  a = 1;
#pragma APE_ANALYZE_BEGIN
#pragma APE_ANALYZE_END
  a = 2;
}
void second(void) {
#pragma APE_ANALYZE_BEGIN
  a++;
#pragma APE_ANALYZE_END
}
)",
                                                 "empty.c");
  auto & functions = *result.getArray("functions");
  ASSERT_EQ(functions.size(), 2U);
  EXPECT_TRUE(functions[0].getAsObject()->getArray("body")->empty());
  EXPECT_EQ(functions[1].getAsObject()->getArray("body")->size(), 2U);
  for (auto & value : functions)
  {
    auto & root = *value.getAsObject();
    EXPECT_EQ(*(*root.getArray("annotations"))[0].getAsString(), "ape.analyze");
    EXPECT_NE(root.getObject("analysis_scope"), nullptr);
  }
}

TEST(RegionFrontend, RetainsOutsideLoadedValueButOnlySelectsInsideLoad)
{
  auto result = lat::region::compileRegionSource(R"(
const int bound = 3, offset = 1;
int external_value = 7, inside[8];
void kernel(void) {
  int cached = external_value;
#pragma APE_ANALYZE_BEGIN
  for (int i = 0; i < bound; ++i) {
    int sampled = external_value;
    inside[i + offset] += cached + sampled;
  }
#pragma APE_ANALYZE_END
}
)",
                                                 "global_values.c");
  auto & root = *(*result.getArray("functions"))[0].getAsObject();
  auto & loop = *(*root.getArray("body"))[0].getAsObject();
  auto & body = *loop.getArray("body");
  ASSERT_EQ(body.size(), 3U);
  EXPECT_EQ(*loop.getInteger("start"), 0);
  EXPECT_EQ(*loop.getInteger("bound"), 3);
  EXPECT_EQ(*loop.getInteger("step"), 1);
  EXPECT_EQ(*loop.getString("var"), "i");
  EXPECT_EQ(*body[0].getAsObject()->getString("object"),
            "global::external_value");
  EXPECT_EQ(*body[0].getAsObject()->getString("op"), "load");
  EXPECT_EQ(*body[1].getAsObject()->getString("object"), "global::inside");
  EXPECT_EQ(*body[1].getAsObject()->getString("op"), "load");
  EXPECT_EQ(*body[2].getAsObject()->getString("object"), "global::inside");
  EXPECT_EQ(*body[2].getAsObject()->getString("op"), "store");
  for (unsigned index : {1U, 2U})
    EXPECT_EQ(
      *(*body[index].getAsObject()->getArray("indices"))[0].getAsString(),
      "i+1");
}

TEST(RegionFrontend, RegionTakesPrecedenceAndKeepsFunctionAndInlineRoles)
{
  auto result = lat::region::compileRegionSource(R"(
#define ANALYZE __attribute__((annotate("ape.analyze")))
#define INLINE __attribute__((annotate("ape.inline")))
int a[8];
INLINE void helper(int* p) { p[0]++; }
ANALYZE void whole(void) { a[1]++; }
ANALYZE void selected(void) {
  a[2]++;
#pragma APE_ANALYZE_BEGIN
  helper(a);
#pragma APE_ANALYZE_END
  a[3]++;
}
)",
                                                 "roles.c");
  auto & functions = *result.getArray("functions");
  ASSERT_EQ(functions.size(), 3U);
  auto & root = *functions[2].getAsObject();
  ASSERT_EQ(root.getArray("body")->size(), 1U);
  auto & call = *(*root.getArray("body"))[0].getAsObject();
  EXPECT_EQ(*call.getString("callee"), "helper");
  EXPECT_EQ(*(*call.getArray("arg_objects"))[0].getAsString(), "global::a");
  EXPECT_EQ(functions[0].getAsObject()->getObject("analysis_scope"), nullptr);
  EXPECT_EQ(functions[1].getAsObject()->getObject("analysis_scope"), nullptr);
}

TEST(RegionFrontend, RetainsOpaqueSelectedCallForHierarchyRejection)
{
  auto result = lat::region::compileRegionSource(R"(
void external(void);
void kernel(void) {
#pragma APE_ANALYZE_BEGIN
  external();
#pragma APE_ANALYZE_END
}
)",
                                                 "opaque.c");
  auto & root = *(*result.getArray("functions"))[0].getAsObject();
  ASSERT_EQ(root.getArray("body")->size(), 1U);
  EXPECT_EQ(*(*root.getArray("body"))[0].getAsObject()->getString("type"),
            "Call");
}

TEST(RegionFrontend, RejectsRuntimeBoundBeforeProducingLat)
{
  EXPECT_THROW(lat::region::compileRegionSource(R"(
int n = 3, a[8];
void kernel(void) {
#pragma APE_ANALYZE_BEGIN
  for (int i = 0; i < n; ++i) a[i]++;
#pragma APE_ANALYZE_END
}
)",
                                                "dynamic.c"),
               std::invalid_argument);
}

}  // namespace
