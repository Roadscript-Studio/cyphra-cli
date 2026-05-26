#include "cli_dsl_lexer.hpp"

#include <cctype>
#include <utility>

namespace roadscript::cli::dsl {
    namespace {
        class Lexer {
        public:
            Lexer(std::string_view fileName, std::string_view input)
                : file(fileName),
                  source(input) {}

            LexResult run() {
                while (!isAtEnd()) {
                    skipTrivia();
                    if (isAtEnd()) {
                        break;
                    }

                    const SourceLocation start = currentLocation();
                    const char ch = peek();
                    if (isIdentifierStart(ch)) {
                        lexIdentifierOrKeyword(start);
                        continue;
                    }
                    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                        lexNumber(start);
                        continue;
                    }

                    switch (ch) {
                        case '"':
                            lexString(start);
                            break;
                        case '{':
                            pushSimpleToken(TokenKind::LBrace, "{", start);
                            advance();
                            break;
                        case '}':
                            pushSimpleToken(TokenKind::RBrace, "}", start);
                            advance();
                            break;
                        case '[':
                            pushSimpleToken(TokenKind::LBracket, "[", start);
                            advance();
                            break;
                        case ']':
                            pushSimpleToken(TokenKind::RBracket, "]", start);
                            advance();
                            break;
                        case '(':
                            pushSimpleToken(TokenKind::LParen, "(", start);
                            advance();
                            break;
                        case ')':
                            pushSimpleToken(TokenKind::RParen, ")", start);
                            advance();
                            break;
                        case ':':
                            pushSimpleToken(TokenKind::Colon, ":", start);
                            advance();
                            break;
                        case ',':
                            pushSimpleToken(TokenKind::Comma, ",", start);
                            advance();
                            break;
                        case '=':
                            pushSimpleToken(TokenKind::Equal, "=", start);
                            advance();
                            break;
                        case ';':
                            pushSimpleToken(TokenKind::Semicolon, ";", start);
                            advance();
                            break;
                        default: {
                            Diagnostic diagnostic;
                            diagnostic.span = spanFrom(start);
                            diagnostic.message = "unexpected character: " + std::string(1, ch);
                            result.diagnostics.push_back(std::move(diagnostic));
                            advance();
                            break;
                        }
                    }
                }

                Token eof;
                eof.kind = TokenKind::EndOfFile;
                eof.span = spanFrom(currentLocation());
                result.tokens.push_back(std::move(eof));
                return result;
            }

        private:
            std::string file;
            std::string_view source;
            std::size_t index = 0;
            int line = 1;
            int column = 1;
            LexResult result;

            [[nodiscard]] bool isAtEnd() const {
                return index >= source.size();
            }

            [[nodiscard]] char peek() const {
                return isAtEnd() ? '\0' : source[index];
            }

            [[nodiscard]] char peekNext() const {
                return (index + 1u) < source.size() ? source[index + 1u] : '\0';
            }

            [[nodiscard]] SourceLocation currentLocation() const {
                return SourceLocation{
                    line,
                    column,
                    static_cast<int>(index)
                };
            }

            [[nodiscard]] SourceSpan spanFrom(const SourceLocation &start) const {
                return SourceSpan{
                    file,
                    start,
                    currentLocation()
                };
            }

            char advance() {
                const char ch = peek();
                if (isAtEnd()) {
                    return '\0';
                }
                ++index;
                if (ch == '\n') {
                    ++line;
                    column = 1;
                } else {
                    ++column;
                }
                return ch;
            }

            static bool isIdentifierStart(char ch) {
                return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
            }

            static bool isIdentifierContinue(char ch) {
                return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
            }

            void skipTrivia() {
                while (!isAtEnd()) {
                    const char ch = peek();
                    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
                        advance();
                        continue;
                    }
                    if (ch == '#') {
                        skipLineComment();
                        continue;
                    }
                    if (ch == '/' && peekNext() == '/') {
                        advance();
                        advance();
                        skipLineComment();
                        continue;
                    }
                    break;
                }
            }

            void skipLineComment() {
                while (!isAtEnd() && peek() != '\n') {
                    advance();
                }
            }

            void pushSimpleToken(TokenKind kind, std::string_view text, const SourceLocation &start) {
                Token token;
                token.kind = kind;
                token.text = std::string(text);
                token.span = SourceSpan{file, start, SourceLocation{start.line, start.column + static_cast<int>(text.size()), start.offset + static_cast<int>(text.size())}};
                result.tokens.push_back(std::move(token));
            }

            void lexIdentifierOrKeyword(const SourceLocation &start) {
                std::string text;
                while (!isAtEnd() && isIdentifierContinue(peek())) {
                    text.push_back(advance());
                }

                Token token;
                token.text = text;
                token.span = spanFrom(start);
                if (text == "let") token.kind = TokenKind::Let;
                else if (text == "if") token.kind = TokenKind::If;
                else if (text == "else") token.kind = TokenKind::Else;
                else if (text == "for") token.kind = TokenKind::For;
                else if (text == "in") token.kind = TokenKind::In;
                else if (text == "not") token.kind = TokenKind::Not;
                else if (text == "true") token.kind = TokenKind::True;
                else if (text == "false") token.kind = TokenKind::False;
                else token.kind = TokenKind::Identifier;
                result.tokens.push_back(std::move(token));
            }

            void lexNumber(const SourceLocation &start) {
                std::string text;
                bool seenDot = false;
                while (!isAtEnd()) {
                    const char ch = peek();
                    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                        text.push_back(advance());
                        continue;
                    }
                    if (ch == '.' && !seenDot) {
                        seenDot = true;
                        text.push_back(advance());
                        continue;
                    }
                    break;
                }

                Token token;
                token.kind = TokenKind::Number;
                token.text = std::move(text);
                token.span = spanFrom(start);
                result.tokens.push_back(std::move(token));
            }

            void lexString(const SourceLocation &start) {
                advance(); // opening quote
                std::string value;
                while (!isAtEnd()) {
                    const char ch = advance();
                    if (ch == '"') {
                        Token token;
                        token.kind = TokenKind::String;
                        token.text = std::move(value);
                        token.span = spanFrom(start);
                        result.tokens.push_back(std::move(token));
                        return;
                    }
                    if (ch == '\\') {
                        if (isAtEnd()) {
                            break;
                        }
                        const char escaped = advance();
                        switch (escaped) {
                            case '"': value.push_back('"'); break;
                            case '\\': value.push_back('\\'); break;
                            case 'n': value.push_back('\n'); break;
                            case 'r': value.push_back('\r'); break;
                            case 't': value.push_back('\t'); break;
                            default: value.push_back(escaped); break;
                        }
                        continue;
                    }
                    value.push_back(ch);
                }

                Diagnostic diagnostic;
                diagnostic.span = spanFrom(start);
                diagnostic.message = "unterminated string literal";
                result.diagnostics.push_back(std::move(diagnostic));
            }
        };
    } // namespace

    LexResult lexWorkflowScript(std::string_view file, std::string_view source) {
        return Lexer(file, source).run();
    }
} // namespace roadscript::cli::dsl
