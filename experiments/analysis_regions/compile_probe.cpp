#include "region_capture.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"

namespace {

constexpr const char* pipelineDiagnostic =
    "region pipeline requires O0 without LTO, plugins or instrumentation";

class CompileProbe : public clang::EmitLLVMOnlyAction {
  public:
    explicit CompileProbe(std::string output) : output_(std::move(output)) {}

  protected:
    bool BeginSourceFileAction(clang::CompilerInstance& compiler) override {
        const auto& options = compiler.getCodeGenOpts();
        if (options.OptimizationLevel != 0 || options.PrepareForLTO || options.PrepareForThinLTO) {
            report(pipelineDiagnostic);
            return false;
        }
        return clang::EmitLLVMOnlyAction::BeginSourceFileAction(compiler);
    }

    void EndSourceFileAction() override {
        clang::EmitLLVMOnlyAction::EndSourceFileAction();
        if (getCompilerInstance().getDiagnostics().hasErrorOccurred())
            return;
        auto module = takeModule();
        if (!module) {
            report("compiler did not emit an LLVM module");
            return;
        }
        try {
            region_probe::capture(*module);
            llvm::LoopAnalysisManager loops;
            llvm::FunctionAnalysisManager functions;
            llvm::CGSCCAnalysisManager cgscc;
            llvm::ModuleAnalysisManager modules;
            llvm::PassBuilder builder;
            builder.registerModuleAnalyses(modules);
            builder.registerCGSCCAnalyses(cgscc);
            builder.registerFunctionAnalyses(functions);
            builder.registerLoopAnalyses(loops);
            builder.crossRegisterProxies(loops, functions, cgscc, modules);
            llvm::ModulePassManager pipeline;
            if (auto error = builder.parsePassPipeline(pipeline, "function(mem2reg),loop-simplify"))
                throw std::runtime_error(llvm::toString(std::move(error)));
            pipeline.run(*module, modules);
            if (llvm::verifyModule(*module, &llvm::errs()))
                throw std::runtime_error("invalid canonical module");
            std::error_code error;
            llvm::raw_fd_ostream output(output_, error, llvm::sys::fs::OF_Text);
            if (error)
                throw std::runtime_error(error.message());
            module->print(output, nullptr);
            output.flush();
            if (output.has_error()) {
                output.clear_error();
                throw std::runtime_error("cannot write IR");
            }
        } catch (const std::exception& error) {
            report(error.what());
        }
    }

  private:
    void report(llvm::StringRef message) {
        auto& diagnostics = getCompilerInstance().getDiagnostics();
        const auto id = diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
        diagnostics.Report(id) << message;
    }

    std::string output_;
};

} // namespace

/**
 * @brief Run the restricted compiler experiment without publishing LAT.
 * @param argc Number of command-line arguments.
 * @param argv Borrowed, non-null command-line argument array.
 * @return Zero on successful IR emission, nonzero on invalid input or failure.
 */
int main(int argc, char** argv) {
    llvm::llvm_shutdown_obj shutdown;
    if (argc != 3 && argc != 4) {
        llvm::errs() << "usage: region_compile_probe SOURCE OUTPUT [optimization-test-option]\n";
        return 2;
    }
    auto source = llvm::MemoryBuffer::getFile(argv[1]);
    if (!source) {
        llvm::errs() << source.getError().message() << '\n';
        return 1;
    }
    std::vector<std::string> arguments{"-xc", "-std=c11", "-O0",
                                       "-g",  "-Xclang",  "-disable-O0-optnone"};
    if (argc == 4) {
        const std::vector<std::string> optimizationOptions{
            "-O0", "-O1",    "-O2",   "-O3",        "-Os",
            "-Oz", "-Ofast", "-flto", "-flto=full", "-flto=thin"};
        if (std::find(optimizationOptions.begin(), optimizationOptions.end(), argv[3]) ==
            optimizationOptions.end()) {
            llvm::errs() << "error: " << pipelineDiagnostic << '\n';
            return 1;
        }
        arguments.emplace_back(argv[3]);
    }
    return clang::tooling::runToolOnCodeWithArgs(std::make_unique<CompileProbe>(argv[2]),
                                                 (*source)->getBuffer(), arguments, argv[1],
                                                 REGION_CLANG_EXECUTABLE)
               ? 0
               : 1;
}
