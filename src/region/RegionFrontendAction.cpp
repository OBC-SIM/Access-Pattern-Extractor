#include "RegionFrontendAction.hpp"

#include <optional>
#include <stdexcept>

#include "AnalysisRegion.hpp"
#include "MapModuleBuilder.hpp"
#include "RegionAstValidator.hpp"
#include "RegionDriverOptions.hpp"
#include "clang/Basic/Version.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"

#if CLANG_VERSION_MAJOR != 14
#error "The region pipeline requires matching Clang 14 development headers"
#endif

namespace map::region
{
namespace
{

class Diagnostics final : public clang::DiagnosticConsumer
{
public:
  explicit Diagnostics(std::string & errors) : errors_(errors) {}
  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic & diagnostic) override
  {
    clang::DiagnosticConsumer::HandleDiagnostic(level, diagnostic);
    if (level < clang::DiagnosticsEngine::Error) return;
    llvm::SmallString<256> message;
    diagnostic.FormatDiagnostic(message);
    if (!errors_.empty()) errors_ += '\n';
    errors_ += message.str().str();
  }

private:
  std::string & errors_;
};

class RegionAction final : public clang::EmitLLVMOnlyAction
{
public:
  RegionAction(std::optional<llvm::json::Object> & result, std::string & errors)
    : result_(result), errors_(errors)
  {
  }

protected:
  bool BeginInvocation(clang::CompilerInstance & compiler) override
  {
    compiler.getDiagnostics().setClient(new Diagnostics(errors_), true);
    return true;
  }

  bool BeginSourceFileAction(clang::CompilerInstance & compiler) override
  {
    const auto & options = compiler.getCodeGenOpts();
    if (options.OptimizationLevel != 0 || options.PrepareForLTO ||
        options.PrepareForThinLTO)
    {
      report("region pipeline requires O0 without LTO");
      return false;
    }
    registerRegionPragmas(compiler.getPreprocessor(), source_);
    return clang::EmitLLVMOnlyAction::BeginSourceFileAction(compiler);
  }

  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance & compiler, llvm::StringRef filename) override
  {
    std::vector<std::unique_ptr<clang::ASTConsumer>> consumers;
    consumers.push_back(makeRegionAstValidator(source_));
    consumers.push_back(
      clang::EmitLLVMOnlyAction::CreateASTConsumer(compiler, filename));
    return std::make_unique<clang::MultiplexConsumer>(std::move(consumers));
  }

  void EndSourceFileAction() override
  {
    clang::EmitLLVMOnlyAction::EndSourceFileAction();
    if (getCompilerInstance().getDiagnostics().hasErrorOccurred()) return;
    try
    {
      auto module = takeModule();
      if (!module)
        throw std::invalid_argument("compiler did not produce a module");
      auto regions = captureAnalysisRegions(*module, source_.functions);
      const auto exported = exportedMapFunctions(*module, &regions);
      for (const auto & function : *module)
      {
        const auto error =
          source_.functionErrors.find(function.getName().str());
        if (exported.count(&function) && error != source_.functionErrors.end())
          throw std::invalid_argument(error->first + ": " + error->second);
      }
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
      if (auto error = builder.parsePassPipeline(pipeline,
                                                 "function(mem2reg),loop-"
                                                 "simplify"))
        throw std::invalid_argument(llvm::toString(std::move(error)));
      pipeline.run(*module, modules);
      if (llvm::verifyModule(*module, &llvm::errs()))
        throw std::invalid_argument("invalid canonical region module");
      validateAnalysisRegions(*module, regions, functions);
      result_.emplace(buildMapModule(*module, modules, &regions));
    }
    catch (const std::exception & error)
    {
      report(error.what());
    }
  }

private:
  void report(llvm::StringRef message)
  {
    auto & diagnostics = getCompilerInstance().getDiagnostics();
    const auto id =
      diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
    diagnostics.Report(id) << message;
  }

  SourceRegions source_;
  std::optional<llvm::json::Object> & result_;
  std::string & errors_;
};

}  // namespace

llvm::json::Object
compileRegionSource(llvm::StringRef source, const std::string & filename,
                    const std::vector<std::string> & arguments)
{
  if (!llvm::StringRef(filename).endswith(".c"))
    throw std::invalid_argument(
      "region frontend requires a C source (.c), not imported IR");
  const auto compilerArguments = regionCompilerArguments(arguments);
  std::optional<llvm::json::Object> result;
  std::string errors;
  const bool success = clang::tooling::runToolOnCodeWithArgs(
    std::make_unique<RegionAction>(result, errors), source, compilerArguments,
    filename, YARDA_REGION_CLANG);
  if (!success || !result)
    throw std::invalid_argument(errors.empty() ? "region compilation failed"
                                               : errors);
  return std::move(*result);
}

}  // namespace map::region
