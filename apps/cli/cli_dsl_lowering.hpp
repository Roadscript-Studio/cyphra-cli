#pragma once

#include "cli_dsl_ast.hpp"
#include "cli_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace roadscript::cli::dsl {
    enum class WorkflowStepKind {
        Command,
        If,
        ForEachGlob,
    };

    enum class CommandFieldTarget {
        InputPath,
        OutputPath,
        Message,
        Key,
        KeyFilePath,
        KeyEnvName,
        DebugJsonPath,
        DebugSvgPath,
        MaxPixels,
        MaxWidth,
        MaxHeight,
        MaxMessageBytes,
        Step,
        Protocol,
        Layout,
        StepSearch,
        Json,
    };

    struct CommandFieldTemplateBinding {
        CommandFieldTarget target = CommandFieldTarget::InputPath;
        std::string variableName;
        SourceSpan span;
    };

    struct DeferredCondition {
        enum class Kind {
            StaticBoolean,
            ExistsPath,
        };

        Kind kind = Kind::StaticBoolean;
        bool staticValue = false;
        bool expectExists = true;
        std::string path;
        std::string pathVariable;
        SourceSpan span;
    };

    struct DeferredForEachGlob {
        std::string variableName;
        std::string pattern;
        SourceSpan span;
    };

    struct DslWorkflowStep {
        WorkflowStepKind kind = WorkflowStepKind::Command;
        SourceSpan span;
        std::string label;
        std::optional<roadscript::cli::CommandIR> command;
        std::vector<CommandFieldTemplateBinding> fieldBindings;
        std::optional<DeferredCondition> condition;
        std::optional<DeferredForEachGlob> forEachGlob;
        std::vector<DslWorkflowStep> body;
        std::vector<DslWorkflowStep> elseBody;
    };

    struct DslWorkflowPlan {
        std::string file;
        std::vector<DslWorkflowStep> steps;
    };

    struct DslLoweringResult {
        DslWorkflowPlan plan;
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool ok() const { return diagnostics.empty(); }
    };

    DslLoweringResult lowerWorkflowProgram(const Program &program);
} // namespace roadscript::cli::dsl
