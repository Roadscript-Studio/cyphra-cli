#include "cli_dsl_semantic.hpp"

#include "cli_command_spec.hpp"

#include <charconv>
#include <initializer_list>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

namespace roadscript::cli::dsl {
    namespace {
        enum class BaseType {
            Invalid,
            Unknown,
            String,
            Number,
            Boolean,
            List,
        };

        struct ExprType {
            BaseType kind = BaseType::Invalid;
            BaseType elementKind = BaseType::Unknown;

            static ExprType invalid() { return {BaseType::Invalid, BaseType::Unknown}; }
            static ExprType unknown() { return {BaseType::Unknown, BaseType::Unknown}; }
            static ExprType string() { return {BaseType::String, BaseType::Unknown}; }
            static ExprType number() { return {BaseType::Number, BaseType::Unknown}; }
            static ExprType boolean() { return {BaseType::Boolean, BaseType::Unknown}; }
            static ExprType list(BaseType elementType) { return {BaseType::List, elementType}; }
        };

        struct Scope {
            std::map<std::string, ExprType> bindings;
        };

        class Validator {
        public:
            explicit Validator(const Program &inputProgram)
                : program(inputProgram) {
                scopes.emplace_back();
            }

            SemanticResult run() {
                for (const auto &statement : program.statements) {
                    validateStatement(statement);
                }
                return SemanticResult{std::move(diagnostics)};
            }

        private:
            const Program &program;
            std::vector<Scope> scopes;
            std::vector<Diagnostic> diagnostics;

            void pushScope() { scopes.emplace_back(); }
            void popScope() { scopes.pop_back(); }

            void addDiagnostic(const SourceSpan &span, std::string message) {
                diagnostics.push_back(Diagnostic{span, std::move(message)});
            }

            void bind(std::string name, ExprType type) {
                scopes.back().bindings[std::move(name)] = type;
            }

            std::optional<ExprType> lookup(std::string_view name) const {
                for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                    const auto found = it->bindings.find(std::string(name));
                    if (found != it->bindings.end()) {
                        return found->second;
                    }
                }
                return std::nullopt;
            }

            void validateStatement(const StatementPtr &statement) {
                if (const auto *letStatement = dynamic_cast<const LetStatement *>(statement.get())) {
                    ExprType type = inferExprType(letStatement->value);
                    bind(letStatement->name, type);
                    return;
                }
                if (const auto *ifStatement = dynamic_cast<const IfStatement *>(statement.get())) {
                    ExprType conditionType = inferExprType(ifStatement->condition);
                    if (conditionType.kind != BaseType::Boolean && conditionType.kind != BaseType::Invalid) {
                        addDiagnostic(ifStatement->condition->span, "if condition must be boolean");
                    }
                    pushScope();
                    for (const auto &nested : ifStatement->thenStatements) {
                        validateStatement(nested);
                    }
                    popScope();
                    pushScope();
                    for (const auto &nested : ifStatement->elseStatements) {
                        validateStatement(nested);
                    }
                    popScope();
                    return;
                }
                if (const auto *forStatement = dynamic_cast<const ForStatement *>(statement.get())) {
                    ExprType iterableType = inferExprType(forStatement->iterable);
                    if (iterableType.kind != BaseType::List) {
                        addDiagnostic(forStatement->iterable->span, "for loop iterable must be a list or glob(...) result");
                        return;
                    }
                    pushScope();
                    bind(forStatement->variable, ExprType{iterableType.elementKind, BaseType::Unknown});
                    for (const auto &nested : forStatement->body) {
                        validateStatement(nested);
                    }
                    popScope();
                    return;
                }
                if (const auto *command = dynamic_cast<const CommandStatement *>(statement.get())) {
                    validateCommand(*command);
                }
            }

