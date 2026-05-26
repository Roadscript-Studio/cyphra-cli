#include "apps/cli/cli_dsl_lowering.hpp"
#include "apps/cli/cli_dsl_parser.hpp"
#include "apps/cli/cli_dsl_semantic.hpp"
#include "apps/cli/cli_ir_validation.hpp"

#include <iostream>
#include <optional>
#include <string>

namespace {
    int require(bool condition, const std::string &message) {
        if (!condition) {
            std::cerr << "[FAIL] " << message << "\n";
            return 1;
        }
        return 0;
    }
}

int main() {
    using namespace roadscript::cli;
    using namespace roadscript::cli::dsl;

    const auto parsed = parseWorkflowScript(
        "lower.rsx",
        "let input = \"tests/fixtures/input/input.jpg\"\n"
        "let outputs = [\"a.png\", \"b.png\"]\n"
        "embed {\n"
        "  in: input\n"
        "  out: \"classic.png\"\n"
        "  msg_block: \"hello classic\"\n"
        "  protocol: classic\n"
        "  step: 30.0\n"
        "  layout: center_ring\n"
        "}\n"
        "verify {\n"
        "  in: \"mosaic.png\"\n"
        "  protocol: mosaic\n"
        "  layout: auto\n"
        "  step_search: true\n"
        "  debug_json: \"mosaic.json\"\n"
        "  debug_svg: \"mosaic.svg\"\n"
        "}\n"
        "for out_file in outputs {\n"
        "  info { in: input msg_block: out_file protocol: classic }\n"
        "}\n"
        "if not exists(\"artifact.png\") {\n"
        "  doctor {}\n"
        "} else {\n"
        "  version {}\n"
        "}\n"
        "for file in glob(\"photos/*.jpg\") {\n"
        "  info { in: file protocol: classic }\n"
        "}\n"
    );
    if (int rc = require(parsed.ok(), "workflow should parse")) return rc;

    const auto semantic = validateWorkflowProgram(parsed.program);
    if (int rc = require(semantic.ok(), "workflow should pass semantic validation")) return rc;

    const auto lowered = lowerWorkflowProgram(parsed.program);
    if (int rc = require(lowered.ok(), "workflow should lower successfully")) return rc;
    if (int rc = require(lowered.plan.steps.size() == 6u, "expected six lowered workflow steps")) return rc;

    const auto &embedStep = lowered.plan.steps[0];
    if (int rc = require(embedStep.kind == WorkflowStepKind::Command, "first step should be command")) return rc;
    if (int rc = require(embedStep.command.has_value(), "embed command should be present")) return rc;
    if (int rc = require(embedStep.command->kind == CommandKind::Embed, "first command should be embed")) return rc;
    if (int rc = require(embedStep.command->options.inputPath == "tests/fixtures/input/input.jpg", "embed input mismatch")) return rc;
    if (int rc = require(embedStep.command->options.outputPath == "classic.png", "embed output mismatch")) return rc;
    if (int rc = require(embedStep.command->options.message == "hello classic", "embed message mismatch")) return rc;
    if (int rc = require(embedStep.command->options.protocol == roadscript::watermark::Protocol::Classic, "embed protocol mismatch")) return rc;
    if (int rc = require(embedStep.command->options.layout == LayoutMode::CenterRing, "embed layout mismatch")) return rc;
    if (int rc = require(validateCommandRequirements(*embedStep.command, "rse") == 0, "embed IR should validate")) return rc;

    const auto &verifyStep = lowered.plan.steps[1];
    if (int rc = require(verifyStep.command.has_value(), "verify command should be present")) return rc;
    if (int rc = require(verifyStep.command->kind == CommandKind::Verify, "second command should be verify")) return rc;
    if (int rc = require(verifyStep.command->options.protocol == roadscript::watermark::Protocol::Mosaic, "verify protocol mismatch")) return rc;
    if (int rc = require(verifyStep.command->options.layout == LayoutMode::Auto, "verify layout mismatch")) return rc;
    if (int rc = require(verifyStep.command->options.enableStepSearch, "verify step_search mismatch")) return rc;
    if (int rc = require(verifyStep.command->options.debugJsonPath == "mosaic.json", "verify debug_json mismatch")) return rc;
    if (int rc = require(verifyStep.command->options.debugSvgPath == "mosaic.svg", "verify debug_svg mismatch")) return rc;

    const auto &unrolledInfoA = lowered.plan.steps[2];
    const auto &unrolledInfoB = lowered.plan.steps[3];
    if (int rc = require(unrolledInfoA.command.has_value() && unrolledInfoB.command.has_value(), "unrolled info commands should be present")) return rc;
    if (int rc = require(unrolledInfoA.command->options.message == "a.png", "first unrolled loop value mismatch")) return rc;
    if (int rc = require(unrolledInfoB.command->options.message == "b.png", "second unrolled loop value mismatch")) return rc;

    const auto &ifStep = lowered.plan.steps[4];
    if (int rc = require(ifStep.kind == WorkflowStepKind::If, "fifth step should be if")) return rc;
    if (int rc = require(ifStep.condition.has_value(), "if condition should be preserved")) return rc;
    if (int rc = require(ifStep.condition->kind == DeferredCondition::Kind::ExistsPath, "if condition should be exists")) return rc;
    if (int rc = require(!ifStep.condition->expectExists, "if condition should represent not exists")) return rc;
    if (int rc = require(ifStep.condition->path == "artifact.png", "if path mismatch")) return rc;
    if (int rc = require(ifStep.body.size() == 1u && ifStep.elseBody.size() == 1u, "if branches should lower")) return rc;

    const auto &globStep = lowered.plan.steps[5];
    if (int rc = require(globStep.kind == WorkflowStepKind::ForEachGlob, "last step should be glob loop")) return rc;
    if (int rc = require(globStep.forEachGlob.has_value(), "glob loop metadata missing")) return rc;
    if (int rc = require(globStep.forEachGlob->variableName == "file", "glob loop variable mismatch")) return rc;
    if (int rc = require(globStep.forEachGlob->pattern == "photos/*.jpg", "glob loop pattern mismatch")) return rc;
    if (int rc = require(globStep.body.size() == 1u, "glob loop body should contain one command")) return rc;
    if (int rc = require(globStep.body[0].command.has_value(), "glob loop command template missing")) return rc;
    if (int rc = require(globStep.body[0].command->kind == CommandKind::Info, "glob loop command should be info")) return rc;
    if (int rc = require(globStep.body[0].fieldBindings.size() == 1u, "glob loop command should preserve one template binding")) return rc;
    if (int rc = require(globStep.body[0].fieldBindings[0].target == CommandFieldTarget::InputPath, "glob binding target mismatch")) return rc;
    if (int rc = require(globStep.body[0].fieldBindings[0].variableName == "file", "glob binding variable mismatch")) return rc;

    const auto badParsed = parseWorkflowScript("bad.rsx", "info { in: missing protocol: classic }\n");
    if (int rc = require(badParsed.ok(), "bad workflow should still parse")) return rc;
    const auto badLowered = lowerWorkflowProgram(badParsed.program);
    if (int rc = require(!badLowered.ok(), "undefined variable should fail lowering")) return rc;
    if (int rc = require(!badLowered.diagnostics.empty(), "undefined variable should produce diagnostics")) return rc;
    if (int rc = require(badLowered.diagnostics.front().span.begin.line == 1, "undefined variable diagnostic line mismatch")) return rc;

    std::cout << "[PASS] dsl_lowering_test\n";
    return 0;
}
