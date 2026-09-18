#include <optional>
#include <stdexcept>
#include <string>

#include "MapModuleBuilder.hpp"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace
{

struct MemoryAccessPatternsPass : public PassInfoMixin<MemoryAccessPatternsPass>
{
  PreservedAnalyses run(Module & M, ModuleAnalysisManager & MAM)
  {
    if (map::hasRegionTransport(M))
    {
      // LLVM 14's default error handler exits before output destructors run.
      sys::RunInterruptHandlers();
      M.getContext().emitError(
        "[MemoryAccessPatterns] region transport requires yarda_region_map");
      return PreservedAnalyses::all();
    }
    llvm::json::Object root;
    std::optional<std::string> failure;
    try
    {
      root = map::buildMapModule(M, MAM);
    }
    catch (const std::invalid_argument & error)
    {
      failure = error.what();
    }
    if (failure)
    {
      // Finish unwinding the C++ exception before a host handler can exit.
      sys::RunInterruptHandlers();
      M.getContext().emitError(llvm::Twine("[MemoryAccessPatterns] ") + *failure);
      return PreservedAnalyses::all();
    }
    llvm::StringRef stem = llvm::sys::path::stem(M.getModuleIdentifier());
    std::string filename = stem.str() + "_ape.json";
    std::error_code EC;
    raw_fd_ostream OS(filename, EC, sys::fs::OF_Text);
    if (EC)
    {
      errs() << "[MemoryAccessPatterns] cannot open " << filename << ": "
             << EC.message() << "\n";
      return PreservedAnalyses::all();
    }

    OS << llvm::json::Value(std::move(root));
    errs() << "[MemoryAccessPatterns] wrote " << filename << "\n";
    return PreservedAnalyses::all();
  }
};

}  // namespace

llvm::PassPluginLibraryInfo getMemoryAccessPatternsPluginInfo()
{
  return {LLVM_PLUGIN_API_VERSION, "MemoryAccessPatterns", LLVM_VERSION_STRING,
          [](PassBuilder & PB) {
            PB.registerPipelineParsingCallback(
              [](StringRef Name, ModulePassManager & MPM,
                 ArrayRef<PassBuilder::PipelineElement>) {
                if (Name == "loop-annotated-trace")
                {
                  MPM.addPass(MemoryAccessPatternsPass());
                  return true;
                }
                return false;
              });
          }};
}

#ifndef LLVM_ATTRIBUTE_WEAK
#define LLVM_ATTRIBUTE_WEAK __attribute__((weak))
#endif
#ifndef LLVM_ATTRIBUTE_VISIBILITY_DEFAULT
#define LLVM_ATTRIBUTE_VISIBILITY_DEFAULT __attribute__((visibility("default")))
#endif

extern "C" LLVM_ATTRIBUTE_WEAK
  LLVM_ATTRIBUTE_VISIBILITY_DEFAULT ::llvm::PassPluginLibraryInfo
  llvmGetPassPluginInfo()
{
  return getMemoryAccessPatternsPluginInfo();
}
