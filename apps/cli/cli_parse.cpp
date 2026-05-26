#include "cli_parse.hpp"

#include "cli_print.hpp"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace roadscript::cli {
    namespace {
        bool isHelpFlag(std::string_view arg) {
            return arg == "--help" || arg == "-h";
        }

        std::optional<ColorMode> parseColorModeValue(const std::string &value) {
            if (value == "auto") {
                return ColorMode::Auto;
            }
            if (value == "always") {
                return ColorMode::Always;
            }
            if (value == "never") {
                return ColorMode::Never;
            }
            return std::nullopt;
        }

        void printHelpHeading(std::ostream &out, std::string_view title) {
            printSectionHeader(out, title);
        }

        void printExampleCommand(std::ostream &out, std::string_view commandLine) {
            printExampleLine(out, commandLine);
        }

        void printCommonJsonNote(std::ostream &out) {
            printNoteLine(out, "Add --json for machine-readable output.");
        }

        void printCommonColorNote(std::ostream &out) {
            printNoteLine(out, "Use --color auto|always|never to control ANSI styling in human output.");
        }

        std::optional<LayoutMode> parseLayout(const std::string &value) {
            if (value == "center-ring") {
                return LayoutMode::CenterRing;
            }
            if (value == "keyed-shuffle") {
                return LayoutMode::KeyedShuffle;
            }
            if (value == "auto") {
                return LayoutMode::Auto;
            }
            return std::nullopt;
        }

        // CLI surfaces architecture families with canonical names while
        // temporarily accepting legacy aliases for compatibility.
        std::optional<roadscript::watermark::Protocol> parseProtocol(const std::string &value) {
            if (value == "classic" || value == "v1") {
                return roadscript::watermark::Protocol::Classic;
            }
            if (value == "mosaic" || value == "mosaic-v2") {
                return roadscript::watermark::Protocol::Mosaic;
            }
            return std::nullopt;
        }

        bool parseFloat(const std::string &value, float &out) {
            char *end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (end == value.c_str() || (end != nullptr && *end != '\0')) {
                return false;
            }
            if (!std::isfinite(parsed)) {
                return false;
            }
            out = parsed;
            return true;
        }

        bool parsePositiveInteger(const std::string &value, long long &out) {
            char *end = nullptr;
            errno = 0;
            const long long parsed = std::strtoll(value.c_str(), &end, 10);
            if (end == value.c_str() || (end != nullptr && *end != '\0') || errno == ERANGE || parsed <= 0) {
                return false;
            }
            out = parsed;
            return true;
        }

        std::string protocolTryLine(CommandKind kind, std::string_view argv0) {
            switch (kind) {
                case CommandKind::Info:
                    return std::string(argv0) + " info --in input.jpg --protocol classic";
                case CommandKind::Embed:
                    return std::string(argv0) +
                        " embed --in input.jpg --out output.png --msg-block \"hello\" --protocol classic";
                case CommandKind::Extract:
                    return std::string(argv0) + " extract --in output.png --protocol classic";
                case CommandKind::Verify:
                    return std::string(argv0) + " verify --in output.png --protocol classic";
                case CommandKind::Version:
                case CommandKind::Doctor:
                case CommandKind::ConfigShow:
                    break;
            }
            return std::string(argv0) + " --help";
        }

        void printRunUsage(std::ostream &out, std::string_view argv0) {
            printTitle(out, "Run Workflow");
            printHelpHeading(out, "USAGE");
            out
                << "  " << argv0 << " run <script.rsx>\n"
                << "  " << argv0 << " run <script.rsx> --dry-run\n"
                << "\n";
            printHelpHeading(out, "DESCRIPTION");
            printKeyValue(out, "Purpose", "Run a Roadscript workflow file");
            out << "\n";
            printHelpHeading(out, "REQUIRED OPTIONS");
            printKeyValue(out, "<script.rsx>", "Workflow script to execute");
            out << "\n";
            printHelpHeading(out, "OPTIONAL FLAGS");
            printKeyValue(out, "--dry-run", "Plan workflow steps without writing files");
            printKeyValue(out, "--color", "auto, always, never");
            out << "\n";
            printHelpHeading(out, "EXAMPLES");
            printExampleCommand(out, std::string(argv0) + " run examples/classic_roundtrip.rsx --dry-run");
            printExampleCommand(out, std::string(argv0) + " run examples/mosaic_debug.rsx");
            out << "\n";
            printHelpHeading(out, "JSON OUTPUT");
            printNoteLine(out, "Workflow JSON mode is not supported yet.");
            printNoteLine(out, "Use --dry-run before batch or glob workflows.");
            printCommonColorNote(out);
        }

        void printConfigUsage(std::ostream &out, std::string_view argv0) {
            printTitle(out, "Manage Configuration");
            printHelpHeading(out, "USAGE");
            out
                << "  " << argv0 << " config show [--color <mode>]\n\n";
            printHelpHeading(out, "DESCRIPTION");
            printKeyValue(out, "Purpose", "Inspect current Roadscript CLI defaults and configuration source");
            out << "\n";
            printHelpHeading(out, "COMMANDS");
            printKeyValue(out, "show", "Display the current configuration screen");
            out << "\n";
            printHelpHeading(out, "OPTIONAL FLAGS");
            printKeyValue(out, "--color", "auto, always, never");
            out << "\n";
            printHelpHeading(out, "EXAMPLES");
            printExampleCommand(out, std::string(argv0) + " config show");
            printExampleCommand(out, std::string(argv0) + " config show --color always");
            out << "\n";
            printHelpHeading(out, "JSON OUTPUT");
            printNoteLine(out, "Configuration JSON mode is not supported yet.");
            printNoteLine(out, "Persistent writable configuration commands may be added in a future release.");
            printCommonColorNote(out);
        }

        void printConfigShowUsage(std::ostream &out, std::string_view argv0) {
            printTitle(out, "Show Configuration");
            printHelpHeading(out, "USAGE");
            out
                << "  " << argv0 << " config show [--color <mode>]\n\n";
            printHelpHeading(out, "DESCRIPTION");
            printKeyValue(out, "Purpose", "Show current Roadscript CLI defaults and where they come from");
            out << "\n";
            printHelpHeading(out, "OPTIONAL FLAGS");
            printKeyValue(out, "--color", "auto, always, never");
            out << "\n";
            printHelpHeading(out, "CURRENT DEFAULTS");
            printKeyValue(out, "Protocol", "classic");
            printKeyValue(out, "Step", "30.00");
            printKeyValue(out, "Layout", "center-ring");
            printKeyValue(out, "Color", "auto");
            out << "\n";
            printHelpHeading(out, "EXAMPLES");
            printExampleCommand(out, std::string(argv0) + " config show");
            printExampleCommand(out, std::string(argv0) + " config show --color never");
            out << "\n";
            printHelpHeading(out, "JSON OUTPUT");
            printNoteLine(out, "Configuration JSON mode is not supported yet.");
            printNoteLine(out, "Future planned behavior: config-set color defaults, with --color overriding config.");
            printCommonColorNote(out);
        }

    } // namespace

    std::string protocolToString(roadscript::watermark::Protocol protocol) {
        return roadscript::watermark::protocolName(protocol);
    }

    std::string layoutToString(LayoutMode layout) {
        switch (layout) {
            case LayoutMode::CenterRing:
                return "center-ring";
            case LayoutMode::KeyedShuffle:
                return "keyed-shuffle";
            case LayoutMode::Auto:
                return "auto";
        }
        return "unknown";
    }

    bool usesKeyedShuffle(LayoutMode layout) {
        return layout == LayoutMode::KeyedShuffle;
    }

    bool hasJsonFlag(int argc, char **argv, int startIndex) {
        for (int i = startIndex; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--json") {
                return true;
            }
        }
        return false;
    }

    void printCommandUsage(std::ostream &out, CommandKind kind, std::string_view argv0) {
        switch (kind) {
            case CommandKind::Info:
                printTitle(out, "Inspect Image Capacity");
                printHelpHeading(out, "USAGE");
                out
                    << "  " << argv0
                    << " info --in <image> [--msg-block <text>] [options]\n\n";
                printHelpHeading(out, "DESCRIPTION");
                printKeyValue(out, "Purpose", "Inspect image dimensions, capacity, and support");
                out << "\n";
                printHelpHeading(out, "REQUIRED OPTIONS");
                printKeyValue(out, "--in", "Source image to inspect");
                out << "\n";
                printHelpHeading(out, "OPTIONAL FLAGS");
                printKeyValue(out, "--msg-block", "Check whether a payload fits");
                printKeyValue(out, "--protocol", "classic (default), mosaic (experimental)");
                printKeyValue(out, "--layout", "center-ring, keyed-shuffle");
                printKeyValue(out, "--step", "Embedding step size");
                printKeyValue(out, "--max-pixels", "Guardrail for image pixel count");
                printKeyValue(out, "--max-width", "Guardrail for image width");
                printKeyValue(out, "--max-height", "Guardrail for image height");
                printKeyValue(out, "--max-message-bytes", "Guardrail for message size");
                printKeyValue(out, "--color", "auto, always, never");
                out << "\n";
                printHelpHeading(out, "KEY SOURCES");
                printKeyValue(out, "--key", "Inline key value");
                printKeyValue(out, "--key-file", "Read key from a file");
                printKeyValue(out, "--key-env", "Read key from an environment variable");
                out << "\n";
                printHelpHeading(out, "EXAMPLES");
                printExampleCommand(out, std::string(argv0) + " info --in input.jpg");
                printExampleCommand(out, std::string(argv0) + " info --in input.jpg --msg-block \"hello\" --protocol classic");
                printExampleCommand(out, std::string(argv0) + " info --in poster.png --protocol mosaic");
                out << "\n";
                printHelpHeading(out, "JSON OUTPUT");
                printCommonJsonNote(out);
                printCommonColorNote(out);
                return;
            case CommandKind::Embed:
                printTitle(out, "Embed Watermark");
                printHelpHeading(out, "USAGE");
                out
                    << "  " << argv0
                    << " embed --in <image> --out <image> --msg-block <text> [options]\n\n";
                printHelpHeading(out, "DESCRIPTION");
                printKeyValue(out, "Purpose", "Embed a payload into an image");
                out << "\n";
                printHelpHeading(out, "REQUIRED OPTIONS");
                printKeyValue(out, "--in", "Source image");
                printKeyValue(out, "--out", "Destination image");
                printKeyValue(out, "--msg-block", "Payload text to embed");
                out << "\n";
                printHelpHeading(out, "OPTIONAL FLAGS");
                printKeyValue(out, "--protocol", "classic (default), mosaic (experimental)");
                printKeyValue(out, "--layout", "center-ring, keyed-shuffle");
                printKeyValue(out, "--step", "Embedding step size");
                printKeyValue(out, "--max-pixels", "Guardrail for image pixel count");
                printKeyValue(out, "--max-width", "Guardrail for image width");
                printKeyValue(out, "--max-height", "Guardrail for image height");
                printKeyValue(out, "--max-message-bytes", "Guardrail for message size");
                printKeyValue(out, "--color", "auto, always, never");
                out << "\n";
                printHelpHeading(out, "KEY SOURCES");
                printKeyValue(out, "--key", "Inline key value");
                printKeyValue(out, "--key-file", "Read key from a file");
                printKeyValue(out, "--key-env", "Read key from an environment variable");
                out << "\n";
                printHelpHeading(out, "EXAMPLES");
                printExampleCommand(out, std::string(argv0) + " embed --protocol classic --in input.jpg --out output.png --msg-block \"hello\"");
                printExampleCommand(out, std::string(argv0) + " embed --protocol mosaic --in poster.png --out mosaic.png --msg-block \"hello\"");
                out << "\n";
                printHelpHeading(out, "JSON OUTPUT");
                printNoteLine(out, "Use --protocol mosaic only with supported large images.");
                printCommonJsonNote(out);
                printCommonColorNote(out);
                return;
            case CommandKind::Extract:
                printTitle(out, "Recover Payload");
                printHelpHeading(out, "USAGE");
                out
                    << "  " << argv0
                    << " extract --in <image> [options]\n\n";
                printHelpHeading(out, "DESCRIPTION");
                printKeyValue(out, "Purpose", "Recover a payload from an image");
                out << "\n";
                printHelpHeading(out, "REQUIRED OPTIONS");
                printKeyValue(out, "--in", "Image containing an embedded payload");
                out << "\n";
                printHelpHeading(out, "OPTIONAL FLAGS");
                printKeyValue(out, "--protocol", "classic (default), mosaic (experimental)");
                printKeyValue(out, "--layout", "center-ring, keyed-shuffle, auto");
                printKeyValue(out, "--step", "Expected embedding step size");
                printKeyValue(out, "--step-search", "Try neighboring step values");
                printKeyValue(out, "--max-pixels", "Guardrail for image pixel count");
                printKeyValue(out, "--max-width", "Guardrail for image width");
                printKeyValue(out, "--max-height", "Guardrail for image height");
                printKeyValue(out, "--color", "auto, always, never");
                out << "\n";
                printHelpHeading(out, "KEY SOURCES");
                printKeyValue(out, "--key", "Inline key value");
                printKeyValue(out, "--key-file", "Read key from a file");
                printKeyValue(out, "--key-env", "Read key from an environment variable");
                out << "\n";
                printHelpHeading(out, "DEBUG OPTIONS");
                printKeyValue(out, "--debug-json", "Write Mosaic debug JSON");
                printKeyValue(out, "--debug-svg", "Write Mosaic debug SVG");
                out << "\n";
                printHelpHeading(out, "EXAMPLES");
                printExampleCommand(out, std::string(argv0) + " extract --protocol classic --in output.png");
                printExampleCommand(out, std::string(argv0) + " extract --protocol mosaic --in mosaic.png --debug-json debug.json --debug-svg debug.svg");
                out << "\n";
                printHelpHeading(out, "JSON OUTPUT");
                printCommonJsonNote(out);
                printCommonColorNote(out);
                return;
            case CommandKind::Verify:
                printTitle(out, "Verify Payload");
                printHelpHeading(out, "USAGE");
                out
                    << "  " << argv0
                    << " verify --in <image> [options]\n\n";
                printHelpHeading(out, "DESCRIPTION");
                printKeyValue(out, "Purpose", "Check whether an embedded payload is valid");
                out << "\n";
                printHelpHeading(out, "REQUIRED OPTIONS");
                printKeyValue(out, "--in", "Image to verify");
                out << "\n";
                printHelpHeading(out, "OPTIONAL FLAGS");
                printKeyValue(out, "--protocol", "classic (default), mosaic (experimental)");
                printKeyValue(out, "--layout", "center-ring, keyed-shuffle, auto");
                printKeyValue(out, "--step", "Expected embedding step size");
                printKeyValue(out, "--step-search", "Try neighboring step values");
                printKeyValue(out, "--max-pixels", "Guardrail for image pixel count");
                printKeyValue(out, "--max-width", "Guardrail for image width");
                printKeyValue(out, "--max-height", "Guardrail for image height");
                printKeyValue(out, "--color", "auto, always, never");
                out << "\n";
                printHelpHeading(out, "KEY SOURCES");
                printKeyValue(out, "--key", "Inline key value");
                printKeyValue(out, "--key-file", "Read key from a file");
                printKeyValue(out, "--key-env", "Read key from an environment variable");
                out << "\n";
                printHelpHeading(out, "DEBUG OPTIONS");
                printKeyValue(out, "--debug-json", "Write Mosaic debug JSON");
                printKeyValue(out, "--debug-svg", "Write Mosaic debug SVG");
                out << "\n";
                printHelpHeading(out, "EXAMPLES");
                printExampleCommand(out, std::string(argv0) + " verify --protocol classic --in output.png");
                printExampleCommand(out, std::string(argv0) + " verify --protocol mosaic --in mosaic.png --debug-json debug.json --debug-svg debug.svg");
                out << "\n";
                printHelpHeading(out, "JSON OUTPUT");
                printCommonJsonNote(out);
                printCommonColorNote(out);
                return;
            case CommandKind::Version:
            case CommandKind::Doctor:
            case CommandKind::ConfigShow:
                if (kind == CommandKind::ConfigShow) {
                    printConfigShowUsage(out, argv0);
                    return;
                }
                break;
        }
    }

    void printGeneralUsage(std::ostream &out, std::string_view argv0) {
        printTitle(out, "Roadscript CLI");
        printHelpHeading(out, "USAGE");
        out << "  " << argv0 << " <command> [options]\n\n";
        printHelpHeading(out, "DESCRIPTION");
        printKeyValue(out, "Purpose", "Embed, inspect, recover, verify, and automate Roadscript workflows");
        out << "\n";
        printHelpHeading(out, "COMMANDS");
        printKeyValue(out, "version", "Show build and engine version details");
        printKeyValue(out, "doctor", "Check CLI and engine linkage");
        printKeyValue(out, "config show", "Show the current Roadscript configuration");
        printKeyValue(out, "info", "Inspect image capacity and support");
        printKeyValue(out, "embed", "Embed a payload into an image");
        printKeyValue(out, "extract", "Recover a payload from an image");
        printKeyValue(out, "verify", "Verify an embedded payload");
        printKeyValue(out, "run", "Run a Roadscript workflow file");
        out << "\n";
        printHelpHeading(out, "OPTIONAL FLAGS");
        printKeyValue(out, "--help", "Show general or command-specific help");
        printKeyValue(out, "--color", "auto, always, never");
        out << "\n";
        printHelpHeading(out, "EXAMPLES");
        printExampleCommand(out, std::string(argv0) + " embed --protocol classic --in input.jpg --out output.png --msg-block \"hello\"");
        printExampleCommand(out, std::string(argv0) + " verify --protocol classic --in output.png");
        printExampleCommand(out, std::string(argv0) + " run examples/classic_roundtrip.rsx --dry-run");
        out << "\n";
        printHelpHeading(out, "JSON OUTPUT");
        printNoteLine(out, "Use <command> --help for command-specific usage.");
        printCommonJsonNote(out);
        printCommonColorNote(out);
    }

    int parseCommonOptions(
        int argc,
        char **argv,
        int startIndex,
        CommandKind kind,
        CommonOptions &options,
        bool allowOutput,
        bool allowMessage,
        bool allowAutoLayout,
        bool allowStepSearch,
        bool allowDebugJson,
        bool allowDebugSvg,
        std::string_view argv0
    ) {
        for (int i = startIndex; i < argc; ++i) {
            const std::string arg = argv[i];

            auto requireValue = [&](const char *flag) -> std::optional<std::string> {
                if (i + 1 >= argc) {
                    if (options.json) {
                        printJsonError(std::cout, std::string(flag) + " requires a value.");
                    } else {
                        printHumanError(std::cerr, std::string(flag) + " requires a value.");
                    }
                    return std::nullopt;
                }
                return std::string(argv[++i]);
            };

            if (arg == "--json") {
                options.json = true;
            } else if (arg == "--in") {
                auto value = requireValue("--in");
                if (!value) return 1;
                options.inputPath = *value;
            } else if (arg == "--out") {
                if (!allowOutput) {
                    if (options.json) {
                        printJsonError(std::cout, "--out is not supported for this command.");
                    } else {
                        printHumanError(std::cerr, "--out is not supported for this command.");
                    }
                    return 1;
                }
                auto value = requireValue("--out");
                if (!value) return 1;
                options.outputPath = *value;
            } else if (arg == "--debug-json") {
                if (!allowDebugJson) {
                    if (options.json) {
                        printJsonError(std::cout, "--debug-json is not supported for this command.");
                    } else {
                        printHumanError(std::cerr, "--debug-json is not supported for this command.");
                    }
                    return 1;
                }
                auto value = requireValue("--debug-json");
                if (!value) return 1;
                options.debugJsonPath = *value;
            } else if (arg == "--debug-svg") {
                if (!allowDebugSvg) {
                    if (options.json) {
                        printJsonError(std::cout, "--debug-svg is not supported for this command.");
                    } else {
                        printHumanError(std::cerr, "--debug-svg is not supported for this command.");
                    }
                    return 1;
                }
                auto value = requireValue("--debug-svg");
                if (!value) return 1;
                options.debugSvgPath = *value;
            } else if (arg == "--msg-block") {
                if (!allowMessage) {
                    if (options.json) {
                        printJsonError(std::cout, "--msg-block is not supported for this command.");
                    } else {
                        printHumanError(std::cerr, "--msg-block is not supported for this command.");
                    }
                    return 1;
                }
                auto value = requireValue("--msg-block");
                if (!value) return 1;
                options.message = *value;
                options.hasMessage = true;
            } else if (arg == "--key") {
                auto value = requireValue("--key");
                if (!value) return 1;
                options.key = *value;
            } else if (arg == "--key-file") {
                auto value = requireValue("--key-file");
                if (!value) return 1;
                options.keyFilePath = *value;
            } else if (arg == "--key-env") {
                auto value = requireValue("--key-env");
                if (!value) return 1;
                options.keyEnvName = *value;
            } else if (arg == "--color") {
                auto value = requireValue("--color");
                if (!value) return 1;
                auto colorMode = parseColorModeValue(*value);
                if (!colorMode) {
                    if (options.json) {
                        printJsonError(std::cout, "invalid --color value: " + *value);
                    } else {
                        printHumanError(std::cerr, "invalid --color value: " + *value);
                        printNoteLine(std::cerr, "Supported color modes: auto, always, never.");
                    }
                    return 1;
                }
                options.colorMode = *colorMode;
            } else if (arg == "--max-pixels") {
                auto value = requireValue("--max-pixels");
                if (!value) return 1;
                long long parsed = 0;
                if (!parsePositiveInteger(*value, parsed)) {
                    if (options.json) {
                        printJsonError(std::cout, "invalid --max-pixels value: " + *value);
                    } else {
                        printHumanError(std::cerr, "invalid --max-pixels value: " + *value);
                        printNoteLine(std::cerr, "--max-pixels must be a positive integer.");
                    }
                    return 1;
                }
                options.maxPixels = parsed;
            } else if (arg == "--max-width") {
                auto value = requireValue("--max-width");
                if (!value) return 1;
                long long parsed = 0;
                if (!parsePositiveInteger(*value, parsed)) {
                    if (options.json) {
                        printJsonError(std::cout, "invalid --max-width value: " + *value);
                    } else {
                        printHumanError(std::cerr, "invalid --max-width value: " + *value);
                        printNoteLine(std::cerr, "--max-width must be a positive integer.");
                    }
                    return 1;
                }
                options.maxWidth = parsed;
            } else if (arg == "--max-height") {
                auto value = requireValue("--max-height");
                if (!value) return 1;
                long long parsed = 0;
                if (!parsePositiveInteger(*value, parsed)) {
                    if (options.json) {
                        printJsonError(std::cout, "invalid --max-height value: " + *value);
                    } else {
                        printHumanError(std::cerr, "invalid --max-height value: " + *value);
                        printNoteLine(std::cerr, "--max-height must be a positive integer.");
                    }
                    return 1;
                }
                options.maxHeight = parsed;
            } else if (arg == "--max-message-bytes") {
                auto value = requireValue("--max-message-bytes");
                if (!value) return 1;
                long long parsed = 0;
                if (!parsePositiveInteger(*value, parsed)) {
                    if (options.json) {
                        printJsonError(std::cout, "invalid --max-message-bytes value: " + *value);
                    } else {
                        printHumanError(std::cerr, "invalid --max-message-bytes value: " + *value);
                        printNoteLine(std::cerr, "--max-message-bytes must be a positive integer.");
                    }
                    return 1;
                }
                options.maxMessageBytes = parsed;
            } else if (arg == "--protocol") {
                auto value = requireValue("--protocol");
                if (!value) return 1;
                auto protocol = parseProtocol(*value);
                if (!protocol) {
                    if (options.json) {
                        printJsonError(std::cout, "unsupported protocol: " + *value);
                    } else {
                        printHumanError(std::cerr, "invalid protocol: " + *value);
                        printNoteLine(std::cerr, "Supported protocols: classic, mosaic.");
                        printTryLine(std::cerr, protocolTryLine(kind, argv0));
                    }
                    return 1;
                }
                options.protocol = *protocol;
            } else if (arg == "--step") {
                auto value = requireValue("--step");
                if (!value) return 1;
                if (!parseFloat(*value, options.step) || !(options.step > 0.0f)) {
                    if (options.json) {
                        printJsonError(std::cout, "invalid --step value: " + *value);
                    } else {
                        printHumanError(std::cerr, "invalid --step value: " + *value);
                        printNoteLine(std::cerr, "--step must be a positive number.");
                    }
                    return 1;
                }
            } else if (arg == "--step-search") {
                if (!allowStepSearch) {
                    if (options.json) {
                        printJsonError(std::cout, "--step-search is not supported for this command.");
                    } else {
                        printHumanError(std::cerr, "--step-search is not supported for this command.");
                    }
                    return 1;
                }
                options.enableStepSearch = true;
            } else if (arg == "--layout") {
                auto value = requireValue("--layout");
                if (!value) return 1;
                auto layout = parseLayout(*value);
                if (!layout) {
                    if (options.json) {
                        printJsonError(std::cout, "unsupported layout: " + *value);
                    } else {
                        printHumanError(std::cerr, "invalid layout: " + *value);
                        printNoteLine(
                            std::cerr,
                            allowAutoLayout
                                ? "Supported layouts: center-ring, keyed-shuffle, auto."
                                : "Supported layouts: center-ring, keyed-shuffle."
                        );
                    }
                    return 1;
                }
                if (!allowAutoLayout && *layout == LayoutMode::Auto) {
                    if (options.json) {
                        printJsonError(std::cout, "layout auto is not supported for this command.");
                    } else {
                        printHumanError(std::cerr, "layout auto is not supported for this command.");
                    }
                    return 1;
                }
                options.layout = *layout;
            } else {
                if (options.json) {
                    printJsonError(std::cout, "unknown flag: " + arg);
                } else {
                    printHumanError(std::cerr, "unknown flag: " + arg);
                    printGeneralUsage(std::cerr, argv0);
                }
                return 1;
            }
        }

        return 0;
    }

    ParseCommandIRResult parseCommandLineToIR(int argc, char **argv, std::string_view argv0) {
        ParseCommandIRResult result;
        if (argc < 2) {
            printGeneralUsage(std::cerr, argv0);
            result.exitCode = 1;
            return result;
        }

        const std::string command = argv[1];
        if (isHelpFlag(command) || command == "help") {
            printGeneralUsage(std::cout, argv0);
            result.exitCode = 0;
            return result;
        }
        if (command == "version" || command == "--version" || command == "-v") {
            result.command.kind = CommandKind::Version;
            result.parseSucceeded = true;
            return result;
        }

        if (command == "doctor" || command == "doc") {
            result.command.kind = CommandKind::Doctor;
            result.parseSucceeded = true;
            return result;
        }

        if (command == "config") {
            if (argc >= 3 && argv[2] != nullptr && isHelpFlag(argv[2])) {
                printConfigUsage(std::cout, argv0);
                result.exitCode = 0;
                return result;
            }
            if (argc < 3) {
                printHumanError(std::cerr, "missing config subcommand.");
                printTryLine(std::cerr, std::string(argv0) + " config show");
                printConfigUsage(std::cerr, argv0);
                result.exitCode = 1;
                return result;
            }

            const std::string subcommand = argv[2];
            if (subcommand == "show") {
                if (argc >= 4 && argv[3] != nullptr && isHelpFlag(argv[3])) {
                    printConfigShowUsage(std::cout, argv0);
                    result.exitCode = 0;
                    return result;
                }
                for (int i = 3; i < argc; ++i) {
                    const std::string arg = argv[i];
                    if (arg == "--color") {
                        if (i + 1 >= argc) {
                            printHumanError(std::cerr, "--color requires a value.");
                            printNoteLine(std::cerr, "Supported color modes: auto, always, never.");
                            result.exitCode = 1;
                            return result;
                        }
                        const std::string value = argv[++i];
                        auto colorMode = parseColorModeValue(value);
                        if (!colorMode) {
                            printHumanError(std::cerr, "invalid --color value: " + value);
                            printNoteLine(std::cerr, "Supported color modes: auto, always, never.");
                            result.exitCode = 1;
                            return result;
                        }
                        result.command.options.colorMode = *colorMode;
                        continue;
                    }
                    printHumanError(std::cerr, "unknown config show option: " + arg);
                    printConfigShowUsage(std::cerr, argv0);
                    result.exitCode = 1;
                    return result;
                }
                result.command.kind = CommandKind::ConfigShow;
                result.parseSucceeded = true;
                return result;
            }

            printHumanError(std::cerr, "unknown config subcommand: " + subcommand);
            printTryLine(std::cerr, std::string(argv0) + " config show");
            printConfigUsage(std::cerr, argv0);
            result.exitCode = 1;
            return result;
        }

        if (command == "info" || command == "embed" || command == "extract" || command == "verify") {
            if (command == "info") {
                result.command.kind = CommandKind::Info;
                result.command.options.layout = LayoutMode::CenterRing;
            } else if (command == "embed") {
                result.command.kind = CommandKind::Embed;
                result.command.options.layout = LayoutMode::CenterRing;
            } else if (command == "extract") {
                result.command.kind = CommandKind::Extract;
                result.command.options.layout = LayoutMode::Auto;
            } else {
                result.command.kind = CommandKind::Verify;
                result.command.options.layout = LayoutMode::Auto;
            }
            if (argc >= 3 && argv[2] != nullptr && isHelpFlag(argv[2])) {
                printCommandUsage(std::cout, result.command.kind, argv0);
                result.exitCode = 0;
                return result;
            }
            result.command.options.json = hasJsonFlag(argc, argv, 2);
            const int parseResult = parseCommonOptions(
                argc,
                argv,
                2,
                result.command.kind,
                result.command.options,
                commandAllowsOutput(result.command.kind),
                commandAllowsMessage(result.command.kind),
                commandAllowsAutoLayout(result.command.kind),
                commandAllowsStepSearch(result.command.kind),
                commandAllowsDebugJson(result.command.kind),
                commandAllowsDebugSvg(result.command.kind),
                argv0
            );
            if (parseResult != 0) {
                result.exitCode = parseResult;
                return result;
            }
            result.parseSucceeded = true;
            return result;
        }

        printHumanError(std::cerr, "unknown command: " + command);
        printNoteLine(std::cerr, "Use --help to list available commands.");
        printGeneralUsage(std::cerr, argv0);
        result.exitCode = 1;
        return result;
    }

    ParseRunCommandResult parseRunCommandArgs(int argc, char **argv, std::string_view argv0) {
        ParseRunCommandResult result;
        if (argc >= 3 && argv[2] != nullptr && isHelpFlag(argv[2])) {
            printRunUsage(std::cout, argv0);
            result.exitCode = 0;
            return result;
        }
        if (argc < 3) {
            printHumanError(std::cerr, "missing required workflow script path.");
            printTryLine(std::cerr, std::string(argv0) + " run workflow.rsx --dry-run");
            printRunUsage(std::cerr, argv0);
            result.exitCode = 1;
            return result;
        }

        bool sawScript = false;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--dry-run") {
                result.options.dryRun = true;
                continue;
            }
            if (arg == "--color") {
                if (i + 1 >= argc) {
                    printHumanError(std::cerr, "--color requires a value.");
                    printNoteLine(std::cerr, "Supported color modes: auto, always, never.");
                    result.exitCode = 1;
                    return result;
                }
                const std::string value = argv[++i];
                auto colorMode = parseColorModeValue(value);
                if (!colorMode) {
                    printHumanError(std::cerr, "invalid --color value: " + value);
                    printNoteLine(std::cerr, "Supported color modes: auto, always, never.");
                    result.exitCode = 1;
                    return result;
                }
                result.options.colorMode = *colorMode;
                continue;
            }
            if (!sawScript && !arg.empty() && arg.front() != '-') {
                result.options.scriptPath = arg;
                sawScript = true;
                continue;
            }

            if (arg.empty() || arg.front() == '-') {
                printHumanError(std::cerr, "unknown run option: " + arg);
                printNoteLine(std::cerr, "Supported run option: --dry-run.");
            } else {
                printHumanError(std::cerr, "unexpected extra argument: " + arg);
            }
            printRunUsage(std::cerr, argv0);
            result.exitCode = 1;
            return result;
        }

        if (!sawScript || result.options.scriptPath.empty()) {
            printHumanError(std::cerr, "missing required workflow script path.");
            printTryLine(std::cerr, std::string(argv0) + " run workflow.rsx --dry-run");
            printRunUsage(std::cerr, argv0);
            result.exitCode = 1;
            return result;
        }

        result.parseSucceeded = true;
        return result;
    }
} // namespace roadscript::cli
