#include <array>
#include <memory>
#include <string>

#include "clang/Basic/Diagnostic.h"
#include "clang/Lex/Pragma.h"
#include "clang/Lex/Preprocessor.h"

namespace {

// Feasibility only: AST scope/CFG validation belongs to R2's frontend action.
class BoundaryPragma : public clang::PragmaHandler {
  public:
    explicit BoundaryPragma(const char* name, const char* marker)
        : PragmaHandler(name), marker_(marker) {}

    void HandlePragma(clang::Preprocessor& pp, clang::PragmaIntroducer intro,
                      clang::Token&) override {
        clang::Token tail;
        pp.Lex(tail);
        if (tail.isNot(clang::tok::eod)) {
            const auto id = pp.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error,
                                                                "region pragma takes no arguments");
            pp.Diag(intro.Loc, id);
            return;
        }
        const std::array<clang::tok::TokenKind, 7> kinds{
            clang::tok::identifier, clang::tok::l_paren,        clang::tok::numeric_constant,
            clang::tok::comma,      clang::tok::string_literal, clang::tok::r_paren,
            clang::tok::semi};
        const std::array<std::string, 7> text{"__builtin_annotation", "(", "0", ",",
                                              '"' + marker_ + '"',    ")", ";"};
        auto tokens = std::make_unique<clang::Token[]>(kinds.size());
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            tokens[i].startToken();
            tokens[i].setKind(kinds[i]);
            pp.CreateString(text[i], tokens[i], intro.Loc, intro.Loc);
        }
        tokens[0].setIdentifierInfo(pp.getIdentifierInfo(text[0]));
        pp.EnterTokenStream(std::move(tokens), kinds.size(), true, false);
    }

  private:
    std::string marker_;
};

class BeginPragma : public BoundaryPragma {
  public:
    BeginPragma() : BoundaryPragma("APE_ANALYZE_BEGIN", "yarda.region.begin.v1") {}
};

class EndPragma : public BoundaryPragma {
  public:
    EndPragma() : BoundaryPragma("APE_ANALYZE_END", "yarda.region.end.v1") {}
};

clang::PragmaHandlerRegistry::Add<BeginPragma> begin("APE_ANALYZE_BEGIN", "R1 begin probe");
clang::PragmaHandlerRegistry::Add<EndPragma> end("APE_ANALYZE_END", "R1 end probe");

} // namespace
