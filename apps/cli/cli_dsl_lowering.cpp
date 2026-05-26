#include "cli_dsl_lowering.hpp"

#include "cli_command_spec.hpp"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace roadscript::cli::dsl {
    namespace {
        enum class LoweredValueKind {
            Invalid,
            String,
            Number,
            Boolean,
            List,
        };

        struct LoweredValue {
            LoweredValueKind kind = LoweredValueKind::Invalid;
            std::string stringValue;
            double numberValue = 0.0;
            bool boolValue = false;
            std::vector<LoweredValue> listValues;

            static LoweredValue string(std::string value) {
                LoweredValue out;
                out.kind = LoweredValueKind::String;
                out.stringValue = std::move(value);
                return out;
            }

            static LoweredValue number(double value) {
                LoweredValue out;
                out.kind = LoweredValueKind::Number;
                out.numberValue = value;
                return out;
            }

            static LoweredValue boolean(bool value) {
                LoweredValue out;
                out.kind = LoweredValueKind::Boolean;
                out.boolValue = value;
                return out;
            }

            static LoweredValue list(std::vector<LoweredValue> values) {
                LoweredValue out;
                out.kind = LoweredValueKind::List;
                out.listValues = std::move(values);
                return out;
            }
        };

        struct VariableBinding {
            enum class Kind {
                StaticValue,
                DynamicLoopVariable,
            };

            Kind kind = Kind::StaticValue;
            LoweredValue value;
            std::string variableName;
        };

        class Lowerer {
        public:
            explicit Lowerer(const Program &inputProgram)
                : program(inputProgram) {
                scopes.emplace_back();
            }

            DslLoweringResult run() {
                DslLoweringResult result;
                result.plan.file = program.file;
                lowerStatements(program.statements, result.plan.steps);
                result.diagnostics = std::move(diagnostics);
                return result;
            }

        private:
            const Program &program;
            std::vector<std::unordered_map<std::string, VariableBinding>> scopes;
            std::vector<Diagnostic> diagnostics;

            void pushScope() {
                scopes.emplace_back();
            }

            void popScope() {
                scopes.pop_back();
            }

            void addDiagnostic(const SourceSpan &span, std::string message) {
                diagnostics.push_back(Diagnostic{span, std::move(message)});
            }

            void bind(std::string name, VariableBinding binding) {
                scopes.back()[std::move(name)] = std::move(binding);
            }

            std::optional<VariableBinding> lookup(std::string_view name) const {
                for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                    const auto found = it->find(std::string(name));
                    if (found != it->end()) {
                        return found->second;
                    }
                }
                return std::nullopt;
            }

            void lowerStatements(
                const std::vector<StatementPtr> &statements,
                std::vector<DslWorkflowStep> &outSteps
            ) {
                for (const auto &statement : statements) {
                    lowerStatement(statement, outSteps);
                }
            }

            void lowerStatement(const StatementPtr &statement, std::vector<DslWorkflowStep> &outSteps) {
                if (const auto *letStatement = dynamic_cast<const LetStatement *>(statement.get())) {
                    LoweredValue value;
                    if (!evaluateStaticExpr(letStatement->value, value)) {
                        addDiagnostic(
                            letStatement->value->span,
                            "let bindings must resolve to static values in workflow v0.1"
                        );
                        return;
                    }
                    bind(letStatement->name, VariableBinding{VariableBinding::Kind::StaticValue, std::move(value), {}});
                    return;
                }

                if (const auto *commandStatement = dynamic_cast<const CommandStatement *>(statement.get())) {
                    auto step = lowerCommandStatement(*commandStatement);
                    if (step.has_value()) {
                        outSteps.push_back(std::move(*step));
                    }
                    return;
                }

                if (const auto *ifStatement = dynamic_cast<const IfStatement *>(statement.get())) {
                    auto condition = lowerCondition(ifStatement->condition);
                    if (!condition.has_value()) {
                        return;
                    }

                    DslWorkflowStep step;
                    step.kind = WorkflowStepKind::If;
                    step.span = ifStatement->span;
                    step.label = "if";
                    step.condition = std::move(condition);

                    pushScope();
                    lowerStatements(ifStatement->thenStatements, step.body);
                    popScope();

                    pushScope();
                    lowerStatements(ifStatement->elseStatements, step.elseBody);
                    popScope();

                    outSteps.push_back(std::move(step));
                    return;
                }

                if (const auto *forStatement = dynamic_cast<const ForStatement *>(statement.get())) {
                    lowerForStatement(*forStatement, outSteps);
                }
            }

            void lowerForStatement(const ForStatement &forStatement, std::vector<DslWorkflowStep> &outSteps) {
                if (const auto *call = dynamic_cast<const FunctionCallExpr *>(forStatement.iterable.get())) {
                    if (call->name == "glob") {
                        if (call->arguments.size() != 1u) {
                            addDiagnostic(call->span, "glob() expects exactly one argument");
                            return;
                        }

                        std::string pattern;
                        if (!resolveStringExpression(call->arguments[0], pattern, nullptr, false)) {
                            addDiagnostic(call->arguments[0]->span, "glob() pattern must resolve to a string");
                            return;
                        }

                        DslWorkflowStep step;
                        step.kind = WorkflowStepKind::ForEachGlob;
                        step.span = forStatement.span;
                        step.label = "for " + forStatement.variable + " in glob";
                        step.forEachGlob = DeferredForEachGlob{
                            forStatement.variable,
                            pattern,
                            forStatement.iterable->span
                        };

                        pushScope();
                        bind(
                            forStatement.variable,
                            VariableBinding{
                                VariableBinding::Kind::DynamicLoopVariable,
                                {},
                                forStatement.variable
                            }
                        );
                        lowerStatements(forStatement.body, step.body);
                        popScope();

                        outSteps.push_back(std::move(step));
                        return;
                    }
                }

                LoweredValue iterable;
                if (!evaluateStaticExpr(forStatement.iterable, iterable)) {
                    addDiagnostic(forStatement.iterable->span, "for loop iterable must be a static list or glob(...)");
                    return;
                }
                if (iterable.kind != LoweredValueKind::List) {
                    addDiagnostic(forStatement.iterable->span, "for loop iterable must be a list");
                    return;
                }

                for (const auto &item : iterable.listValues) {
                    pushScope();
                    bind(
                        forStatement.variable,
                        VariableBinding{
                            VariableBinding::Kind::StaticValue,
                            item,
                            {}
                        }
                    );
                    lowerStatements(forStatement.body, outSteps);
                    popScope();
                }
            }

            std::optional<DslWorkflowStep> lowerCommandStatement(const CommandStatement &statement) {
                const auto kind = lowerCommandKind(statement);
                if (!kind.has_value()) {
                    return std::nullopt;
                }
                const auto &spec = commandSpec(*kind);

                roadscript::cli::CommandIR command;
                command.kind = *kind;
                command.sourceSpan = roadscript::cli::CommandSourceSpan{
                    statement.span.file,
                    statement.span.begin.line,
                    statement.span.begin.column
                };
                command.options.protocol = spec.defaultProtocol;
                command.options.layout = spec.defaultLayout;

                DslWorkflowStep step;
                step.kind = WorkflowStepKind::Command;
                step.span = statement.span;
                step.label = std::string(spec.displayLabel);
                step.command = command;

                for (const auto &field : statement.fields) {
                    if (!lowerField(*kind, field, *step.command, step.fieldBindings)) {
                        return std::nullopt;
                    }
                }

                return step;
            }

            std::optional<roadscript::cli::CommandKind> lowerCommandKind(const CommandStatement &statement) {
                const auto kind = commandKindFromCanonicalName(joinCommandName(statement.nameParts));
                if (!kind.has_value()) {
                    addDiagnostic(statement.span, "unknown command during lowering: " + joinCommandName(statement.nameParts));
                }
                return kind;
            }

            bool lowerField(
                roadscript::cli::CommandKind kind,
                const FieldAssignment &field,
                roadscript::cli::CommandIR &command,
                std::vector<CommandFieldTemplateBinding> &bindings
            ) {
                const auto fieldKind = parseDslFieldName(field.name);
                if (!fieldKind.has_value()) {
                    addDiagnostic(field.span, "unknown field during lowering: " + field.name);
                    return false;
                }
                if (!commandAllowsField(kind, *fieldKind)) {
                    addDiagnostic(field.span, "unsupported field during lowering: " + field.name);
                    return false;
                }

                switch (*fieldKind) {
                    case CommandField::In:
                        return assignStringField(field, command.options.inputPath, CommandFieldTarget::InputPath, bindings);
                    case CommandField::Out:
                        return assignStringField(field, command.options.outputPath, CommandFieldTarget::OutputPath, bindings);
                    case CommandField::MsgBlock:
                        command.options.hasMessage = true;
                        return assignStringField(field, command.options.message, CommandFieldTarget::Message, bindings);
                    case CommandField::Key: {
                        std::string staticValue;
                        std::string dynamicVariable;
                        if (!resolveStringExpression(field.value, staticValue, &dynamicVariable, true)) {
                            addDiagnostic(field.value->span, "field 'key' expects a string value");
                            return false;
                        }
                        if (!dynamicVariable.empty()) {
                            bindings.push_back(CommandFieldTemplateBinding{
                                CommandFieldTarget::Key,
                                std::move(dynamicVariable),
                                field.span
                            });
                            return true;
                        }
                        command.options.key = std::move(staticValue);
                        return true;
                    }
                    case CommandField::KeyFile:
                        return assignStringField(field, command.options.keyFilePath, CommandFieldTarget::KeyFilePath, bindings);
                    case CommandField::KeyEnv:
                        return assignStringField(field, command.options.keyEnvName, CommandFieldTarget::KeyEnvName, bindings);
                    case CommandField::Protocol:
                        return assignProtocolField(field, command.options.protocol);
                    case CommandField::Step:
                        return assignStepField(field, command.options.step);
                    case CommandField::Layout:
                        return assignLayoutField(field, command.options.layout);
                    case CommandField::StepSearch:
                        return assignBooleanField(field, command.options.enableStepSearch, "step_search");
                    case CommandField::DebugJson:
                        return assignStringField(field, command.options.debugJsonPath, CommandFieldTarget::DebugJsonPath, bindings);
                    case CommandField::DebugSvg:
                        return assignStringField(field, command.options.debugSvgPath, CommandFieldTarget::DebugSvgPath, bindings);
                    case CommandField::Json:
                        return assignBooleanField(field, command.options.json, "json");
                    case CommandField::MaxPixels:
                        return assignPositiveIntegerField(field, command.options.maxPixels);
                    case CommandField::MaxWidth:
                        return assignPositiveIntegerField(field, command.options.maxWidth);
                    case CommandField::MaxHeight:
                        return assignPositiveIntegerField(field, command.options.maxHeight);
                    case CommandField::MaxMessageBytes:
                        return assignPositiveIntegerField(field, command.options.maxMessageBytes);
                }
                return false;
            }

            bool assignStringField(
                const FieldAssignment &field,
                std::string &target,
                CommandFieldTarget targetField,
                std::vector<CommandFieldTemplateBinding> &bindings
            ) {
                std::string staticValue;
                std::string dynamicVariable;
                if (!resolveStringExpression(field.value, staticValue, &dynamicVariable, true)) {
                    addDiagnostic(field.value->span, "field '" + field.name + "' expects a string value");
                    return false;
                }

                if (!dynamicVariable.empty()) {
                    bindings.push_back(CommandFieldTemplateBinding{
                        targetField,
                        std::move(dynamicVariable),
                        field.span
                    });
                    return true;
                }

                target = std::move(staticValue);
                return true;
            }

            bool assignPositiveIntegerField(const FieldAssignment &field, std::optional<long long> &target) {
                double numericValue = 0.0;
                if (!resolveNumberExpression(field.value, numericValue)) {
                    addDiagnostic(field.value->span, "field '" + field.name + "' expects a positive integer");
                    return false;
                }
                const long long integerValue = static_cast<long long>(numericValue);
                if (!std::isfinite(numericValue) || numericValue <= 0.0 ||
                    static_cast<double>(integerValue) != numericValue) {
                    addDiagnostic(field.value->span, "field '" + field.name + "' expects a positive integer");
                    return false;
                }
                target = integerValue;
                return true;
            }

            bool assignStepField(const FieldAssignment &field, float &target) {
                double numericValue = 0.0;
                if (!resolveNumberExpression(field.value, numericValue)) {
                    addDiagnostic(field.value->span, "field 'step' expects a numeric value");
                    return false;
                }
                target = static_cast<float>(numericValue);
                return true;
            }

            bool assignBooleanField(const FieldAssignment &field, bool &target, std::string_view name) {
                bool booleanValue = false;
                if (!resolveBooleanExpression(field.value, booleanValue)) {
                    addDiagnostic(field.value->span, "field '" + std::string(name) + "' expects a boolean value");
                    return false;
                }
                target = booleanValue;
                return true;
            }

            bool assignProtocolField(
                const FieldAssignment &field,
                roadscript::watermark::Protocol &target
            ) {
                const auto enumText = resolveEnumExpression(field.value);
                if (!enumText.has_value()) {
                    addDiagnostic(field.value->span, "invalid protocol value");
                    return false;
                }
                if (*enumText == "classic" || *enumText == "v1") {
                    target = roadscript::watermark::Protocol::Classic;
                    return true;
                }
                if (*enumText == "mosaic" || *enumText == "mosaic_v2" || *enumText == "mosaic-v2") {
                    target = roadscript::watermark::Protocol::Mosaic;
                    return true;
                }
                addDiagnostic(field.value->span, "invalid protocol value");
                return false;
            }

            bool assignLayoutField(
                const FieldAssignment &field,
                roadscript::cli::LayoutMode &target
            ) {
                const auto enumText = resolveEnumExpression(field.value);
                if (!enumText.has_value()) {
                    addDiagnostic(field.value->span, "invalid layout value for this command");
                    return false;
                }
                if (*enumText == "center_ring" || *enumText == "center-ring") {
                    target = roadscript::cli::LayoutMode::CenterRing;
                    return true;
                }
                if (*enumText == "keyed_shuffle" || *enumText == "keyed-shuffle") {
                    target = roadscript::cli::LayoutMode::KeyedShuffle;
                    return true;
                }
                if (*enumText == "auto") {
                    target = roadscript::cli::LayoutMode::Auto;
                    return true;
                }
                addDiagnostic(field.value->span, "invalid layout value for this command");
                return false;
            }

            std::optional<DeferredCondition> lowerCondition(const ExprPtr &expr) {
                if (const auto *call = dynamic_cast<const FunctionCallExpr *>(expr.get())) {
                    return lowerExistsCondition(*call, true, expr->span);
                }
                if (const auto *unary = dynamic_cast<const UnaryExpr *>(expr.get())) {
                    if (unary->op == UnaryOp::Not) {
                        if (const auto *call = dynamic_cast<const FunctionCallExpr *>(unary->operand.get())) {
                            return lowerExistsCondition(*call, false, expr->span);
                        }
                    }
                }

                bool booleanValue = false;
                if (resolveBooleanExpression(expr, booleanValue)) {
                    DeferredCondition condition;
                    condition.kind = DeferredCondition::Kind::StaticBoolean;
                    condition.staticValue = booleanValue;
                    condition.span = expr->span;
                    return condition;
                }

                addDiagnostic(expr->span, "if condition must be a boolean or exists(path)");
                return std::nullopt;
            }

            std::optional<DeferredCondition> lowerExistsCondition(
                const FunctionCallExpr &call,
                bool expectExists,
                const SourceSpan &span
            ) {
                if (call.name != "exists") {
                    addDiagnostic(call.span, "unsupported condition function: " + call.name);
                    return std::nullopt;
                }
                if (call.arguments.size() != 1u) {
                    addDiagnostic(call.span, "exists() expects exactly one argument");
                    return std::nullopt;
                }

                std::string path;
                std::string dynamicVariable;
                if (!resolveStringExpression(call.arguments[0], path, &dynamicVariable, true)) {
                    addDiagnostic(call.arguments[0]->span, "exists() argument must resolve to a string");
                    return std::nullopt;
                }

                DeferredCondition condition;
                condition.kind = DeferredCondition::Kind::ExistsPath;
                condition.expectExists = expectExists;
                condition.path = std::move(path);
                condition.pathVariable = std::move(dynamicVariable);
                condition.span = span;
                return condition;
            }

            bool evaluateStaticExpr(const ExprPtr &expr, LoweredValue &out) {
                if (expr == nullptr) {
                    return false;
                }

                if (const auto *literal = dynamic_cast<const LiteralExpr *>(expr.get())) {
                    switch (literal->kind) {
                        case LiteralKind::String:
                            out = LoweredValue::string(literal->text);
                            return true;
                        case LiteralKind::Number: {
                            char *end = nullptr;
                            const double parsed = std::strtod(literal->text.c_str(), &end);
                            if (end == literal->text.c_str() || (end != nullptr && *end != '\0')) {
                                return false;
                            }
                            out = LoweredValue::number(parsed);
                            return true;
                        }
                        case LiteralKind::Boolean:
                            out = LoweredValue::boolean(literal->text == "true");
                            return true;
                    }
                }

                if (const auto *variable = dynamic_cast<const VariableExpr *>(expr.get())) {
                    const auto binding = lookup(variable->name);
                    if (!binding.has_value()) {
                        addDiagnostic(variable->span, "unknown variable: " + variable->name);
                        return false;
                    }
                    if (binding->kind != VariableBinding::Kind::StaticValue) {
                        return false;
                    }
                    out = binding->value;
                    return true;
                }

                if (const auto *list = dynamic_cast<const ListExpr *>(expr.get())) {
                    std::vector<LoweredValue> elements;
                    elements.reserve(list->elements.size());
                    for (const auto &element : list->elements) {
                        LoweredValue lowered;
                        if (!evaluateStaticExpr(element, lowered)) {
                            return false;
                        }
                        elements.push_back(std::move(lowered));
                    }
                    out = LoweredValue::list(std::move(elements));
                    return true;
                }

                if (const auto *unary = dynamic_cast<const UnaryExpr *>(expr.get())) {
                    if (unary->op != UnaryOp::Not) {
                        return false;
                    }
                    bool booleanValue = false;
                    if (!resolveBooleanExpression(unary->operand, booleanValue)) {
                        return false;
                    }
                    out = LoweredValue::boolean(!booleanValue);
                    return true;
                }

                return false;
            }

            bool resolveStringExpression(
                const ExprPtr &expr,
                std::string &staticValue,
                std::string *dynamicVariable,
                bool allowDynamicVariable
            ) {
                LoweredValue lowered;
                if (evaluateStaticExpr(expr, lowered)) {
                    if (lowered.kind != LoweredValueKind::String) {
                        return false;
                    }
                    staticValue = lowered.stringValue;
                    if (dynamicVariable != nullptr) {
                        dynamicVariable->clear();
                    }
                    return true;
                }

                const auto *variable = dynamic_cast<const VariableExpr *>(expr.get());
                if (!allowDynamicVariable || variable == nullptr) {
                    return false;
                }

                const auto binding = lookup(variable->name);
                if (!binding.has_value()) {
                    addDiagnostic(variable->span, "unknown variable: " + variable->name);
                    return false;
                }
                if (binding->kind != VariableBinding::Kind::DynamicLoopVariable) {
                    return false;
                }
                if (dynamicVariable != nullptr) {
                    *dynamicVariable = binding->variableName;
                }
                staticValue.clear();
                return true;
            }

            bool resolveNumberExpression(const ExprPtr &expr, double &out) {
                LoweredValue lowered;
                if (!evaluateStaticExpr(expr, lowered) || lowered.kind != LoweredValueKind::Number) {
                    return false;
                }
                out = lowered.numberValue;
                return true;
            }

            bool resolveBooleanExpression(const ExprPtr &expr, bool &out) {
                LoweredValue lowered;
                if (!evaluateStaticExpr(expr, lowered) || lowered.kind != LoweredValueKind::Boolean) {
                    return false;
                }
                out = lowered.boolValue;
                return true;
            }

            std::optional<std::string> resolveEnumExpression(const ExprPtr &expr) {
                if (const auto *variable = dynamic_cast<const VariableExpr *>(expr.get())) {
                    const auto binding = lookup(variable->name);
                    if (binding.has_value()) {
                        if (binding->kind != VariableBinding::Kind::StaticValue ||
                            binding->value.kind != LoweredValueKind::String) {
                            return std::nullopt;
                        }
                        return binding->value.stringValue;
                    }
                    return variable->name;
                }

                LoweredValue lowered;
                if (!evaluateStaticExpr(expr, lowered) || lowered.kind != LoweredValueKind::String) {
                    return std::nullopt;
                }
                return lowered.stringValue;
            }
        };
    } // namespace

    DslLoweringResult lowerWorkflowProgram(const Program &program) {
        return Lowerer(program).run();
    }
} // namespace roadscript::cli::dsl
