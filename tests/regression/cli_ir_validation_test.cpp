#include "apps/cli/cli_ir.hpp"
#include "apps/cli/cli_ir_validation.hpp"

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
}

int main() {
    using namespace roadscript::cli;

    CommandIR missingInput;
    missingInput.kind = CommandKind::Info;
    if (int rc = require(
            validateCommandRequirements(missingInput, "rse") == 1,
            "info should require an input path")) {
        return rc;
    }

    CommandIR missingMessage;
    missingMessage.kind = CommandKind::Embed;
    missingMessage.options.inputPath = "in.png";
    missingMessage.options.outputPath = "out.png";
    if (int rc = require(
            validateCommandRequirements(missingMessage, "rse") == 1,
            "embed should require a message")) {
        return rc;
    }

    CommandIR conflict;
    conflict.kind = CommandKind::Verify;
    conflict.options.inputPath = "in.png";
    conflict.options.key = "secret";
    conflict.options.keyEnvName = "RS_KEY";
    if (int rc = require(
            resolveCommandKeySource(conflict) == 1,
            "conflicting key sources should be rejected")) {
        return rc;
    }

    CommandIR valid;
    valid.kind = CommandKind::Extract;
    valid.options.inputPath = "in.png";
    valid.options.layout = LayoutMode::Auto;
    if (int rc = require(
            validateCommandRequirements(valid, "rse") == 0,
            "extract with an input path should validate")) {
        return rc;
    }

    std::cout << "[PASS] cli_ir_validation_test\n";
    return 0;
}