            ExprType inferExprType(const ExprPtr &expr) {
                if (expr == nullptr) {
                    return ExprType::invalid();
                }
                if (const auto *literal = dynamic_cast<const LiteralExpr *>(expr.get())) {
                    switch (literal->kind) {
                        case LiteralKind::String:
                            return ExprType::string();
                        case LiteralKind::Number:
                            return ExprType::number();
                        case LiteralKind::Boolean:
                            return ExprType::boolean();
                    }
                }
                if (const auto *variable = dynamic_cast<const VariableExpr *>(expr.get())) {
                    const auto resolved = lookup(variable->name);
                    if (!resolved.has_value()) {
                        addDiagnostic(variable->span, "unknown variable: " + variable->name);
                        return ExprType::invalid();
                    }
                    return *resolved;
                }
                if (const auto *list = dynamic_cast<const ListExpr *>(expr.get())) {
                    BaseType elementType = BaseType::Unknown;
                    for (const auto &element : list->elements) {
                        const ExprType itemType = inferExprType(element);
                        if (itemType.kind == BaseType::Invalid) {
                            return ExprType::invalid();
                        }
                        if (itemType.kind == BaseType::List) {
                            addDiagnostic(element->span, "nested lists are not supported in workflow v0.1");
                            return ExprType::invalid();
                        }
                        if (elementType == BaseType::Unknown) {
                            elementType = itemType.kind;
                        } else if (itemType.kind != BaseType::Unknown && elementType != itemType.kind) {
                            addDiagnostic(element->span, "list elements must have the same type");
                            return ExprType::invalid();
                        }
                    }
                    return ExprType::list(elementType);
                }
                if (const auto *call = dynamic_cast<const FunctionCallExpr *>(expr.get())) {
                    if (call->name == "glob") {
                        if (call->arguments.size() != 1u) {
                            addDiagnostic(call->span, "glob() expects exactly one argument");
                            return ExprType::invalid();
                        }
                        if (!checkStringLike(call->arguments[0], "glob() argument must be string-like")) {
                            return ExprType::invalid();
                        }
                        return ExprType::list(BaseType::String);
                    }
                    if (call->name == "exists") {
                        if (call->arguments.size() != 1u) {
                            addDiagnostic(call->span, "exists() expects exactly one argument");
                            return ExprType::invalid();
                        }
                        if (!checkStringLike(call->arguments[0], "exists() argument must be string-like")) {
                            return ExprType::invalid();
                        }
                        return ExprType::boolean();
                    }
                    addDiagnostic(call->span, "unknown function: " + call->name);
                    return ExprType::invalid();
                }
                if (const auto *unary = dynamic_cast<const UnaryExpr *>(expr.get())) {
                    const ExprType operandType = inferExprType(unary->operand);
                    if (operandType.kind != BaseType::Boolean && operandType.kind != BaseType::Invalid) {
                        addDiagnostic(unary->operand->span, "not expects a boolean operand");
                        return ExprType::invalid();
                    }
                    return ExprType::boolean();
                }
                return ExprType::invalid();
            }

            bool checkStringLike(const ExprPtr &expr, const std::string &message) {
                const ExprType type = inferExprType(expr);
                if (type.kind == BaseType::Invalid) {
                    return false;
                }
                if (type.kind != BaseType::String) {
                    addDiagnostic(expr->span, message);
                    return false;
                }
                return true;
            }

            void validateCommand(const CommandStatement &command) {
                const auto kind = commandKindFromCanonicalName(joinCommandName(command.nameParts));
                if (!kind.has_value()) {
                    addDiagnostic(command.span, "unknown command: " + joinCommandName(command.nameParts));
                    return;
                }
                const auto &spec = commandSpec(*kind);

                std::map<CommandField, const FieldAssignment *> seenFields;
                for (const auto &field : command.fields) {
                    const auto fieldKind = parseDslFieldName(field.name);
                    if (!fieldKind.has_value()) {
                        addDiagnostic(field.span, "unknown or unsupported field for command: " + field.name);
                        continue;
                    }
                    if (seenFields.contains(*fieldKind)) {
                        addDiagnostic(field.span, "duplicate field: " + field.name);
                        continue;
                    }
                    seenFields[*fieldKind] = &field;

                    if (!commandAllowsField(*kind, *fieldKind)) {
                        addDiagnostic(field.span, "unknown or unsupported field for command: " + field.name);
                        continue;
                    }

                    validateFieldValue(field, fieldTypeForCommand(*kind, *fieldKind));
                }

                for (const auto required : spec.requiredFields) {
                    if (!seenFields.contains(required)) {
                        addDiagnostic(command.span, "missing required field: " + std::string(commandFieldDslName(required)));
                    }
                }

                if (spec.allowKeyResolution) {
                    int keySources = 0;
                    keySources += seenFields.contains(CommandField::Key) ? 1 : 0;
                    keySources += seenFields.contains(CommandField::KeyFile) ? 1 : 0;
                    keySources += seenFields.contains(CommandField::KeyEnv) ? 1 : 0;
                    if (keySources > 1) {
                        addDiagnostic(command.span, "only one of key, key_file, or key_env may be used");
                    }
                }
            }

            CommandFieldType fieldTypeForCommand(CommandKind kind, CommandField field) const {
                if (field == CommandField::Layout) {
                    return commandSpec(kind).allowsAutoLayout
                        ? CommandFieldType::LayoutDecode
                        : CommandFieldType::LayoutInfoEmbed;
                }
                return commandFieldSpec(field).type;
            }

