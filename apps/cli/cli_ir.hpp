#pragma once

#include "cli_types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace roadscript::cli {
    enum class CommandKind {
        Version,
        Doctor,
        ConfigShow,
        Info,
        Embed,
        Extract,
        Verify,
    };

    struct CommandSourceSpan {
        std::string file;
        int line = 1;
        int column = 1;
    };

    // This is the normalized command shape shared by argv parsing today and
    // future DSL lowering later. It intentionally mirrors the current CLI
    // surface without adding execution-only workflow concepts yet.
    struct CommandIR {
        CommandKind kind = CommandKind::Version;
        CommonOptions options;
        std::optional<CommandSourceSpan> sourceSpan;
    };

    std::string_view commandKindName(CommandKind kind);
    bool commandAllowsOutput(CommandKind kind);
    bool commandAllowsMessage(CommandKind kind);
    bool commandAllowsAutoLayout(CommandKind kind);
    bool commandAllowsStepSearch(CommandKind kind);
    bool commandAllowsDebugJson(CommandKind kind);
    bool commandAllowsDebugSvg(CommandKind kind);
} // namespace roadscript::cli
