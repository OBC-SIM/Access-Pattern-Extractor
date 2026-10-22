#include <gtest/gtest.h>

#include "../../src/region/RegionAccessBuilder.hpp"
#include "../helpers/IndexIrFixture.hpp"
#include "AccessBuilder.hpp"
#include "AccessMetadataBuilder.hpp"
#include "JsonExportVisitor.hpp"

namespace map::test
{
namespace
{

std::unique_ptr<Statement> access(IndexIr & ir, bool strict = false)
{
  auto & instruction = *ir.value("value");
  auto & function = *ir.module->getFunction("kernel");
  const auto metadata = buildAccessMetadata(*ir.module);
  return strict ? region::makeRegionAccess(instruction, ir.evolution(), {},
                                           metadata, {}, function)
                : makeAccessFromInstr(instruction, ir.evolution(), {},
                                      metadata, {}, function);
}

void rejected(IndexIr & ir, const char * message, bool strict = false)
{
  try
  {
    access(ir, strict);
    FAIL() << "accepted unsupported access";
  }
  catch (const std::invalid_argument & error)
  {
    EXPECT_NE(std::string(error.what()).find(message), std::string::npos)
      << error.what();
  }
}

TEST(AccessWidth, ScalarCastSerializesActualLoadWidth)
{
  IndexIr ir("define void @kernel(i8* %p) {\n"
             " %wide = bitcast i8* %p to i32*\n"
             " %value = load i32, i32* %wide\n ret void\n}");
  auto statement = access(ir);
  JsonExportVisitor visitor;
  statement->accept(visitor);
  const auto * node = visitor.getResult().getAsObject();
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->getString("type").getValueOr(""), "Scalar");
  EXPECT_EQ(node->getInteger("elem_size").getValueOr(0), 4);
}

TEST(AccessWidth, ScalarCastWidthDoesNotDependOnOtherGepUsers)
{
  IndexIr ir("define void @kernel(i8* %p) {\n"
             " %other = getelementptr i8, i8* %p, i64 1\n"
             " %wide = bitcast i8* %p to i32*\n"
             " %value = load i32, i32* %wide\n ret void\n}");
  auto statement = access(ir);
  JsonExportVisitor visitor;
  statement->accept(visitor);
  const auto * node = visitor.getResult().getAsObject();
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->getString("type").getValueOr(""), "Scalar");
  EXPECT_EQ(node->getInteger("elem_size").getValueOr(0), 4);
}

TEST(AccessWidth, RejectsChangedArrayRowLayoutInsideCallee)
{
  IndexIr ir("define void @kernel([3 x i32]* %p) {\n"
             " %rows = bitcast [3 x i32]* %p to [2 x i32]*\n"
             " %at = getelementptr [2 x i32], [2 x i32]* %rows, i64 1, i64 1\n"
             " %value = load i32, i32* %at\n ret void\n}");
  rejected(ir, "unsupported array pointer cast");
}

TEST(AccessWidth, RejectsArrayStrideDifferentFromMemoryWidth)
{
  IndexIr ir("target datalayout = \"e-p:64:64-i24:32\"\n"
             "define void @kernel(i24* %p) {\n"
             " %at = getelementptr i24, i24* %p, i64 1\n"
             " %value = load i24, i24* %at\n ret void\n}");
  rejected(ir, "array stride differs from access width");
}

TEST(AccessWidth, ScalarSerializesStoreSizeRatherThanPaddedAllocationSize)
{
  IndexIr ir("target datalayout = \"e-p:64:64-i24:32\"\n"
             "define void @kernel(i24* %p) {\n"
             " %value = load i24, i24* %p\n ret void\n}");
  auto statement = access(ir);
  JsonExportVisitor visitor;
  statement->accept(visitor);
  EXPECT_EQ(visitor.getResult().getAsObject()->getInteger("elem_size")
              .getValueOr(0), 3);
}

