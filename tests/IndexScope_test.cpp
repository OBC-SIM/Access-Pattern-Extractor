#include <gtest/gtest.h>

#include "AccessBuilder.hpp"
#include "helpers/IndexIrFixture.hpp"

namespace map::test
{
namespace
{

TEST(IndexScope, RejectsExitAccessUsingTheHeaderPhi)
{
  auto source = indexLoop("%index = add i64 %i, 0");
  source.replace(source.find("exit: ret void"), 14,
                 "exit: %outside = getelementptr [64 x i32], [64 x i32]* @a, "
                 "i64 0, i64 %i\nstore i32 7, i32* %outside\nret void");
  IndexIr ir(source);
  EXPECT_THROW(buildMapModule(*ir.module, ir.modules), std::invalid_argument);
}

TEST(IndexScope, RejectsExitAccessWhenItsGepWasDefinedInsideTheLoop)
{
  const std::string source =
    "@a = global [64 x i32] zeroinitializer\n"
    "define void @kernel() {\nentry: br label %header\nheader:\n"
    " %i = phi i64 [0, %entry], [%next, %body]\n"
    " %p = getelementptr [64 x i32], [64 x i32]* @a, i64 0, i64 %i\n"
    " %test = icmp slt i64 %i, 4\n"
    " br i1 %test, label %body, label %exit\nbody:\n"
    " store i32 1, i32* %p\n %next = add i64 %i, 1\n"
    " br label %header\nexit: store i32 7, i32* %p\nret void\n}\n";
  IndexIr ir(source);
  EXPECT_THROW(buildMapModule(*ir.module, ir.modules), std::invalid_argument);
}

TEST(IndexScope, RejectsLegacyIndexQueryOutsideTheInductionScope)
{
  auto source = indexLoop("%index = add i64 %i, 0");
  source.replace(source.find("exit: ret void"), 14,
                 "exit: %outside = getelementptr [64 x i32], [64 x i32]* @a, "
                 "i64 0, i64 %i\nstore i32 7, i32* %outside\nret void");
  IndexIr ir(source);
  auto * gep = llvm::cast<llvm::GEPOperator>(ir.value("outside"));
  EXPECT_THROW(getIndexVars(gep, ir.evolution(), {}), std::invalid_argument);
}

TEST(IndexScope, RejectsAnEscapedInductionPassedToInlineCall)
{
  auto source = indexLoop("%index = add i64 %i, 0");
  source.replace(source.find("exit: ret void"), 14,
                 "exit: call void @touch(i64 %i)\nret void");
  source += "define void @touch(i64 %n) { ret void }\n";
  IndexIr ir(source);
  auto * callee = ir.module->getFunction("touch");
  auto * call = llvm::cast<llvm::CallInst>(*callee->user_begin());
  EXPECT_THROW(makeAccessFromInstr(*call, ir.evolution(), {}, {}, {callee},
                                   *ir.module->getFunction("kernel")),
               std::invalid_argument);
}

}  // namespace
}  // namespace map::test
