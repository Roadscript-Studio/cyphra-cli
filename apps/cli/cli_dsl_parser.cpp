#include "cli_dsl_parser.hpp"

#include "cli_dsl_lexer.hpp"

#include <utility>

namespace roadscript::cli::dsl {
    namespace {
        class Parser {
        public:
            Parser(std::vector<Token> inputTokens, std::string_view fileName)
                : tokens(std::move(inputTokens)),
                  file(fileName) {}

            ParseResult run() {
                ParseResult result;
                result.program.file = file;
                while (!isAtEnd()) {
                    auto statement = parseStatement();
                    if (statement) {
                        result.program.statements.push_back(std::move(statement));
                    } else if (!diagnostics.empty()) {
                        synchronize();
                    }
                }
                result.diagnostics = std::move(diagnostics);
                return result;
            }

        private:
            std::vector<Token> tokens;
            std::string file;
            std::size_t current = 0;
            std::vector<Diagnostic> diagnostics;

            [[nodiscard]] bool isAtEnd() const {
                return peek().kind == TokenKind::EndOfFile;
            }

            [[nodiscard]] const Token &peek() const {
                return tokens[current];
            }

            [[nodiscard]] const Token &previous() const {
                return tokens[current - 1u];
            }

            [[nodiscard]] bool check(TokenKind kind) const {
                return !isAtEnd() && peek().kind == kind;
            }

            bool match(TokenKind kind) {
                if (!check(kind)) {
                    return false;
                }
                ++current;
                return true;
            }

            const Token *matchIdentifier() {
                if (!check(TokenKind::Identifier)) {
                    return nullptr;
                }
                const Token *token = &tokens[current];
                ++current;
                return token;
            }

            const Token *consumeFieldName(std::string_view message) {
                if (check(TokenKind::Identifier) || check(TokenKind::In)) {
                    ++current;
                    return &previous();
                }
                errorAt(peek(), message);
                return nullptr;
            }

            const Token *consume(TokenKind kind, std::string_view message) {
                if (check(kind)) {
                    ++current;
                    return &previous();
                }
                errorAt(peek(), message);
                return nullptr;
            }

            void errorAt(const Token &token, std::string_view message) {
                diagnostics.push_back(Diagnostic{
                    token.span,
                    std::string(message)
                });
            }

            void synchronize() {
                if (current == 0u && !isAtEnd()) {
                    ++current;
                }
                while (!isAtEnd()) {
                    if (current > 0u &&
                        (previous().kind == TokenKind::Semicolon || previous().kind == TokenKind::RBrace)) {
                        return;
                    }
                    switch (peek().kind) {
                        case TokenKind::Let:
                        case TokenKind::If:
                        case TokenKind::For:
                        case TokenKind::Identifier:
                            return;
                        default:
                            ++current;
                            break;
                    }
                }
            }

            static SourceSpan mergeSpans(const SourceSpan &start, const SourceSpan &end) {
                return SourceSpan{
                    start.file.empty() ? end.file : start.file,
                    start.begin,
                    end.end
                };
            }

            StatementPtr parseStatement() {
                if (match(TokenKind::Semicolon)) {
                    return nullptr;
                }
                if (match(TokenKind::Let)) {
                    return parseLetStatement(previous());
                }
                if (match(TokenKind::If)) {
                    return parseIfStatement(previous());
                }
                if (match(TokenKind::For)) {
                    return parseForStatement(previous());
                }
                if (check(TokenKind::Identifier)) {
                    return parseCommandStatement();
                }

                errorAt(peek(), "expected statement");
                return nullptr;
            }

            StatementPtr parseLetStatement(const Token &letToken) {
                const Token *name = consume(TokenKind::Identifier, "expected variable name after let");
                if (name == nullptr) {
                    return nullptr;
                }
                if (consume(TokenKind::Equal, "expected '=' after variable name") == nullptr) {
                    return nullptr;
                }
                ExprPtr value = parseExpression();
                if (!value) {
                    return nullptr;
                }
                match(TokenKind::Semicolon);
                return std::make_shared<LetStatement>(
                    mergeSpans(letToken.span, value->span),
                    name->text,
                    std::move(value)
                );
            }

            StatementPtr parseIfStatement(const Token &ifToken) {
                ExprPtr condition = parseExpression();
                if (!condition) {
                    return nullptr;
                }
                auto ifStatement = std::make_shared<IfStatement>(
                    mergeSpans(ifToken.span, condition->span),
                    std::move(condition)
                );
                if (!parseStatementBlock(ifStatement->thenStatements, "expected '{' after if condition")) {
                    return nullptr;
                }
                if (match(TokenKind::Else)) {
                    if (!parseStatementBlock(ifStatement->elseStatements, "expected '{' after else")) {
                        return nullptr;
                    }
                    ifStatement->span = mergeSpans(ifStatement->span, previous().span);
                } else if (!ifStatement->thenStatements.empty()) {
                    ifStatement->span = mergeSpans(ifStatement->span, ifStatement->thenStatements.back()->span);
                }
                return ifStatement;
            }

            StatementPtr parseForStatement(const Token &forToken) {
                const Token *name = consume(TokenKind::Identifier, "expected loop variable after for");
                if (name == nullptr) {
                    return nullptr;
                }
                if (consume(TokenKind::In, "expected 'in' after loop variable") == nullptr) {
                    return nullptr;
                }
                ExprPtr iterable = parseExpression();
                if (!iterable) {
                    return nullptr;
                }
                auto forStatement = std::make_shared<ForStatement>(
                    mergeSpans(forToken.span, iterable->span),
                    name->text,
                    std::move(iterable)
                );
                if (!parseStatementBlock(forStatement->body, "expected '{' after loop iterable")) {
                    return nullptr;
                }
                if (!forStatement->body.empty()) {
                    forStatement->span = mergeSpans(forStatement->span, forStatement->body.back()->span);
                }
                return forStatement;
            }