TEST(AccessWidth, RejectsPaddedStructFieldWidth)
{
  IndexIr ir("target datalayout = \"e-p:64:64-f80:128\"\n"
             "%S = type { x86_fp80 }\n"
             "define void @kernel(%S* %p) {\n"
             " %at = getelementptr %S, %S* %p, i64 0, i32 0\n"
             " %value = load x86_fp80, x86_fp80* %at\n ret void\n}");
  rejected(ir, "array stride differs from access width");
}

TEST(AccessWidth, RejectsCastOfNonzeroGepInsteadOfOmittingAccess)
{
  IndexIr ir("define void @kernel(i8* %p) {\n"
             " %at = getelementptr i8, i8* %p, i64 4\n"
             " %wide = bitcast i8* %at to i32*\n"
             " %value = load i32, i32* %wide\n ret void\n}");
  rejected(ir, "unsupported array pointer cast");
}

TEST(AccessWidth, PreservesBothRuntimeCastSpellingsWithoutStorageIdentity)
{
  for (const bool allocation : {false, true})
    for (const bool castFirst : {false, true})
    {
      SCOPED_TRACE(allocation ? "malloc" : "pointer load");
      SCOPED_TRACE(castFirst ? "cast before GEP" : "cast after GEP");
      IndexIr ir("@source = global i8* null\n"
        "declare i8* @malloc(i64)\n"
        "define void @kernel() {\n" +
        std::string(allocation ? " %buf = call i8* @malloc(i64 16)\n"
                               : " %buf = load i8*, i8** @source\n") +
        (castFirst ? " %wide = bitcast i8* %buf to i32*\n"
                     " %at = getelementptr i32, i32* %wide, i64 1\n"
                   : " %byte = getelementptr i8, i8* %buf, i64 4\n"
                     " %at = bitcast i8* %byte to i32*\n") +
        " %value = load i32, i32* %at\n ret void\n}");
      auto statement = access(ir);
      ASSERT_NE(statement, nullptr);
      JsonExportVisitor visitor;
      statement->accept(visitor);
      const auto * node = visitor.getResult().getAsObject();
      ASSERT_NE(node, nullptr);
      EXPECT_FALSE(node->getString("object").hasValue());
      EXPECT_EQ(node->getInteger("elem_size").getValueOr(0), 4);
      EXPECT_EQ(node->getString("op").getValueOr(""), "load");
    }
}

TEST(AccessWidth, FieldPathLeavesWidthsToObjectAndFieldMetadata)
{
  IndexIr ir("%S = type { i8, i32 }\n"
             "define void @kernel(%S* %p) {\n"
             " %at = getelementptr %S, %S* %p, i64 0, i32 1\n"
             " %value = load i32, i32* %at\n ret void\n}");
  auto statement = access(ir);
  JsonExportVisitor visitor;
  statement->accept(visitor);
  const auto * node = visitor.getResult().getAsObject();
  ASSERT_NE(node, nullptr);
  ASSERT_NE(node->getArray("access_path"), nullptr);
  EXPECT_FALSE(node->getInteger("elem_size").hasValue());
}

std::string pointerLoop(bool cast)
{
  return "@a = global [16 x i32] zeroinitializer\n"
         "define void @kernel() {\nentry:\n"
         " %base = getelementptr [16 x i32], [16 x i32]* @a, i64 0, i64 0\n"
         " br label %header\nheader:\n"
         " %i = phi i64 [0, %entry], [%next, %body]\n"
         " %p = phi i32* [%base, %entry], [%nextp, %body]\n"
         " %test = icmp slt i64 %i, 3\n"
         " br i1 %test, label %body, label %exit\nbody:\n" +
         std::string(cast ? " %asfloat = bitcast i32* %p to float*\n"
                            " %address = bitcast float* %asfloat to i32*\n"
                            " %value = load i32, i32* %address\n"
                          : " %value = load i32, i32* %p\n") +
         " %nextp = getelementptr i32, i32* %p, i64 2\n"
         " %next = add i64 %i, 1\n br label %header\nexit: ret void\n}";
}

