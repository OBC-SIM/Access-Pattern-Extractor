#pragma once

#include "AnalysisRegion.hpp"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"

namespace lat::test
{

inline constexpr char defaultRegionIr[] = R"(
@a = global i32 0
@begin = constant [22 x i8] c"yarda.region.begin.v1\00"
@end = constant [20 x i8] c"yarda.region.end.v1\00"
declare i32 @llvm.annotation.i32(i32, i8*, i8*, i32)
define void @kernel() {
entry:
  store i32 0, i32* @a
  %b = call i32 @llvm.annotation.i32(i32 0, i8* getelementptr
    ([22 x i8], [22 x i8]* @begin, i32 0, i32 0), i8* null, i32 1)
  br label %header
header:
  %i = phi i32 [0, %entry], [%next, %body]
  %test = icmp slt i32 %i, 3
  br i1 %test, label %body, label %exit
body:
  %read = load i32, i32* @a
  store i32 %read, i32* @a
  %next = add nsw i32 %i, 1
  br label %header
exit:
  %e = call i32 @llvm.annotation.i32(i32 0, i8* getelementptr
    ([20 x i8], [20 x i8]* @end, i32 0, i32 0), i8* null, i32 2)
  store i32 2, i32* @a
  ret void
}
)";

struct RegionIr
{
  llvm::LLVMContext context;
  std::unique_ptr<llvm::Module> module;
  llvm::LoopAnalysisManager loops;
  llvm::FunctionAnalysisManager functions;
  llvm::CGSCCAnalysisManager cgscc;
  llvm::ModuleAnalysisManager modules;

  explicit RegionIr(llvm::StringRef source = defaultRegionIr)
  {
    llvm::SMDiagnostic diagnostic;
    module = llvm::parseAssemblyString(source, diagnostic, context);
    if (!module) throw std::runtime_error("invalid test IR");
    llvm::PassBuilder builder;
    builder.registerModuleAnalyses(modules);
    builder.registerCGSCCAnalyses(cgscc);
    builder.registerFunctionAnalyses(functions);
    builder.registerLoopAnalyses(loops);
    builder.crossRegisterProxies(loops, functions, cgscc, modules);
  }
};

}  // namespace lat::test
