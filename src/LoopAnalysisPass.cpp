#include "LatModuleBuilder.hpp"
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

struct LoopAnnotatedTracePass : public PassInfoMixin<LoopAnnotatedTracePass>
{
  PreservedAnalyses run(Module & M, ModuleAnalysisManager & MAM)
  {
    if (lat::hasRegionTransport(M))
    {
      // LLVM 14's default error handler exits before output destructors run.
      sys::RunInterruptHandlers();
      M.getContext().emitError(
        "[LoopAnnotatedTrace] region transport requires yarda_region_lat");
      return PreservedAnalyses::all();
    }
    auto root = lat::buildLatModule(M, MAM);
    llvm::StringRef stem = llvm::sys::path::stem(M.getModuleIdentifier());
    std::string filename = stem.str() + "_ape.json";
    std::error_code EC;
    raw_fd_ostream OS(filename, EC, sys::fs::OF_Text);
    if (EC)
    {
      errs() << "[LoopAnnotatedTrace] cannot open " << filename << ": "
             << EC.message() << "\n";
      return PreservedAnalyses::all();
    }

    OS << llvm::json::Value(std::move(root));
    errs() << "[LoopAnnotatedTrace] wrote " << filename << "\n";
    return PreservedAnalyses::all();
  }
};

}  // namespace

llvm::PassPluginLibraryInfo getLoopAnnotatedTracePluginInfo()
{
  return {LLVM_PLUGIN_API_VERSION, "LoopAnnotatedTrace", LLVM_VERSION_STRING,
          [](PassBuilder & PB) {
            PB.registerPipelineParsingCallback(
              [](StringRef Name, ModulePassManager & MPM,
                 ArrayRef<PassBuilder::PipelineElement>) {
                if (Name == "loop-annotated-trace")
                {
                  MPM.addPass(LoopAnnotatedTracePass());
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
  return getLoopAnnotatedTracePluginInfo();
}
