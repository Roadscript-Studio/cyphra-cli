#include "apps/cli/cli_dsl_ast.hpp"
#include "apps/cli/cli_dsl_parser.hpp"
#include "apps/cli/cli_print.hpp"

#include <iostream>
#include <memory>
#include <sstream>
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
    using namespace roadscript::cli::dsl;

    const std::string script =
        "# workflow comment\n"
        "let input = \"tests/fixtures/input/input.jpg\"\n"
        "let outputs = [\"a.png\", \"b.png\"]\n"
        "// glob loop comment\n"
        "for file in glob(\"photos/*.jpg\") {\n"
        "  info { in: file protocol: classic }\n"
        "}\n"
        "for out_file in outputs {\n"
        "  if not exists(out_file) {\n"
        "    embed {\n"
        "      in: input\n"
        "      out: out_file\n"
        "      msg_block: \"hello\"\n"
        "      protocol: classic\n"
        "      step: 30.0\n"
        "      layout: center_ring\n"
        "    }\n"
        "  }\n"
        "}\n"
        "config show {}\n";

    const auto parsed = parseWorkflowScript("workflow.rsx", script);
    if (int rc = require(parsed.ok(), "expected parser success for representative workflow")) return rc;
    if (int rc = require(parsed.program.statements.size() == 5u, "expected five top-level statements")) return rc;

    if (int rc = require(dynamic_cast<LetStatement *>(parsed.program.statements[0].get()) != nullptr, "first statement should be let")) return rc;
    if (int rc = require(dynamic_cast<LetStatement *>(parsed.program.statements[1].get()) != nullptr, "second statement should be let")) return rc;

    const auto *globForStatement = dynamic_cast<ForStatement *>(parsed.program.statements[2].get());
    if (int rc = require(globForStatement != nullptr, "third statement should be for")) return rc;
    if (int rc = require(globForStatement->body.size() == 1u, "glob loop should contain one statement")) return rc;
    if (int rc = require(dynamic_cast<CommandStatement *>(globForStatement->body[0].get()) != nullptr, "glob loop should contain command")) return rc;

    const auto *forStatement = dynamic_cast<ForStatement *>(parsed.program.statements[3].get());
    if (int rc = require(forStatement != nullptr, "fourth statement should be for")) return rc;
    if (int rc = require(forStatement->body.size() == 1u, "for body should contain one statement")) return rc;

    const auto *ifStatement = dynamic_cast<IfStatement *>(forStatement->body[0].get());
    if (int rc = require(ifStatement != nullptr, "for body should contain if")) return rc;
    if (int rc = require(ifStatement->thenStatements.size() == 1u, "if body should contain one statement")) return rc;

    const auto *embedCommand = dynamic_cast<CommandStatement *>(ifStatement->thenStatements[0].get());
    if (int rc = require(embedCommand != nullptr, "if body should contain embed command")) return rc;
    if (int rc = require(joinCommandName(embedCommand->nameParts) == "embed", "embed command name mismatch")) return rc;
    if (int rc = require(embedCommand->fields.size() == 6u, "embed command should have six fields")) return rc;

    const auto *configCommand = dynamic_cast<CommandStatement *>(parsed.program.statements[4].get());
    if (int rc = require(configCommand != nullptr, "fifth statement should be config command")) return rc;
    if (int rc = require(joinCommandName(configCommand->nameParts) == "config show", "config command name mismatch")) return rc;

    const auto invalid = parseWorkflowScript(
        "invalid.rsx",
        "embed {\n  in \"missing colon\"\n}\n"
    );
    if (int rc = require(!invalid.ok(), "invalid script should fail to parse")) return rc;
    if (int rc = require(!invalid.diagnostics.empty(), "invalid script should report diagnostics")) return rc;
    if (int rc = require(invalid.diagnostics.front().span.begin.line == 2, "expected diagnostic on line 2")) return rc;
    if (int rc = require(invalid.diagnostics.front().span.begin.column == 6, "expected diagnostic column on missing colon")) return rc;
    std::ostringstream diagnosticOut;
    roadscript::cli::printDiagnostic(diagnosticOut, invalid.diagnostics.front());
    if (int rc = require(
            diagnosticOut.str().find("invalid.rsx:2:6:") != std::string::npos,
            "formatted diagnostic should include file:line:column")) {
        return rc;
    }

    std::cout << "[PASS] dsl_parser_test\n";
    return 0;
}
