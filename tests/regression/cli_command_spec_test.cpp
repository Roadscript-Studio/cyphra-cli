#include "apps/cli/cli_command_spec.hpp"

#include <iostream>
#include <set>
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

    std::set<CommandKind> commands;
    for (const auto &spec : allCommandSpecs()) {
        commands.insert(spec.kind);
        if (int rc = require(!spec.canonicalName.empty(), "command spec missing canonical name")) return rc;
        if (int rc = require(!spec.displayLabel.empty(), "command spec missing display label")) return rc;
    }

    if (int rc = require(commands.size() == 7u, "expected seven command specs")) return rc;
    if (int rc = require(commandKindFromCanonicalName("version") == CommandKind::Version, "missing version command lookup")) return rc;
    if (int rc = require(commandKindFromCanonicalName("doctor") == CommandKind::Doctor, "missing doctor command lookup")) return rc;
    if (int rc = require(commandKindFromCanonicalName("config show") == CommandKind::ConfigShow, "missing config show command lookup")) return rc;
    if (int rc = require(commandKindFromCanonicalName("info") == CommandKind::Info, "missing info command lookup")) return rc;
    if (int rc = require(commandKindFromCanonicalName("embed") == CommandKind::Embed, "missing embed command lookup")) return rc;
    if (int rc = require(commandKindFromCanonicalName("extract") == CommandKind::Extract, "missing extract command lookup")) return rc;
    if (int rc = require(commandKindFromCanonicalName("verify") == CommandKind::Verify, "missing verify command lookup")) return rc;

    std::set<CommandField> fields;
    for (const auto &fieldSpec : allCommandFieldSpecs()) {
        fields.insert(fieldSpec.field);
        if (int rc = require(!fieldSpec.dslName.empty(), "field spec missing DSL name")) return rc;
    }
    if (int rc = require(fields.size() == 17u, "expected seventeen field specs")) return rc;
    if (int rc = require(parseDslFieldName("in") == CommandField::In, "missing in field")) return rc;
    if (int rc = require(parseDslFieldName("out") == CommandField::Out, "missing out field")) return rc;
    if (int rc = require(parseDslFieldName("msg_block") == CommandField::MsgBlock, "missing msg_block field")) return rc;
    if (int rc = require(parseDslFieldName("debug_json") == CommandField::DebugJson, "missing debug_json field")) return rc;
    if (int rc = require(parseDslFieldName("max_message_bytes") == CommandField::MaxMessageBytes, "missing max_message_bytes field")) return rc;

    if (int rc = require(commandSpec(CommandKind::Info).defaultLayout == LayoutMode::CenterRing, "info default layout mismatch")) return rc;
    if (int rc = require(commandAllowsField(CommandKind::Info, CommandField::MaxMessageBytes), "info should allow max_message_bytes")) return rc;
    if (int rc = require(!commandAllowsField(CommandKind::Verify, CommandField::MaxMessageBytes), "verify should not allow max_message_bytes")) return rc;
    if (int rc = require(commandRequiresField(CommandKind::Embed, CommandField::MsgBlock), "embed should require msg_block")) return rc;
    if (int rc = require(commandSpec(CommandKind::Verify).defaultLayout == LayoutMode::Auto, "verify default layout mismatch")) return rc;

    std::cout << "[PASS] cli_command_spec_test\n";
    return 0;
}
