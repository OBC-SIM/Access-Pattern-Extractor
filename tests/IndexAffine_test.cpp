#include <gtest/gtest.h>

#include "helpers/IndexIrFixture.hpp"
#include "helpers/IndexRejection.hpp"

namespace map::test
{
namespace
{

TEST(IndexAffine, RejectsPointerToIntegerIndex)
{
  IndexIr ir(R"(@a = global [64 x i32] zeroinitializer
    define void @kernel(i32* %source) {
    entry:
      %index = ptrtoint i32* %source to i64
      %p = getelementptr [64 x i32], [64 x i32]* @a, i64 0, i64 %index
      store i32 1, i32* %p
      ret void
    })");
  expectIndexRejected(ir, "expression is outside the affine grammar");
}

TEST(IndexAffine, RejectsPointerToIntegerTermCombinedWithInduction)
{
  auto source = indexLoop("%address = ptrtoint i32* %source to i64\n"
                          "%index = add i64 %address, %i");
  source.replace(source.find("i64 %n"), 6, "i64 %n, i32* %source");
  IndexIr ir(source);
  expectIndexRejected(ir, "expression is outside the affine grammar");
}

TEST(IndexAffine, PreservesScaleOnDescendingNonunitInduction)
{
  IndexIr ir(indexLoop("%twice = mul i64 %i, 2\n%index = add i64 %twice, 1",
                       "5", "0", "-2", "i64", "sgt"));
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"2*i+1"}));
}

TEST(IndexAffine, RejectsNonintegralRelationToEmittedInduction)
{
  auto source =
    indexLoop("%other = add i64 %j, 1\n%index = add i64 %j, 0", "0", "6", "2");
  source.insert(source.find(" %i = phi"),
                " %j = phi i64 [0, %entry], [%other, %body]\n");
  IndexIr ir(source);
  expectIndexRejected(ir, "nonintegral emitted IV coefficient");
}

TEST(IndexAffine, RejectsScaledSignedNarrowing)
{
  IndexIr ir(indexLoop("%scaled = mul i64 %i, 2\n"
                       "%index = trunc i64 %scaled to i8",
                       "63", "66", "1", "i64", "slt", "i8"));
  expectIndexRejected(ir, "index may wrap or narrow");
}

TEST(IndexAffine, RejectsScaledWrappingRecurrence)
{
  IndexIr ir(
    indexLoop("%index = mul i8 %i, 3", "40", "44", "1", "i8", "slt", "i8"));
  expectIndexRejected(ir, "index may wrap or narrow");
}

TEST(IndexAffine, PreservesProvedUnsignedByteArithmetic)
{
  IndexIr ir(R"(@a = global [512 x i32] zeroinitializer
    define void @kernel(i8 zeroext %x) {
    entry:
      %wide = zext i8 %x to i32
      %scaled = mul i32 %wide, 2
      %index = add i32 %scaled, 1
      %p = getelementptr [512 x i32], [512 x i32]* @a, i64 0, i32 %index
      store i32 1, i32* %p
      ret void
    })");
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"2*x+1"}));
}

TEST(IndexAffine, MergesWidenedFormalAndItsUniquelyInitializedLoad)
{
  IndexIr ir(R"(@a = global [64 x i32] zeroinitializer
    define void @kernel(i32 %x) {
    entry:
      %slot = alloca i32
      store i32 %x, i32* %slot
      %loaded = load i32, i32* %slot
      %direct = sext i32 %x to i64
      %indirect = sext i32 %loaded to i64
      %index = add i64 %direct, %indirect
      %p = getelementptr [64 x i32], [64 x i32]* @a, i64 0, i64 %index
      store i32 1, i32* %p
      ret void
    })");
  // Two copies of the same signed i32 value sum exactly within signed i64.
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"2*x"}));
}

TEST(IndexAffine, PreservesProvedBooleanArithmetic)
{
  IndexIr ir(R"(@a = global [4 x i32] zeroinitializer
    define void @kernel(i1 zeroext %x) {
    entry:
      %wide = zext i1 %x to i64
      %scaled = mul i64 %wide, 2
      %index = add i64 %scaled, 1
      %p = getelementptr [4 x i32], [4 x i32]* @a, i64 0, i64 %index
      store i32 1, i32* %p
      ret void
    })");
  EXPECT_EQ(ir.resolve(), (std::vector<std::string>{"2*x+1"}));
}

TEST(IndexAffine, RejectsUnprovedFormalArithmeticOverflow)
{
  IndexIr ir(R"(@a = global [512 x i32] zeroinitializer
    define void @kernel(i32 %x) {
    entry:
      %index = mul i32 %x, 2
      %p = getelementptr [512 x i32], [512 x i32]* @a, i64 0, i32 %index
      store i32 1, i32* %p
      ret void
    })");
  expectIndexRejected(ir, "index may wrap or narrow");
}

TEST(IndexAffine, RejectsArithmeticWithAnUnserializableFormalName)
{
  IndexIr ir(R"(@a = global [512 x i32] zeroinitializer
    define void @kernel(i8 zeroext %x) {
    entry:
      %wide = zext i8 %x to i32
      %index = mul i32 %wide, 2
      %p = getelementptr [512 x i32], [512 x i32]* @a, i64 0, i32 %index
      store i32 1, i32* %p
      ret void
    })");
  ir.module->getFunction("kernel")->getArg(0)->setName("x.not_an_identifier");
  expectIndexRejected(ir, "variable name is outside the affine grammar");
}

TEST(IndexAffine, RejectsSiblingLoopCaptureBeforeMergingDebugNames)
{
  IndexIr ir(R"(@a = global [512 x i32] zeroinitializer
    define void @kernel() {
    entry: br label %first
    first:
      %i = phi i64 [0, %entry], [%inext, %first.body]
      %it = icmp slt i64 %i, 3
      br i1 %it, label %first.body, label %between
    first.body:
      %inext = add i64 %i, 1
      br label %first
    between: br label %second
    second:
      %j = phi i64 [0, %between], [%jnext, %second.body]
      %jt = icmp slt i64 %j, 3
      br i1 %jt, label %second.body, label %exit
    second.body:
      %index = add i64 %i, %j
      %p = getelementptr [512 x i32], [512 x i32]* @a, i64 0, i64 %index
      store i32 1, i32* %p
      %jnext = add i64 %j, 1
      br label %second
    exit: ret void
    })");
  expectIndexRejected(ir, "index is not bound to an emitted loop",
                      {{ir.value("i"), "same"}, {ir.value("j"), "same"}});
}

}  // namespace
}  // namespace map::test
