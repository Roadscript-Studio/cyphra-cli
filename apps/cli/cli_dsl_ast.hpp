#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace roadscript::cli::dsl {
    struct SourceLocation {
        int line = 1;
        int column = 1;
        int offset = 0;
    };

    struct SourceSpan {
        std::string file;
        SourceLocation begin;
        SourceLocation end;
    };

    struct Diagnostic {
        SourceSpan span;
        std::string message;
    };

    struct Expr;
    struct Statement;

    using ExprPtr = std::shared_ptr<Expr>;
    using StatementPtr = std::shared_ptr<Statement>;

    enum class LiteralKind {
        String,
        Number,
        Boolean,
    };

    enum class UnaryOp {
        Not,
    };

    struct Expr {
        explicit Expr(SourceSpan sourceSpan)
            : span(std::move(sourceSpan)) {}
        virtual ~Expr() = default;
        SourceSpan span;
    };

    struct LiteralExpr final : Expr {
        LiteralExpr(SourceSpan sourceSpan, std::string literalText, LiteralKind literalKind)
            : Expr(std::move(sourceSpan)),
              text(std::move(literalText)),
              kind(literalKind) {}

        std::string text;
        LiteralKind kind = LiteralKind::String;
    };

    struct VariableExpr final : Expr {
        VariableExpr(SourceSpan sourceSpan, std::string variableName)
            : Expr(std::move(sourceSpan)),
              name(std::move(variableName)) {}

        std::string name;
    };

    struct ListExpr final : Expr {
        explicit ListExpr(SourceSpan sourceSpan)
            : Expr(std::move(sourceSpan)) {}

        std::vector<ExprPtr> elements;
    };

    struct FunctionCallExpr final : Expr {
        FunctionCallExpr(SourceSpan sourceSpan, std::string functionName)
            : Expr(std::move(sourceSpan)),
              name(std::move(functionName)) {}

        std::string name;
        std::vector<ExprPtr> arguments;
    };

    struct UnaryExpr final : Expr {
        UnaryExpr(SourceSpan sourceSpan, UnaryOp unaryOp, ExprPtr unaryOperand)
            : Expr(std::move(sourceSpan)),
              op(unaryOp),
              operand(std::move(unaryOperand)) {}

        UnaryOp op = UnaryOp::Not;
        ExprPtr operand;
    };

    struct FieldAssignment {
        std::string name;
        ExprPtr value;
        SourceSpan span;
    };

    struct Statement {
        explicit Statement(SourceSpan sourceSpan)
            : span(std::move(sourceSpan)) {}
        virtual ~Statement() = default;
        SourceSpan span;
    };

    struct LetStatement final : Statement {
        LetStatement(SourceSpan sourceSpan, std::string variableName, ExprPtr variableValue)
            : Statement(std::move(sourceSpan)),
              name(std::move(variableName)),
              value(std::move(variableValue)) {}

        std::string name;
        ExprPtr value;
    };

    struct IfStatement final : Statement {
        IfStatement(SourceSpan sourceSpan, ExprPtr ifCondition)
            : Statement(std::move(sourceSpan)),
              condition(std::move(ifCondition)) {}

        ExprPtr condition;
        std::vector<StatementPtr> thenStatements;
        std::vector<StatementPtr> elseStatements;
    };

    struct ForStatement final : Statement {
        ForStatement(SourceSpan sourceSpan, std::string loopName, ExprPtr loopIterable)
            : Statement(std::move(sourceSpan)),
              variable(std::move(loopName)),
              iterable(std::move(loopIterable)) {}

        std::string variable;
        ExprPtr iterable;
        std::vector<StatementPtr> body;
    };

    struct CommandStatement final : Statement {
        explicit CommandStatement(SourceSpan sourceSpan)
            : Statement(std::move(sourceSpan)) {}

        std::vector<std::string> nameParts;
        std::vector<FieldAssignment> fields;
    };

    struct Program {
        std::string file;
        std::vector<StatementPtr> statements;
    };

    inline std::string joinCommandName(const std::vector<std::string> &nameParts) {
        std::string joined;
        for (std::size_t i = 0; i < nameParts.size(); ++i) {
            if (i != 0) {
                joined += ' ';
            }
            joined += nameParts[i];
        }
        return joined;
    }
} // namespace roadscript::cli::dsl
