#include "cli_ir.hpp"

#include "cli_command_spec.hpp"

namespace roadscript::cli {
    std::string_view commandKindName(CommandKind kind) {
        return commandCanonicalName(kind);
    }

    bool commandAllowsOutput(CommandKind kind) {
        return commandSpec(kind).allowsOutput;
    }

    bool commandAllowsMessage(CommandKind kind) {
        return commandSpec(kind).allowsMessage;
    }

    bool commandAllowsAutoLayout(CommandKind kind) {
        return commandSpec(kind).allowsAutoLayout;
    }

    bool commandAllowsStepSearch(CommandKind kind) {
        return commandSpec(kind).allowsStepSearch;
    }

    bool commandAllowsDebugJson(CommandKind kind) {
        return commandSpec(kind).allowsDebugJson;
    }

    bool commandAllowsDebugSvg(CommandKind kind) {
        return commandSpec(kind).allowsDebugSvg;
    }
} // namespace roadscript::cli
