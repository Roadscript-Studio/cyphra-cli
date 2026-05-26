#pragma once

#include "cli_ir.hpp"
#include "cli_types.hpp"

#include <opencv2/core.hpp>

#include <ostream>
#include <string>
#include <string_view>

namespace roadscript::cli {
    struct ParseCommandIRResult {
        CommandIR command;
        bool parseSucceeded = false;
        int exitCode = 0;
    };

    struct RunCommandOptions {
        std::string scriptPath;
        bool dryRun = false;
        ColorMode colorMode = ColorMode::Auto;
    };

    struct ParseRunCommandResult {
        RunCommandOptions options;
        bool parseSucceeded = false;
        int exitCode = 0;
    };

    // Parsing stays in the CLI so the engine remains focused on watermark
    // logic rather than command-line concerns.
    std::string protocolToString(roadscript::watermark::Protocol protocol);
    std::string layoutToString(LayoutMode layout);
    bool usesKeyedShuffle(LayoutMode layout);
    bool hasJsonFlag(int argc, char **argv, int startIndex);
    void printCommandUsage(std::ostream &out, CommandKind kind, std::string_view argv0);
    void printGeneralUsage(std::ostream &out, std::string_view argv0);

    ParseCommandIRResult parseCommandLineToIR(int argc, char **argv, std::string_view argv0);
    ParseRunCommandResult parseRunCommandArgs(int argc, char **argv, std::string_view argv0);

    int parseCommonOptions(
        int argc,
        char **argv,
        int startIndex,
        CommonOptions &options,
        bool allowOutput,
        bool allowMessage,
        bool allowAutoLayout,
        bool allowStepSearch,
        bool allowDebugJson,
        bool allowDebugSvg,
        std::string_view argv0
    );
} // namespace roadscript::cli
