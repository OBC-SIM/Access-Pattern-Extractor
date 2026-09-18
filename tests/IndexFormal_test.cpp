#include <gtest/gtest.h>

#include "../src/index/ScalarFormal.hpp"
#include "helpers/IndexIrFixture.hpp"

namespace map::test
{
namespace
{

TEST(IndexFormal, DoesNotBindPointerParameterAsInteger)
{
  IndexIr ir(R"(define void @kernel(i32* %p) {
    entry: ret void
    })");
  auto * argument = ir.module->getFunction("kernel")->getArg(0);
  EXPECT_FALSE(index::resolveFormalValue(argument, {}));
}

TEST(IndexFormal, DoesNotBindPointerParameterLoadAsInteger)
{
  IndexIr ir(R"(define void @kernel(i32* %p) {
    entry:
      %slot = alloca i32*
      store i32* %p, i32** %slot
      %loaded = load i32*, i32** %slot
      ret void
    })");
  EXPECT_FALSE(index::resolveFormalValue(ir.value("loaded"), {}));
}

TEST(IndexFormal, PreservesUnsignedParameterExtension)
{
  IndexIr ir(indexLoop("%index = zext i32 %n to i64", "0", "4", "1", "i32"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"n"}));
}

TEST(IndexFormal, PreservesUnsignedBytePromotionThenSignedExtension)
{
  IndexIr ir(
    indexLoop("%promoted = zext i8 %n to i32\n"
              "%index = sext i32 %promoted to i64",
              "0", "4", "1", "i8"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"n"}));
}

TEST(IndexFormal, PreservesUnsignedParameterLoadedFromItsSlot)
{
  auto source = indexLoop(
    "%loaded = load i32, i32* %slot\n"
    "%index = zext i32 %loaded to i64",
    "0", "4", "1", "i32");
  source.replace(source.find("entry: br"), 7,
                 "entry: %slot = alloca i32\nstore i32 %n, i32* %slot\n");
  IndexIr ir(source);
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"n"}));
}

TEST(IndexFormal, RejectsNarrowingAnUnknownParameter)
{
  IndexIr ir(
    indexLoop("%small = trunc i64 %n to i32\n"
              "%index = sext i32 %small to i64"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexFormal, RejectsZeroExtensionAfterSignedPromotion)
{
  IndexIr ir(
    indexLoop("%promoted = sext i8 %n to i32\n"
              "%index = zext i32 %promoted to i64",
              "0", "4", "1", "i8"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

TEST(IndexFormal, RejectsConflictingSignedAndUnsignedUses)
{
  IndexIr ir(
    indexLoop("%unsigned = zext i8 %n to i64\n"
              "%other = getelementptr [64 x i32], [64 x i32]* @a, "
              "i64 0, i64 %unsigned\nstore i32 1, i32* %other\n"
              "%index = sext i8 %n to i64",
              "0", "4", "1", "i8"));
  EXPECT_THROW(ir.resolve(), std::invalid_argument);
}

}  // namespace
}  // namespace map::test
