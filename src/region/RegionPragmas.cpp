#include "RegionPragmas.hpp"

#include <array>
#include <memory>

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Pragma.h"
#include "clang/Lex/Preprocessor.h"

namespace lat::region
{
namespace
{

class BoundaryPragma final : public clang::PragmaHandler
{
public:
  BoundaryPragma(bool begin, SourceRegions & source)
    : PragmaHandler(begin ? "APE_ANALYZE_BEGIN" : "APE_ANALYZE_END")
    , begin_(begin)
    , source_(source)
  {
  }

  void HandlePragma(clang::Preprocessor & pp, clang::PragmaIntroducer intro,
                    clang::Token &) override
  {
    const auto reject = [&](const char * message) {
      const auto id = pp.getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error, "%0");
      pp.Diag(intro.Loc, id) << message;
    };
    clang::Token tail;
    pp.LexUnexpandedToken(tail);
    if (intro.Kind != clang::PIK_HashPragma || intro.Loc.isMacroID() ||
        !pp.getSourceManager().isWrittenInMainFile(intro.Loc))
    {
      reject("region boundaries require literal main-file #pragma directives");
      return;
    }
    if (tail.isNot(clang::tok::eod))
    {
      reject("region pragma takes no arguments");
      return;
    }
    source_.boundaries.push_back({intro.Loc, begin_});
    const std::array<clang::tok::TokenKind, 7> kinds{
      clang::tok::identifier,
      clang::tok::l_paren,
      clang::tok::numeric_constant,
      clang::tok::comma,
      clang::tok::string_literal,
      clang::tok::r_paren,
      clang::tok::semi};
    const std::array<std::string, 7> text{"__builtin_annotation",
                                          "(",
                                          "0",
                                          ",",
                                          begin_ ? "\"yarda.region.begin.v1\""
                                                 : "\"yarda.region.end.v1\"",
                                          ")",
                                          ";"};
    auto tokens = std::make_unique<clang::Token[]>(kinds.size());
    for (std::size_t i = 0; i < kinds.size(); ++i)
    {
      tokens[i].startToken();
      tokens[i].setKind(kinds[i]);
      pp.CreateString(text[i], tokens[i], intro.Loc, intro.Loc);
    }
    tokens[0].setIdentifierInfo(pp.getIdentifierInfo(text[0]));
    pp.EnterTokenStream(std::move(tokens), kinds.size(), true, false);
  }

private:
  bool begin_;
  SourceRegions & source_;
};

}  // namespace

void registerRegionPragmas(clang::Preprocessor & pp, SourceRegions & source)
{
  pp.AddPragmaHandler(new BoundaryPragma(true, source));
  pp.AddPragmaHandler(new BoundaryPragma(false, source));
}

}  // namespace lat::region
