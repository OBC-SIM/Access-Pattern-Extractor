#include "RegionAstValidator.hpp"

#include <algorithm>
#include <stdexcept>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/SourceManager.h"

namespace lat::region
{
namespace
{

int markerKind(const clang::Stmt * statement)
{
  const auto * call = llvm::dyn_cast<clang::CallExpr>(statement);
  if (!call || call->getNumArgs() != 2 || !call->getDirectCallee() ||
      call->getDirectCallee()->getName() != "__builtin_annotation")
    return 0;
  const auto * text = llvm::dyn_cast<clang::StringLiteral>(
    call->getArg(1)->IgnoreParenImpCasts());
  if (!text) return 0;
  if (text->getString() == "yarda.region.begin.v1") return 1;
  if (text->getString() == "yarda.region.end.v1") return -1;
  return 0;
}

void collectMarkers(const clang::Stmt * statement,
                    std::vector<const clang::Stmt *> & markers)
{
  if (!statement) return;
  if (markerKind(statement)) markers.push_back(statement);
  for (const auto * child : statement->children())
    collectMarkers(child, markers);
}

void validateStatement(const clang::Stmt * statement, bool selected)
{
  if (!statement) return;
  if (llvm::isa<clang::AsmStmt, clang::GotoStmt, clang::IndirectGotoStmt,
                clang::LabelStmt>(statement))
    throw std::invalid_argument("region function rejects asm, goto and labels");
  if (selected)
  {
    if (llvm::isa<clang::IfStmt, clang::SwitchStmt, clang::WhileStmt,
                  clang::DoStmt, clang::BreakStmt, clang::ContinueStmt,
                  clang::ReturnStmt, clang::AbstractConditionalOperator>(
          statement))
      throw std::invalid_argument("unsupported control flow in selected body");
    if (const auto * binary = llvm::dyn_cast<clang::BinaryOperator>(statement))
      if (binary->isLogicalOp())
        throw std::invalid_argument("conditional expression in selected body");
  }
  for (const auto * child : statement->children())
    validateStatement(child, selected);
}

void validateFunction(clang::FunctionDecl & function, SourceRegions & source)
{
  auto * body = llvm::dyn_cast<clang::CompoundStmt>(function.getBody());
  if (!body) return;
  if (function.hasAttr<clang::AsmLabelAttr>())
    throw std::invalid_argument("custom assembler symbols are unsupported");
  std::vector<const clang::Stmt *> markers;
  collectMarkers(body, markers);
  if (markers.empty())
  {
    try
    {
      for (const auto * statement : body->body())
      {
        if (statement == body->body_back() &&
            llvm::isa<clang::ReturnStmt>(statement))
        {
          for (const auto * child : statement->children())
            validateStatement(child, true);
        }
        else
        {
          validateStatement(statement, true);
        }
      }
    }
    catch (const std::invalid_argument & error)
    {
      source.functionErrors[function.getNameAsString()] = error.what();
    }
    return;
  }
  const auto & manager = function.getASTContext().getSourceManager();
  for (const auto * marker : markers)
  {
    auto found =
      std::find_if(source.boundaries.begin(), source.boundaries.end(),
                   [&](const SourceBoundary & boundary) {
                     return boundary.location ==
                              manager.getExpansionLoc(marker->getBeginLoc()) &&
                            boundary.begin == (markerKind(marker) == 1);
                   });
    if (found == source.boundaries.end() || found->matched)
      throw std::invalid_argument("reserved region transport used directly");
    found->matched = true;
  }
  if (markers.size() != 2 || markerKind(markers[0]) != 1 ||
      markerKind(markers[1]) != -1)
    throw std::invalid_argument(
      "expected one ordered begin/end pair per function");
  for (const auto * marker : markers)
    if (std::find(body->body_begin(), body->body_end(), marker) ==
        body->body_end())
      throw std::invalid_argument(
        "region boundaries must be top-level complete statements");
  for (const auto * attribute : function.specific_attrs<clang::AnnotateAttr>())
    if (attribute->getAnnotation() == "ape.inline" ||
        attribute->getAnnotation() == "yard.inline")
      throw std::invalid_argument("region conflicts with inline annotation");
  bool selected = false;
  for (const auto * statement : body->body())
  {
    if (statement == markers[0])
    {
      selected = true;
      continue;
    }
    if (statement == markers[1])
    {
      selected = false;
      continue;
    }
    validateStatement(statement, selected);
  }
  source.functions.insert(function.getNameAsString());
}

class Validator final : public clang::ASTConsumer
{
public:
  explicit Validator(SourceRegions & source) : source_(source) {}

  bool HandleTopLevelDecl(clang::DeclGroupRef declarations) override
  {
    for (auto * declaration : declarations)
    {
      auto * function = llvm::dyn_cast<clang::FunctionDecl>(declaration);
      if (!function || !function->doesThisDeclarationHaveABody()) continue;
      try
      {
        validateFunction(*function, source_);
        if (source_.functions.count(function->getNameAsString()) &&
            !function->hasAttr<clang::UsedAttr>())
          function->addAttr(
            clang::UsedAttr::CreateImplicit(function->getASTContext()));
      }
      catch (const std::exception & error)
      {
        report(function->getASTContext(), error.what());
        return false;
      }
    }
    return true;
  }

  void HandleTranslationUnit(clang::ASTContext & context) override
  {
    if (context.getDiagnostics().hasErrorOccurred()) return;
    try
    {
      for (auto * declaration : context.getTranslationUnitDecl()->decls())
      {
        if (context.getSourceManager().isWrittenInMainFile(
              declaration->getLocation()) &&
            (declaration->hasAttr<clang::AsmLabelAttr>() ||
             llvm::isa<clang::FileScopeAsmDecl>(declaration)))
          throw std::invalid_argument(
            "custom assembler symbols are unsupported");
      }
      for (const auto & boundary : source_.boundaries)
        if (!boundary.matched)
          throw std::invalid_argument(
            "source boundary missing from function AST");
    }
    catch (const std::exception & error)
    {
      report(context, error.what());
    }
  }

private:
  void report(clang::ASTContext & context, const char * message)
  {
    auto & diagnostics = context.getDiagnostics();
    const auto id =
      diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
    diagnostics.Report(id) << message;
  }

  SourceRegions & source_;
};

}  // namespace

std::unique_ptr<clang::ASTConsumer>
makeRegionAstValidator(SourceRegions & source)
{
  return std::make_unique<Validator>(source);
}

}  // namespace lat::region
