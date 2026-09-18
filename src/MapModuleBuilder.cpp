#include "MapModuleBuilder.hpp"

#include <stdexcept>

#include "AccessMetadataBuilder.hpp"
#include "IrHelpers.hpp"
#include "JsonExportVisitor.hpp"
#include "StatementBuilder.hpp"

using namespace llvm;

namespace map
{
namespace
{

constexpr llvm::StringLiteral AnalyzeAnnotation = "ape.analyze";
constexpr llvm::StringLiteral InlineAnnotation = "ape.inline";

}  // namespace

std::set<const Function *> exportedMapFunctions(Module & module,
                                                const AnalysisRegions * regions)
{
  std::set<const Function *> definitions, selected;
  for (auto & function : module)
  {
    if (function.isDeclaration()) continue;
    definitions.insert(&function);
    if (hasFunctionAnnotation(function, AnalyzeAnnotation) ||
        hasFunctionAnnotation(function, InlineAnnotation) ||
        (regions && regions->count(function.getName().str())))
      selected.insert(&function);
  }
  return selected.empty() ? definitions : selected;
}

llvm::json::Object buildMapModule(Module & M, ModuleAnalysisManager & MAM,
                                  const AnalysisRegions * regions)
{
  const auto exported = exportedMapFunctions(M, regions);
  auto & FAM =
    MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  std::set<const Function *> analyzeFuncs;
  std::set<const Function *> inlineFuncs;
  for (Function & F : M)
  {
    if (F.isDeclaration()) continue;
    if (hasFunctionAnnotation(F, AnalyzeAnnotation) ||
        (regions && regions->count(F.getName().str())))
      analyzeFuncs.insert(&F);
    if (hasFunctionAnnotation(F, InlineAnnotation)) inlineFuncs.insert(&F);
  }

  map::AccessMetadata metadata = buildAccessMetadata(M);
  llvm::json::Array moduleFuncs;
  for (Function & F : M)
  {
    if (!exported.count(&F)) continue;

    auto & LI = FAM.getResult<LoopAnalysis>(F);
    auto & SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
    NameMap names = buildDebugNameMap(F);

    std::vector<std::unique_ptr<Statement>> root;
    const auto * selection = regions && regions->count(F.getName().str())
                               ? &regions->at(F.getName().str())
                               : nullptr;
    buildRootStatements(F, LI, SE, names, inlineFuncs, metadata, root,
                        selection, regions != nullptr);

    llvm::json::Array params;
    for (Argument & Arg : F.args()) params.push_back(getValueName(&Arg, names));

    map::JsonExportVisitor vis;
    llvm::json::Array bodyJson;
    for (auto & stmt : root)
    {
      stmt->accept(vis);
      bodyJson.push_back(vis.getResult());
    }

    llvm::json::Object funcEntry;
    llvm::json::Array annotations;
    if (analyzeFuncs.count(&F)) annotations.push_back(AnalyzeAnnotation.str());
    if (inlineFuncs.count(&F)) annotations.push_back(InlineAnnotation.str());
    if (selection)
    {
      if (inlineFuncs.count(&F))
        throw std::invalid_argument("region conflicts with inline annotation");
      funcEntry["analysis_scope"] =
        llvm::json::Object{{"kind", "region"}, {"name", "APE_ANALYZE"}};
    }
    funcEntry["function"] = F.getName().str();
    funcEntry["params"] = std::move(params);
    funcEntry["annotations"] = std::move(annotations);
    funcEntry["body"] = std::move(bodyJson);
    moduleFuncs.push_back(std::move(funcEntry));
  }

  return llvm::json::Object{{"schema_version", 2},
                            {"metadata", map::toJson(metadata)},
                            {"functions", std::move(moduleFuncs)}};
}

}  // namespace map
