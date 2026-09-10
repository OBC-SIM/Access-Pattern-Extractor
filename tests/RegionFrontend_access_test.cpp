#include "helpers/RegionTestSupport.hpp"

namespace lat::test
{
namespace
{

TEST(RegionAccess, RejectsRuntimePointerBaseLoadedInsideOrOutsideRegion)
{
  for (const auto & prefix : {std::string{}, std::string{"int *p = gp;"}})
  {
    const auto pointer = prefix.empty() ? "gp" : "p";
    expectSourceRejected("int *gp; void kernel(void) {" + prefix +
                           "\n#pragma APE_ANALYZE_BEGIN\n" + pointer +
                           "[0] = 1;\n#pragma APE_ANALYZE_END\n}",
                         "unresolved region access object");
  }
}

TEST(RegionAccess, PreservesInlinePointerParameterBinding)
{
  auto result = region::compileRegionSource(R"(
int a[8];
__attribute__((annotate("ape.inline"))) void helper(int *p) {p[0]++;}
void kernel(void) {
#pragma APE_ANALYZE_BEGIN
  helper(a);
#pragma APE_ANALYZE_END
}
)",
                                            "parameter.c");
  const auto & functions = *result.getArray("functions");
  ASSERT_EQ(functions.size(), 2U);
  const auto & body = *functions[0].getAsObject()->getArray("body");
  ASSERT_EQ(body.size(), 2U);
  for (const auto & access : body)
    EXPECT_EQ(*access.getAsObject()->getString("object"),
              "function:helper::param:p");
  const auto & call =
    *(*functions[1].getAsObject()->getArray("body"))[0].getAsObject();
  EXPECT_EQ(*(*call.getArray("arg_objects"))[0].getAsString(), "global::a");
}

TEST(RegionAccess, PreservesLocalAndGlobalObjects)
{
  auto result = compileBody(
    "int local[2]; local[0] = 3;\n#pragma APE_ANALYZE_BEGIN\n"
    "a[1] = local[0];\n#pragma APE_ANALYZE_END\n");
  const auto & body =
    *(*result.getArray("functions"))[0].getAsObject()->getArray("body");
  ASSERT_EQ(body.size(), 2U);
  EXPECT_EQ(*body[0].getAsObject()->getString("object"),
            "function:kernel::local:local");
  EXPECT_EQ(*body[1].getAsObject()->getString("object"), "global::a");
}

TEST(RegionAccess, PreservesOpaqueCallArgumentObjectsAndArity)
{
  auto result = region::compileRegionSource(R"(
int a[8];
void external(int *, int, int *);
void kernel(void) {
  int local[2];
#pragma APE_ANALYZE_BEGIN
  external(a, 3, local);
#pragma APE_ANALYZE_END
}
)",
                                            "opaque.c");
  const auto & body =
    *(*result.getArray("functions"))[0].getAsObject()->getArray("body");
  ASSERT_EQ(body.size(), 1U);
  const auto & call = *body[0].getAsObject();
  EXPECT_EQ(*call.getString("callee"), "external");
  ASSERT_EQ(call.getArray("args")->size(), 3U);
  const auto & objects = *call.getArray("arg_objects");
  ASSERT_EQ(objects.size(), call.getArray("args")->size());
  EXPECT_EQ(*objects[0].getAsString(), "global::a");
  EXPECT_EQ(*objects[1].getAsString(), "3");
  EXPECT_EQ(*objects[2].getAsString(), "function:kernel::local:local");
}

}  // namespace
}  // namespace lat::test
