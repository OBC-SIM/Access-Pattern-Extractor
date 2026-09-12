#pragma once

#include <stdexcept>
#include <string>

#include "IrHelpers.hpp"
#include "LatModuleBuilder.hpp"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"

namespace lat::test
{

/** @brief Own the IR and analysis managers for one independent index test. */
struct IndexIr
{
  llvm::LLVMContext context;
  std::unique_ptr<llvm::Module> module;
  llvm::LoopAnalysisManager loops;
  llvm::FunctionAnalysisManager functions;
  llvm::CGSCCAnalysisManager cgscc;
  llvm::ModuleAnalysisManager modules;

  /**
   * @brief Parse a complete module and register its LLVM analyses.
   * @param source Valid LLVM 14 assembly, copied by the parser.
   */
  explicit IndexIr(llvm::StringRef source)
  {
    llvm::SMDiagnostic diagnostic;
    module = llvm::parseAssemblyString(source, diagnostic, context);
    if (!module) throw std::runtime_error("invalid index test IR");
    llvm::PassBuilder builder;
    builder.registerModuleAnalyses(modules);
    builder.registerCGSCCAnalyses(cgscc);
    builder.registerFunctionAnalyses(functions);
    builder.registerLoopAnalyses(loops);
    builder.crossRegisterProxies(loops, functions, cgscc, modules);
  }

  /**
   * @brief Find a named instruction without depending on its position.
   * @param name Instruction name in kernel.
   * @return Non-null borrowed instruction; throws if the fixture is invalid.
   */
  llvm::Instruction * value(llvm::StringRef name)
  {
    for (auto & instruction : llvm::instructions(module->getFunction("kernel")))
      if (instruction.getName() == name) return &instruction;
    throw std::runtime_error("missing index test instruction");
  }

  /** @brief Return the kernel's borrowed ScalarEvolution analysis. */
  llvm::ScalarEvolution & evolution()
  {
    return functions.getResult<llvm::ScalarEvolutionAnalysis>(
      *module->getFunction("kernel"));
  }

  /**
   * @brief Resolve the named index using the production compatibility API.
   * @param names Optional debug names; ownership stays with the caller.
   * @return LAT index vector, or the production rejection exception.
   */
  std::vector<std::string> resolve(const NameMap & names = {})
  {
    return resolveIndex(value("index"), evolution(), names);
  }
};

/**
 * @brief Construct a header-tested loop with an explicit index computation.
 * @param body LLVM instructions defining index.
 * @param start Initial IV operand.
 * @param bound Exclusive comparison operand.
 * @param step Increment operand.
 * @param type IV integer type.
 * @param predicate Loop-continuation comparison.
 * @param indexType GEP index type, independent of the IV type for cast tests.
 * @return Complete LLVM assembly; no optimization pass changes its semantics.
 */
inline std::string indexLoop(const std::string & body,
                             const std::string & start = "0",
                             const std::string & bound = "4",
                             const std::string & step = "1",
                             const std::string & type = "i64",
                             const std::string & predicate = "slt",
                             const std::string & indexType = "i64")
{
  return "@a = global [64 x i32] zeroinitializer\n"
         "define void @kernel(" +
         type +
         " %n) {\n"
         "entry: br label %header\nheader:\n"
         " %i = phi " +
         type + " [" + start +
         ", %entry], [%next, %body]\n"
         " %test = icmp " +
         predicate + " " + type + " %i, " + bound +
         "\n"
         " br i1 %test, label %body, label %exit\nbody:\n" +
         body +
         "\n"
         " %p = getelementptr [64 x i32], [64 x i32]* @a, i64 0, " +
         indexType +
         " %index\n store i32 1, i32* %p\n"
         " %next = add " +
         type + " %i, " + step +
         "\n"
         " br label %header\nexit: ret void\n}\n";
}

}  // namespace lat::test