            void validateFieldValue(const FieldAssignment &field, CommandFieldType kind) {
                switch (kind) {
                    case CommandFieldType::StringLike:
                        checkStringLike(field.value, "field '" + field.name + "' expects a string value");
                        return;
                    case CommandFieldType::Number: {
                        const ExprType type = inferExprType(field.value);
                        if (type.kind != BaseType::Number && type.kind != BaseType::Invalid) {
                            addDiagnostic(field.value->span, "field '" + field.name + "' expects a numeric value");
                        }
                        return;
                    }
                    case CommandFieldType::Boolean: {
                        const ExprType type = inferExprType(field.value);
                        if (type.kind != BaseType::Boolean && type.kind != BaseType::Invalid) {
                            addDiagnostic(field.value->span, "field '" + field.name + "' expects a boolean value");
                        }
                        return;
                    }
                    case CommandFieldType::PositiveInteger: {
                        const ExprType type = inferExprType(field.value);
                        if (type.kind != BaseType::Number && type.kind != BaseType::Invalid) {
                            addDiagnostic(field.value->span, "field '" + field.name + "' expects a positive integer");
                            return;
                        }
                        if (const auto *literal = dynamic_cast<const LiteralExpr *>(field.value.get())) {
                            if (literal->kind == LiteralKind::Number) {
                                if (!isPositiveIntegerLiteral(literal->text)) {
                                    addDiagnostic(field.value->span, "field '" + field.name + "' expects a positive integer");
                                }
                            }
                        }
                        return;
                    }
                    case CommandFieldType::Protocol:
                        if (!matchesEnumValue(field.value, {"classic", "mosaic"}, true)) {
                            addDiagnostic(field.value->span, "invalid protocol value");
                        }
                        return;
                    case CommandFieldType::LayoutInfoEmbed:
                        if (!matchesEnumValue(field.value, {"center_ring", "keyed_shuffle"}, false,
                                              {"center-ring", "keyed-shuffle"})) {
                            addDiagnostic(field.value->span, "invalid layout value for this command");
                        }
                        return;
                    case CommandFieldType::LayoutDecode:
                        if (!matchesEnumValue(field.value, {"center_ring", "keyed_shuffle", "auto"}, false,
                                              {"center-ring", "keyed-shuffle", "auto"})) {
                            addDiagnostic(field.value->span, "invalid layout value for this command");
                        }
                        return;
                }
            }

            static bool isPositiveIntegerLiteral(const std::string &text) {
                if (text.empty()) {
                    return false;
                }
                long long value = 0;
                const char *begin = text.data();
                const char *end = text.data() + text.size();
                const auto parsed = std::from_chars(begin, end, value);
                return parsed.ec == std::errc{} && parsed.ptr == end && value > 0;
            }

            bool matchesEnumValue(
                const ExprPtr &expr,
                std::initializer_list<std::string_view> identifiers,
                bool allowAliases,
                std::initializer_list<std::string_view> stringValues = {}
            ) {
                if (const auto *variable = dynamic_cast<const VariableExpr *>(expr.get())) {
                    const auto resolved = lookup(variable->name);
                    if (resolved.has_value()) {
                        return resolved->kind == BaseType::String;
                    }
                    for (const auto identifier : identifiers) {
                        if (variable->name == identifier) {
                            return true;
                        }
                    }
                    for (const auto stringValue : stringValues) {
                        if (variable->name == stringValue) {
                            return true;
                        }
                    }
                    if (allowAliases) {
                        if (variable->name == "v1" || variable->name == "mosaic_v2" || variable->name == "mosaic-v2") {
                            return true;
                        }
                    }
                    addDiagnostic(variable->span, "unknown variable: " + variable->name);
                    return false;
                }
                if (const auto *literal = dynamic_cast<const LiteralExpr *>(expr.get())) {
                    if (literal->kind != LiteralKind::String) {
                        return false;
                    }
                    for (const auto identifier : identifiers) {
                        if (literal->text == identifier) {
                            return true;
                        }
                    }
                    for (const auto stringValue : stringValues) {
                        if (literal->text == stringValue) {
                            return true;
                        }
                    }
                    if (allowAliases) {
                        return literal->text == "v1" || literal->text == "mosaic-v2";
                    }
                    return false;
                }
                inferExprType(expr);
                return false;
            }
        };
    } // namespace

    SemanticResult validateWorkflowProgram(const Program &program) {
        return Validator(program).run();
    }
} // namespace roadscript::cli::dsl
