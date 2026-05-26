#include "cli_dsl_runtime.hpp"

#include "cli_commands.hpp"
#include "cli_dsl_lowering.hpp"
#include "cli_dsl_parser.hpp"
#include "cli_dsl_semantic.hpp"
#include "cli_print.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace roadscript::cli {
    namespace {
        namespace fs = std::filesystem;

        struct RuntimeStats {
            int commandStepsPlanned = 0;
            int commandStepsExecuted = 0;
            int commandStepsSkipped = 0;
            int failedSteps = 0;
            std::size_t failedStepNumber = 0;
            std::string failedCommand;
            std::string failedSource;
            std::string failedReason;
            std::vector<std::string> artifactPaths;
        };

        struct RuntimeContext {
            RunCommandOptions options;
            fs::path scriptPath;
            std::size_t nextStepNumber = 1;
            RuntimeStats stats;
        };

        using RuntimeBindings = std::map<std::string, std::string>;

        std::string readTextFile(const fs::path &path) {
            std::ifstream in(path, std::ios::binary);
            std::ostringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }

        void printDslDiagnostics(const std::vector<dsl::Diagnostic> &diagnostics) {
            for (const auto &diagnostic : diagnostics) {
                printDiagnostic(std::cerr, diagnostic);
            }
        }

        std::size_t countCommandSteps(const std::vector<dsl::DslWorkflowStep> &steps) {
            std::size_t count = 0;
            for (const auto &step : steps) {
                switch (step.kind) {
                    case dsl::WorkflowStepKind::Command:
                        ++count;
                        break;
                    case dsl::WorkflowStepKind::If:
                        count += countCommandSteps(step.body);
                        count += countCommandSteps(step.elseBody);
                        break;
                    case dsl::WorkflowStepKind::ForEachGlob:
                        count += countCommandSteps(step.body);
                        break;
                }
            }
            return count;
        }

        std::string spanPrefix(const dsl::SourceSpan &span) {
            std::ostringstream out;
            out << span.file << ":" << span.begin.line << ":" << span.begin.column;
            return out.str();
        }

        bool wildcardMatch(std::string_view pattern, std::string_view text) {
            std::size_t p = 0;
            std::size_t t = 0;
            std::size_t star = std::string_view::npos;
            std::size_t match = 0;

            while (t < text.size()) {
                if (p < pattern.size() &&
                    (pattern[p] == '?' || pattern[p] == text[t])) {
                    ++p;
                    ++t;
                    continue;
                }
                if (p < pattern.size() && pattern[p] == '*') {
                    star = p++;
                    match = t;
                    continue;
                }
                if (star != std::string_view::npos) {
                    p = star + 1u;
                    t = ++match;
                    continue;
                }
                return false;
            }

            while (p < pattern.size() && pattern[p] == '*') {
                ++p;
            }
            return p == pattern.size();
        }

        std::vector<std::string> expandGlobPattern(const std::string &pattern) {
            fs::path patternPath(pattern);
            fs::path parent = patternPath.parent_path();
            if (parent.empty()) {
                parent = ".";
            }

            const std::string filenamePattern = patternPath.filename().string();
            std::vector<std::string> matches;
            std::error_code ec;
            if (!fs::exists(parent, ec) || !fs::is_directory(parent, ec)) {
                return matches;
            }

            for (const auto &entry : fs::directory_iterator(parent, ec)) {
                if (ec) {
                    break;
                }
                if (wildcardMatch(filenamePattern, entry.path().filename().string())) {
                    matches.push_back(entry.path().string());
                }
            }

            std::sort(matches.begin(), matches.end());
            return matches;
        }

        bool resolveBoundString(
            const std::string &staticValue,
            std::string_view dynamicVariable,
            const RuntimeBindings &bindings,
            std::string &out
        ) {
            if (!dynamicVariable.empty()) {
                const auto found = bindings.find(std::string(dynamicVariable));
                if (found == bindings.end()) {
                    return false;
                }
                out = found->second;
                return true;
            }
            out = staticValue;
            return true;
        }

        bool instantiateCommand(
            const dsl::DslWorkflowStep &step,
            const RuntimeBindings &bindings,
            CommandIR &outCommand,
            std::string &error
        ) {
            if (!step.command.has_value()) {
                error = "workflow command step is missing CommandIR";
                return false;
            }

            outCommand = *step.command;
            for (const auto &binding : step.fieldBindings) {
                std::string resolved;
                std::string_view variableName = binding.variableName;
                switch (binding.target) {
                    case dsl::CommandFieldTarget::InputPath:
                        if (!resolveBoundString(outCommand.options.inputPath, variableName, bindings, resolved)) {
                            error = "unbound workflow variable: " + std::string(variableName);
                            return false;
                        }
                        outCommand.options.inputPath = std::move(resolved);
                        break;
                    case dsl::CommandFieldTarget::OutputPath:
                        if (!resolveBoundString(outCommand.options.outputPath, variableName, bindings, resolved)) {
                            error = "unbound workflow variable: " + std::string(variableName);
                            return false;
                        }
                        outCommand.options.outputPath = std::move(resolved);
                        break;
                    case dsl::CommandFieldTarget::Message:
                        if (!resolveBoundString(outCommand.options.message, variableName, bindings, resolved)) {
                            error = "unbound workflow variable: " + std::string(variableName);
                            return false;
                        }
                        outCommand.options.message = std::move(resolved);
                        outCommand.options.hasMessage = true;
                        break;
                    case dsl::CommandFieldTarget::Key:
                        if (!resolveBoundString("", variableName, bindings, resolved)) {
                            error = "unbound workflow variable: " + std::string(variableName);
                            return false;
                        }
                        outCommand.options.key = std::move(resolved);
                        break;
                    case dsl::CommandFieldTarget::KeyFilePath:
                        if (!resolveBoundString(outCommand.options.keyFilePath, variableName, bindings, resolved)) {
                            error = "unbound workflow variable: " + std::string(variableName);
                            return false;
                        }
                        outCommand.options.keyFilePath = std::move(resolved);
                        break;
                    case dsl::CommandFieldTarget::KeyEnvName:
                        if (!resolveBoundString(outCommand.options.keyEnvName, variableName, bindings, resolved)) {
                            error = "unbound workflow variable: " + std::string(variableName);
                            return false;
                        }
                        outCommand.options.keyEnvName = std::move(resolved);
                        break;
                    case dsl::CommandFieldTarget::DebugJsonPath:
                        if (!resolveBoundString(outCommand.options.debugJsonPath, variableName, bindings, resolved)) {
                            error = "unbound workflow variable: " + std::string(variableName);
                            return false;
                        }
                        outCommand.options.debugJsonPath = std::move(resolved);
                        break;
                    case dsl::CommandFieldTarget::DebugSvgPath:
                        if (!resolveBoundString(outCommand.options.debugSvgPath, variableName, bindings, resolved)) {
                            error = "unbound workflow variable: " + std::string(variableName);
                            return false;
                        }
                        outCommand.options.debugSvgPath = std::move(resolved);
                        break;
                    case dsl::CommandFieldTarget::MaxPixels:
                    case dsl::CommandFieldTarget::MaxWidth:
                    case dsl::CommandFieldTarget::MaxHeight:
                    case dsl::CommandFieldTarget::MaxMessageBytes:
                    case dsl::CommandFieldTarget::Step:
                    case dsl::CommandFieldTarget::Protocol:
                    case dsl::CommandFieldTarget::Layout:
                    case dsl::CommandFieldTarget::StepSearch:
                    case dsl::CommandFieldTarget::Json:
                        error = "unsupported dynamic binding target in workflow runtime";
                        return false;
                }
            }

            return true;
        }

        void recordCommandArtifacts(const CommandIR &command, RuntimeStats &stats) {
            auto addPath = [&](const std::string &path) {
                if (path.empty()) {
                    return;
                }
                if (std::find(stats.artifactPaths.begin(), stats.artifactPaths.end(), path) ==
                    stats.artifactPaths.end()) {
                    stats.artifactPaths.push_back(path);
                }
            };

            addPath(command.options.outputPath);
            addPath(command.options.debugJsonPath);
            addPath(command.options.debugSvgPath);
        }

        bool evaluateCondition(
            const dsl::DeferredCondition &condition,
            const RuntimeBindings &bindings,
            bool &out,
            std::string &error
        ) {
            if (condition.kind == dsl::DeferredCondition::Kind::StaticBoolean) {
                out = condition.staticValue;
                return true;
            }

            std::string path;
            if (!resolveBoundString(condition.path, condition.pathVariable, bindings, path)) {
                error = "unbound workflow variable: " + condition.pathVariable;
                return false;
            }

            const bool exists = fs::exists(path);
            out = condition.expectExists ? exists : !exists;
            return true;
        }

        void printWorkflowCommandPreview(const CommandIR &command) {
            printKeyValue(std::cout, "Command", std::string(commandKindName(command.kind)));
            if (!command.options.inputPath.empty()) {
                printKeyValue(std::cout, "Input", command.options.inputPath);
            }
            if (!command.options.outputPath.empty()) {
                printKeyValue(std::cout, "Output", command.options.outputPath);
                printKeyValue(std::cout, "Output exists", formatBoolYesNo(fs::exists(command.options.outputPath)));
            }
            if (command.options.hasMessage) {
                printKeyValue(std::cout, "Message bytes", command.options.message.size());
            }
            printKeyValue(std::cout, "Protocol", formatProtocolLabel(command.options.protocol));
            printKeyValue(std::cout, "Step", command.options.step, 2);
            printKeyValue(std::cout, "Layout", layoutToString(command.options.layout));
            if (!command.options.debugJsonPath.empty()) {
                printKeyValue(std::cout, "Debug JSON", command.options.debugJsonPath);
                printKeyValue(std::cout, "Debug JSON exists", formatBoolYesNo(fs::exists(command.options.debugJsonPath)));
            }
            if (!command.options.debugSvgPath.empty()) {
                printKeyValue(std::cout, "Debug SVG", command.options.debugSvgPath);
                printKeyValue(std::cout, "Debug SVG exists", formatBoolYesNo(fs::exists(command.options.debugSvgPath)));
            }
            printKeyValue(
                std::cout,
                "Key mode",
                command.options.key.has_value() || !command.options.keyFilePath.empty() || !command.options.keyEnvName.empty()
                    ? "provided"
                    : "none"
            );
            if (command.options.json) {
                printKeyValue(std::cout, "JSON", "true");
            }
        }

        void printWorkflowSummary(const RuntimeContext &context, double elapsedMs) {
            printSectionHeader(std::cout, "Workflow Summary");
            printSectionHeader(std::cout, "Execution");
            printKeyValue(std::cout, "Script", context.options.scriptPath);
            printKeyValue(std::cout, "Dry run", formatBoolYesNo(context.options.dryRun));
            printKeyValue(std::cout, "Elapsed ms", elapsedMs, 2);
            std::cout << "\n";
            printSectionHeader(std::cout, "Counts");
            printKeyValue(std::cout, "Planned steps", context.stats.commandStepsPlanned);
            printKeyValue(std::cout, "Executed steps", context.stats.commandStepsExecuted);
            printKeyValue(std::cout, "Skipped steps", context.stats.commandStepsSkipped);
            printKeyValue(std::cout, "Failures", context.stats.failedSteps);
            if (context.stats.failedStepNumber > 0) {
                printKeyValue(std::cout, "Failed step", static_cast<long long>(context.stats.failedStepNumber));
            }
            if (!context.stats.failedCommand.empty()) {
                printKeyValue(std::cout, "Failed command", context.stats.failedCommand);
            }
            if (!context.stats.failedSource.empty()) {
                printKeyValue(std::cout, "Failed source", context.stats.failedSource);
            }
            if (!context.stats.failedReason.empty()) {
                printKeyValue(std::cout, "Failure detail", context.stats.failedReason);
            }
            if (!context.stats.artifactPaths.empty()) {
                std::cout << "\n";
                printSectionHeader(std::cout, "Artifacts");
                for (std::size_t i = 0; i < context.stats.artifactPaths.size(); ++i) {
                    printKeyValue(std::cout, "Artifact " + std::to_string(i + 1), context.stats.artifactPaths[i]);
                }
            }
        }

        bool validateWorkflowJsonCompatibility(
            const std::vector<dsl::DslWorkflowStep> &steps,
            std::vector<dsl::Diagnostic> &diagnostics
        ) {
            bool ok = true;
            for (const auto &step : steps) {
                if (step.kind == dsl::WorkflowStepKind::Command && step.command.has_value() && step.command->options.json) {
                    diagnostics.push_back(dsl::Diagnostic{
                        step.span,
                        "workflow execution with json: true is not supported yet; use direct CLI commands or --dry-run"
                    });
                    ok = false;
                }
                if (step.kind == dsl::WorkflowStepKind::If) {
                    ok = validateWorkflowJsonCompatibility(step.body, diagnostics) && ok;
                    ok = validateWorkflowJsonCompatibility(step.elseBody, diagnostics) && ok;
                }
                if (step.kind == dsl::WorkflowStepKind::ForEachGlob) {
                    ok = validateWorkflowJsonCompatibility(step.body, diagnostics) && ok;
                }
            }
            return ok;
        }

        int executeWorkflowSteps(
            const std::vector<dsl::DslWorkflowStep> &steps,
            RuntimeContext &context,
            RuntimeBindings &bindings,
            std::string_view argv0
        ) {
            for (const auto &step : steps) {
                if (step.kind == dsl::WorkflowStepKind::Command) {
                    CommandIR command;
                    std::string instantiateError;
                    if (!instantiateCommand(step, bindings, command, instantiateError)) {
                        context.stats.failedSteps++;
                        context.stats.failedStepNumber = context.nextStepNumber;
                        context.stats.failedCommand = step.label;
                        context.stats.failedSource = spanPrefix(step.span);
                        context.stats.failedReason = instantiateError;
                        printDiagnostic(
                            std::cerr,
                            dsl::Diagnostic{step.span, "step " + std::to_string(context.nextStepNumber) +
                                " (" + step.label + ") failed: " + instantiateError}
                        );
                        return 1;
                    }

                    const std::size_t stepNumber = context.nextStepNumber++;
                    recordCommandArtifacts(command, context.stats);
                    if (context.options.dryRun) {
                        printSectionHeader(std::cout, "Workflow Step " + std::to_string(stepNumber));
                        printKeyValue(std::cout, "Source", spanPrefix(step.span));
                        printWorkflowCommandPreview(command);
                        continue;
                    }

                    printSectionHeader(std::cout, "Workflow Step " + std::to_string(stepNumber));
                    printKeyValue(std::cout, "Source", spanPrefix(step.span));
                    printKeyValue(std::cout, "Command", std::string(commandKindName(command.kind)));
                    const int exitCode = executeCommandIR(command, argv0);
                    if (exitCode != 0) {
                        context.stats.failedSteps++;
                        context.stats.failedStepNumber = stepNumber;
                        context.stats.failedCommand = std::string(commandKindName(command.kind));
                        context.stats.failedSource = spanPrefix(step.span);
                        context.stats.failedReason =
                            "command failed with exit code " + std::to_string(exitCode);
                        printDiagnostic(
                            std::cerr,
                            dsl::Diagnostic{step.span, "step " + std::to_string(stepNumber) +
                                " (" + step.label + ") failed with exit code " +
                                std::to_string(exitCode)}
                        );
                        return exitCode;
                    }
                    context.stats.commandStepsExecuted++;
                    continue;
                }

                if (step.kind == dsl::WorkflowStepKind::If) {
                    bool conditionValue = false;
                    std::string conditionError;
                    if (!step.condition.has_value() ||
                        !evaluateCondition(*step.condition, bindings, conditionValue, conditionError)) {
                        context.stats.failedSteps++;
                        context.stats.failedStepNumber = context.nextStepNumber;
                        context.stats.failedCommand = step.label;
                        context.stats.failedSource = spanPrefix(step.span);
                        context.stats.failedReason = conditionError;
                        printDiagnostic(
                            std::cerr,
                            dsl::Diagnostic{step.span, "failed to evaluate condition: " + conditionError}
                        );
                        return 1;
                    }

                    const auto &chosen = conditionValue ? step.body : step.elseBody;
                    const auto &skipped = conditionValue ? step.elseBody : step.body;
                    context.stats.commandStepsSkipped += static_cast<int>(countCommandSteps(skipped));

                    if (context.options.dryRun) {
                        printSectionHeader(std::cout, "Workflow Condition");
                        printKeyValue(std::cout, "Source", spanPrefix(step.span));
                        printKeyValue(std::cout, "Result", conditionValue ? "true" : "false");
                    }

                    const int rc = executeWorkflowSteps(chosen, context, bindings, argv0);
                    if (rc != 0) {
                        return rc;
                    }
                    continue;
                }

                if (step.kind == dsl::WorkflowStepKind::ForEachGlob) {
                    const std::string pattern =
                        step.forEachGlob.has_value() ? step.forEachGlob->pattern : std::string();
                    const std::vector<std::string> matches = expandGlobPattern(pattern);

                    if (context.options.dryRun) {
                        printSectionHeader(std::cout, "Workflow Loop");
                        printKeyValue(std::cout, "Source", spanPrefix(step.span));
                        printKeyValue(std::cout, "Glob", pattern);
                        printKeyValue(std::cout, "Matches", matches.size());
                    }

                    if (matches.empty()) {
                        context.stats.commandStepsSkipped += static_cast<int>(countCommandSteps(step.body));
                        if (!context.options.dryRun) {
                            printSectionHeader(std::cout, "Workflow Loop");
                            printKeyValue(std::cout, "Source", spanPrefix(step.span));
                            printKeyValue(std::cout, "Glob", pattern);
                            printKeyValue(std::cout, "Matches", 0);
                        }
                        printKeyValue(std::cout, "Loop status", "0 matches; loop skipped");
                        continue;
                    }

                    for (const auto &match : matches) {
                        RuntimeBindings iterationBindings = bindings;
                        if (step.forEachGlob.has_value()) {
                            iterationBindings[step.forEachGlob->variableName] = match;
                        }
                        const int rc = executeWorkflowSteps(step.body, context, iterationBindings, argv0);
                        if (rc != 0) {
                            return rc;
                        }
                    }
                }
            }

            return 0;
        }

        bool workflowUsesJsonMode(const std::vector<dsl::DslWorkflowStep> &steps) {
            for (const auto &step : steps) {
                if (step.kind == dsl::WorkflowStepKind::Command &&
                    step.command.has_value() &&
                    step.command->options.json) {
                    return true;
                }
                if (step.kind == dsl::WorkflowStepKind::If) {
                    if (workflowUsesJsonMode(step.body) || workflowUsesJsonMode(step.elseBody)) {
                        return true;
                    }
                }
                if (step.kind == dsl::WorkflowStepKind::ForEachGlob &&
                    workflowUsesJsonMode(step.body)) {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    int runDslWorkflow(const RunCommandOptions &options, std::string_view argv0) {
        const auto workflowStart = std::chrono::steady_clock::now();
        const std::filesystem::path scriptPath = std::filesystem::path(options.scriptPath);
        if (!fs::exists(scriptPath)) {
            printHumanError(std::cerr, "workflow script not found: " + options.scriptPath);
            printTryLine(std::cerr, std::string(argv0) + " run examples/classic_roundtrip.rsx --dry-run");
            return 1;
        }

        std::string source;
        try {
            source = readTextFile(scriptPath);
            if (source.empty()) {
                std::ifstream in(scriptPath, std::ios::binary);
                if (!in) {
                    throw std::runtime_error(
                        "failed to read workflow script: " + options.scriptPath
                    );
                }
            }
        } catch (const std::exception &e) {
            printHumanError(std::cerr, e.what());
            return 1;
        }

        const auto parsed = dsl::parseWorkflowScript(options.scriptPath, source);
        if (!parsed.ok()) {
            printDslDiagnostics(parsed.diagnostics);
            return 1;
        }

        const auto semantic = dsl::validateWorkflowProgram(parsed.program);
        if (!semantic.ok()) {
            printDslDiagnostics(semantic.diagnostics);
            return 1;
        }

        const auto lowered = dsl::lowerWorkflowProgram(parsed.program);
        if (!lowered.ok()) {
            printDslDiagnostics(lowered.diagnostics);
            return 1;
        }

        if (!options.dryRun) {
            std::vector<dsl::Diagnostic> runtimeDiagnostics;
            if (!validateWorkflowJsonCompatibility(lowered.plan.steps, runtimeDiagnostics)) {
                printDslDiagnostics(runtimeDiagnostics);
                return 1;
            }
        }

        RuntimeContext context{
            options,
            scriptPath,
            1,
            RuntimeStats{
                static_cast<int>(countCommandSteps(lowered.plan.steps)),
                0,
                0,
                0,
                0,
                {},
                {},
                {},
                {}
            }
        };

        printSectionHeader(std::cout, "Workflow");
        printKeyValue(std::cout, "Script", options.scriptPath);
        printKeyValue(std::cout, "Dry run", formatBoolYesNo(options.dryRun));
        printKeyValue(
            std::cout,
            "Workflow JSON",
            workflowUsesJsonMode(lowered.plan.steps) ? "deferred" : "none"
        );

        RuntimeBindings bindings;
        const int rc = executeWorkflowSteps(lowered.plan.steps, context, bindings, argv0);
        const auto workflowEnd = std::chrono::steady_clock::now();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(workflowEnd - workflowStart).count();
        printWorkflowSummary(context, elapsedMs);
        return rc;
    }
} // namespace roadscript::cli