            StatementPtr parseCommandStatement() {
                const Token &start = peek();
                auto command = std::make_shared<CommandStatement>(start.span);
                const Token *firstPart = matchIdentifier();
                if (firstPart == nullptr) {
                    errorAt(start, "expected command name");
                    return nullptr;
                }
                command->nameParts.push_back(firstPart->text);
                if (firstPart->text == "config") {
                    const Token *secondPart = consume(TokenKind::Identifier, "expected config subcommand");
                    if (secondPart == nullptr) {
                        return nullptr;
                    }
                    command->nameParts.push_back(secondPart->text);
                }
                if (consume(TokenKind::LBrace, "expected '{' after command name") == nullptr) {
                    return nullptr;
                }

                while (!check(TokenKind::RBrace) && !isAtEnd()) {
                    const Token *fieldName = consumeFieldName("expected field name inside command block");
                    if (fieldName == nullptr) {
                        return nullptr;
                    }
                    if (consume(TokenKind::Colon, "expected ':' after field name") == nullptr) {
                        return nullptr;
                    }
                    ExprPtr value = parseExpression();
                    if (!value) {
                        return nullptr;
                    }
                    command->fields.push_back(FieldAssignment{
                        fieldName->text,
                        std::move(value),
                        {}
                    });
                    command->fields.back().span = mergeSpans(fieldName->span, command->fields.back().value->span);
                    match(TokenKind::Semicolon);
                }

                const Token *closing = consume(TokenKind::RBrace, "expected '}' after command block");
                if (closing == nullptr) {
                    return nullptr;
                }
                command->span = mergeSpans(start.span, closing->span);
                return command;
            }

            bool parseStatementBlock(std::vector<StatementPtr> &out, std::string_view openMessage) {
                if (consume(TokenKind::LBrace, openMessage) == nullptr) {
                    return false;
                }
                while (!check(TokenKind::RBrace) && !isAtEnd()) {
                    auto statement = parseStatement();
                    if (statement) {
                        out.push_back(std::move(statement));
                    } else if (!diagnostics.empty()) {
                        synchronize();
                    }
                }
                return consume(TokenKind::RBrace, "expected '}' to close block") != nullptr;
            }

            ExprPtr parseExpression() {
                return parseUnary();
            }

            ExprPtr parseUnary() {
                if (match(TokenKind::Not)) {
                    const Token &opToken = previous();
                    ExprPtr operand = parseUnary();
                    if (!operand) {
                        return nullptr;
                    }
                    return std::make_shared<UnaryExpr>(
                        mergeSpans(opToken.span, operand->span),
                        UnaryOp::Not,
                        std::move(operand)
                    );
                }
                return parsePrimary();
            }

            ExprPtr parsePrimary() {
                if (match(TokenKind::String)) {
                    return std::make_shared<LiteralExpr>(
                        previous().span,
                        previous().text,
                        LiteralKind::String
                    );
                }
                if (match(TokenKind::Number)) {
                    return std::make_shared<LiteralExpr>(
                        previous().span,
                        previous().text,
                        LiteralKind::Number
                    );
                }
                if (match(TokenKind::True)) {
                    return std::make_shared<LiteralExpr>(
                        previous().span,
                        "true",
                        LiteralKind::Boolean
                    );
                }
                if (match(TokenKind::False)) {
                    return std::make_shared<LiteralExpr>(
                        previous().span,
                        "false",
                        LiteralKind::Boolean
                    );
                }
                if (match(TokenKind::Identifier)) {
                    const Token &identifier = previous();
                    if (match(TokenKind::LParen)) {
                        auto call = std::make_shared<FunctionCallExpr>(identifier.span, identifier.text);
                        if (!check(TokenKind::RParen)) {
                            while (true) {
                                ExprPtr argument = parseExpression();
                                if (!argument) {
                                    return nullptr;
                                }
                                call->arguments.push_back(std::move(argument));
                                if (!match(TokenKind::Comma)) {
                                    break;
                                }
                            }
                        }
                        const Token *closing = consume(TokenKind::RParen, "expected ')' after function arguments");
                        if (closing == nullptr) {
                            return nullptr;
                        }
                        call->span = mergeSpans(identifier.span, closing->span);
                        return call;
                    }
                    return std::make_shared<VariableExpr>(identifier.span, identifier.text);
                }
                if (match(TokenKind::LBracket)) {
                    const Token &open = previous();
                    auto list = std::make_shared<ListExpr>(open.span);
                    if (!check(TokenKind::RBracket)) {
                        while (true) {
                            ExprPtr element = parseExpression();
                            if (!element) {
                                return nullptr;
                            }
                            list->elements.push_back(std::move(element));
                            if (!match(TokenKind::Comma)) {
                                break;
                            }
                        }
                    }
                    const Token *closing = consume(TokenKind::RBracket, "expected ']' after list");
                    if (closing == nullptr) {
                        return nullptr;
                    }
                    list->span = mergeSpans(open.span, closing->span);
                    return list;
                }

                errorAt(peek(), "expected expression");
                return nullptr;
            }
        };
    } // namespace

    ParseResult parseWorkflowScript(std::string_view file, std::string_view source) {
        const LexResult lexed = lexWorkflowScript(file, source);
        ParseResult result;
        result.program.file = std::string(file);
        if (!lexed.ok()) {
            result.diagnostics = lexed.diagnostics;
            return result;
        }
        Parser parser(lexed.tokens, file);
        return parser.run();
    }
} // namespace roadscript::cli::dsl
