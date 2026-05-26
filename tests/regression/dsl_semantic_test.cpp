#include "apps/cli/cli_dsl_parser.hpp"
#include "apps/cli/cli_dsl_semantic.hpp"

#include <iostream>
#include <string>

namespace {
    int require(bool condition, const std::string &message) {
        if (!condition) {
            std::cerr << "[FAIL] " << message << "\n";
            return 1;
        }
        return 0;
    }

    bool hasMessage(const roadscript::cli::dsl::SemanticResult &result, const std::string &needle) {
        for (const auto &diagnostic : result.diagnostics) {
            if (diagnostic.message.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
}

int main() {
    using namespace roadscript::cli::dsl;

    const auto validProgram = parseWorkflowScript(
        "valid.rsx",
        "let input = \"tests/fixtures/input/input.jpg\"\n"
        "let outputs = [\"a.png\", \"b.png\"]\n"
        "info { in: input protocol: classic layout: center_ring json: true }\n"
        "for out_file in outputs {\n"
        "  if not exists(out_file) {\n"
        "    embed {\n"
        "      in: input\n"
        "      out: out_file\n"
        "      msg_block: \"hello\"\n"
        "      protocol: mosaic\n"
        "      step: 30.0\n"
        "      layout: keyed_shuffle\n"
        "      max_message_bytes: 100\n"
        "    }\n"
        "  }\n"
        "}\n"
        "verify { in: \"out.png\" protocol: mosaic layout: auto step_search: true debug_json: \"a.json\" debug_svg: \"a.svg\" }\n"
    );
    if (int rc = require(validProgram.ok(), "valid script should parse")) return rc;
    const auto validSemantic = validateWorkflowProgram(validProgram.program);
    if (int rc = require(validSemantic.ok(), "valid script should pass semantic validation")) return rc;

    const auto missingRequired = parseWorkflowScript("missing.rsx", "embed { in: \"a\" out: \"b\" }\n");
    if (int rc = require(missingRequired.ok(), "missing-required script should parse")) return rc;
    const auto missingRequiredSemantic = validateWorkflowProgram(missingRequired.program);
    if (int rc = require(!missingRequiredSemantic.ok(), "missing-required script should fail semantic validation")) return rc;
    if (int rc = require(hasMessage(missingRequiredSemantic, "missing required field: msg_block"), "missing msg_block diagnostic missing")) return rc;

    const auto duplicateField = parseWorkflowScript("dup.rsx", "info { in: \"a\" in: \"b\" }\n");
    if (int rc = require(duplicateField.ok(), "duplicate-field script should parse")) return rc;
    const auto duplicateFieldSemantic = validateWorkflowProgram(duplicateField.program);
    if (int rc = require(hasMessage(duplicateFieldSemantic, "duplicate field: in"), "duplicate field diagnostic missing")) return rc;

    const auto badLayout = parseWorkflowScript("layout.rsx", "embed { in: \"a\" out: \"b\" msg_block: \"x\" layout: auto }\n");
    if (int rc = require(badLayout.ok(), "bad-layout script should parse")) return rc;
    const auto badLayoutSemantic = validateWorkflowProgram(badLayout.program);
    if (int rc = require(hasMessage(badLayoutSemantic, "invalid layout value for this command"), "bad layout diagnostic missing")) return rc;

    const auto badInfoLayout = parseWorkflowScript("info-layout.rsx", "info { in: \"a\" layout: auto }\n");
    if (int rc = require(badInfoLayout.ok(), "bad info-layout script should parse")) return rc;
    const auto badInfoLayoutSemantic = validateWorkflowProgram(badInfoLayout.program);
    if (int rc = require(hasMessage(badInfoLayoutSemantic, "invalid layout value for this command"), "info layout diagnostic missing")) return rc;

    const auto keyConflict = parseWorkflowScript("keys.rsx", "info { in: \"a\" key: \"k\" key_env: \"RS_KEY\" }\n");
    if (int rc = require(keyConflict.ok(), "key-conflict script should parse")) return rc;
    const auto keyConflictSemantic = validateWorkflowProgram(keyConflict.program);
    if (int rc = require(hasMessage(keyConflictSemantic, "only one of key, key_file, or key_env may be used"), "key conflict diagnostic missing")) return rc;

    const auto unknownCommand = parseWorkflowScript("unknown.rsx", "banana { in: \"a\" }\n");
    if (int rc = require(unknownCommand.ok(), "unknown-command script should parse")) return rc;
    const auto unknownCommandSemantic = validateWorkflowProgram(unknownCommand.program);
    if (int rc = require(hasMessage(unknownCommandSemantic, "unknown command"), "unknown command diagnostic missing")) return rc;

    const auto unknownField = parseWorkflowScript("field.rsx", "verify { in: \"a\" msg_block: \"oops\" }\n");
    if (int rc = require(unknownField.ok(), "unknown-field script should parse")) return rc;
    const auto unknownFieldSemantic = validateWorkflowProgram(unknownField.program);
    if (int rc = require(hasMessage(unknownFieldSemantic, "unknown or unsupported field for command: msg_block"), "unknown field diagnostic missing")) return rc;

    const auto unsupportedDebug = parseWorkflowScript("debug.rsx", "embed { in: \"a\" out: \"b\" msg_block: \"x\" debug_json: \"oops.json\" }\n");
    if (int rc = require(unsupportedDebug.ok(), "unsupported-debug script should parse")) return rc;
    const auto unsupportedDebugSemantic = validateWorkflowProgram(unsupportedDebug.program);
    if (int rc = require(hasMessage(unsupportedDebugSemantic, "unknown or unsupported field for command: debug_json"), "unsupported debug field diagnostic missing")) return rc;

    const auto unsupportedMaxMessage = parseWorkflowScript("max-message.rsx", "verify { in: \"a\" max_message_bytes: 64 }\n");
    if (int rc = require(unsupportedMaxMessage.ok(), "unsupported max-message script should parse")) return rc;
    const auto unsupportedMaxMessageSemantic = validateWorkflowProgram(unsupportedMaxMessage.program);
    if (int rc = require(hasMessage(unsupportedMaxMessageSemantic, "unknown or unsupported field for command: max_message_bytes"), "unsupported max_message_bytes diagnostic missing")) return rc;

    const auto badTypes = parseWorkflowScript("types.rsx", "extract { in: true step_search: \"yes\" }\n");
    if (int rc = require(badTypes.ok(), "bad-types script should parse")) return rc;
    const auto badTypesSemantic = validateWorkflowProgram(badTypes.program);
    if (int rc = require(hasMessage(badTypesSemantic, "field 'in' expects a string value"), "bad type for in diagnostic missing")) return rc;
    if (int rc = require(hasMessage(badTypesSemantic, "field 'step_search' expects a boolean value"), "bad type for step_search diagnostic missing")) return rc;

    const auto badProtocol = parseWorkflowScript("protocol.rsx", "info { in: \"a\" protocol: banana }\n");
    if (int rc = require(badProtocol.ok(), "bad protocol script should parse")) return rc;
    const auto badProtocolSemantic = validateWorkflowProgram(badProtocol.program);
    if (int rc = require(hasMessage(badProtocolSemantic, "invalid protocol value"), "invalid protocol diagnostic missing")) return rc;

    const auto badBuiltin = parseWorkflowScript("builtin.rsx", "if exists([\"a\"]) { version {} }\n");
    if (int rc = require(badBuiltin.ok(), "bad builtin script should parse")) return rc;
    const auto badBuiltinSemantic = validateWorkflowProgram(badBuiltin.program);
    if (int rc = require(hasMessage(badBuiltinSemantic, "exists() argument must be string-like"), "bad builtin diagnostic missing")) return rc;

    const auto unknownVariable = parseWorkflowScript("var.rsx", "info { in: input }\n");
    if (int rc = require(unknownVariable.ok(), "unknown-variable script should parse")) return rc;
    const auto unknownVariableSemantic = validateWorkflowProgram(unknownVariable.program);
    if (int rc = require(hasMessage(unknownVariableSemantic, "unknown variable: input"), "unknown variable diagnostic missing")) return rc;

    const auto badLoop = parseWorkflowScript("loop.rsx", "for x in exists(\"a\") { version {} }\n");
    if (int rc = require(badLoop.ok(), "bad-loop script should parse")) return rc;
    const auto badLoopSemantic = validateWorkflowProgram(badLoop.program);
    if (int rc = require(hasMessage(badLoopSemantic, "for loop iterable must be a list or glob(...) result"), "bad loop iterable diagnostic missing")) return rc;

    std::cout << "[PASS] dsl_semantic_test\n";
    return 0;
}