TEST(AccessWidth, StrictPointerRecurrenceRetainsCastRejection)
{
  IndexIr ir(pointerLoop(true));
  rejected(ir, "unsupported region pointer cast", true);
}

TEST(AccessWidth, StrictPointerRecurrenceUsesItsSeparateAffineStrideProof)
{
  IndexIr ir(pointerLoop(false));
  auto statement = access(ir, true);
  JsonExportVisitor visitor;
  statement->accept(visitor);
  const auto * node = visitor.getResult().getAsObject();
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->getArray("indices")->front().getAsString().getValueOr(""), "2*i");
  EXPECT_EQ(node->getInteger("elem_size").getValueOr(0), 4);
}

TEST(AccessWidth, StrictPointerRecurrenceRejectsCastsOnIncomingBackedge)
{
  auto source = pointerLoop(false);
  const std::string increment = " %nextp = getelementptr i32, i32* %p, i64 2";
  source.replace(source.find(increment), increment.size(),
    " %asfloat = bitcast i32* %p to float*\n"
    " %nextfloat = getelementptr float, float* %asfloat, i64 2\n"
    " %nextp = bitcast float* %nextfloat to i32*");
  IndexIr ir(source);
  rejected(ir, "unsupported region pointer cast", true);
}

std::string runtimePointerLoop()
{
  auto source = pointerLoop(false);
  const std::string global = "@a = global [16 x i32] zeroinitializer";
  const std::string base =
    " %base = getelementptr [16 x i32], [16 x i32]* @a, i64 0, i64 0";
  source.replace(source.find(global), global.size(), "declare i32* @allocate()");
  source.replace(source.find(base), base.size(), " %base = call i32* @allocate()");
  return source;
}

TEST(AccessWidth, RuntimePointerRecurrenceIsPreservedOnlyByLegacyExtraction)
{
  auto source = runtimePointerLoop();
  IndexIr ir(source);
  auto statement = access(ir);
  ASSERT_NE(statement, nullptr);
  JsonExportVisitor visitor;
  statement->accept(visitor);
  const auto * node = visitor.getResult().getAsObject();
  ASSERT_NE(node, nullptr);
  EXPECT_FALSE(node->getString("object").hasValue());
  EXPECT_EQ(node->getInteger("elem_size").getValueOr(0), 4);
  rejected(ir, "unresolved region access object", true);
}

TEST(AccessWidth, RuntimeStorageDoesNotBypassDynamicStrideRejection)
{
  auto source = runtimePointerLoop();
  source.insert(0, "@stride = global i64 1\n");
  source.insert(source.find(" %base ="), " %stride = load i64, i64* @stride\n");
  const std::string step = "getelementptr i32, i32* %p, i64 2";
  source.replace(source.find(step), step.size(),
                 "getelementptr i32, i32* %p, i64 %stride");
  IndexIr ir(source);
  rejected(ir, "runtime-dependent coefficient");
}

TEST(AccessWidth, StrictCastRejectionPrecedesScaledIndexValidation)
{
  IndexIr ir("@a = global [16 x i32] zeroinitializer\n"
             "define void @kernel() {\nentry:\n"
             " %base = bitcast [16 x i32]* @a to float*\n"
             " br label %header\nheader:\n"
             " %i = phi i64 [0, %entry], [%next, %body]\n"
             " %test = icmp slt i64 %i, 3\n"
             " br i1 %test, label %body, label %exit\nbody:\n"
             " %index = mul i64 %i, 2\n"
             " %at = getelementptr float, float* %base, i64 %index\n"
             " %value = load float, float* %at\n"
             " %next = add i64 %i, 1\n br label %header\nexit: ret void\n}");
  rejected(ir, "unsupported region pointer cast", true);
}

}  // namespace
}  // namespace map::test
