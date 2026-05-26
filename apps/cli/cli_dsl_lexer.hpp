#pragma once

#include "cli_dsl_ast.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace roadscript::cli::dsl {
    enum class TokenKind {
        EndOfFile,
        Identifier,
        String,
        Number,
        True,
        False,
        Let,
        If,
        Else,
        For,
        In,
        Not,
        LBrace,
        RBrace,
        LBracket,
        RBracket,
        LParen,
        RParen,
        Colon,
        Comma,
        Equal,
        Semicolon,
    };

    struct Token {
        TokenKind kind = TokenKind::EndOfFile;
        std::string text;
        SourceSpan span;
    };

    struct LexResult {
        std::vector<Token> tokens;
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool ok() const { return diagnostics.empty(); }
    };

    LexResult lexWorkflowScript(std::string_view file, std::string_view source);
} // namespace roadscript::cli::dsl
