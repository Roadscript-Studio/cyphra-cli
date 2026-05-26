#include "cli_ir_validation.hpp"

#include "cli_parse.hpp"
#include "cli_print.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace roadscript::cli {
    namespace {
        void trimTrailingNewlines(std::string &value) {
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
                value.pop_back();
            }
        }

        int printCommandError(
            const CommandIR &command,
            std::string_view argv0,
            const std::string &reason,
            bool showUsage,
            std::string_view tryCommand = {}
        ) {
            if (command.options.json) {
                printJsonError(std::cout, reason);
            } else {
                printHumanError(std::cerr, reason);
                if (!tryCommand.empty()) {
                    printTryLine(std::cerr, tryCommand);
                }
                if (showUsage) {
                    printCommandUsage(std::cerr, command.kind, argv0);
                }
            }
            return 1;
        }
    } // namespace

    int resolveCommandKeySource(CommandIR &command) {
        CommonOptions &options = command.options;
        int sourceCount = 0;
        if (options.key.has_value()) {
            sourceCount++;
        }
        if (!options.keyFilePath.empty()) {
            sourceCount++;
        }
        if (!options.keyEnvName.empty()) {
            sourceCount++;
        }

        if (sourceCount > 1) {
            return printCommandError(
                command,
                "",
                "key source conflict: choose only one of --key, --key-file, or --key-env.",
                false
            );
        }

        if (!options.keyFilePath.empty()) {
            std::ifstream input(options.keyFilePath, std::ios::binary);
            if (!input) {
                return printCommandError(command, "", "failed to read key file: " + options.keyFilePath, false);
            }

            std::string keyValue(
                (std::istreambuf_iterator<char>(input)),
                std::istreambuf_iterator<char>()
            );
            trimTrailingNewlines(keyValue);

            if (keyValue.empty()) {
                return printCommandError(command, "", "key file is empty: " + options.keyFilePath, false);
            }

            options.key = std::move(keyValue);
        }

        if (!options.keyEnvName.empty()) {
            const char *envValue = std::getenv(options.keyEnvName.c_str());
            if (envValue == nullptr) {
                return printCommandError(
                    command,
                    "",
                    "environment variable " + options.keyEnvName + " is not set.",
                    false
                );
            }

            std::string keyValue(envValue);
            if (keyValue.empty()) {
                return printCommandError(
                    command,
                    "",
                    "environment variable " + options.keyEnvName + " is empty.",
                    false
                );
            }

            options.key = std::move(keyValue);
        }

        return 0;
    }

    int validateCommandRequirements(const CommandIR &command, std::string_view argv0) {
        switch (command.kind) {
            case CommandKind::Version:
            case CommandKind::Doctor:
            case CommandKind::ConfigShow:
                return 0;
            case CommandKind::Info:
                if (command.options.inputPath.empty()) {
                    return printCommandError(
                        command,
                        argv0,
                        "missing required option --in <image>.",
                        true,
                        std::string(argv0) + " info --in input.jpg --protocol classic"
                    );
                }
                return 0;
            case CommandKind::Embed:
                if (command.options.inputPath.empty()) {
                    return printCommandError(
                        command,
                        argv0,
                        "missing required option --in <image>.",
                        true,
                        std::string(argv0) + " embed --in input.jpg --out output.png --msg-block \"hello\""
                    );
                }
                if (command.options.outputPath.empty()) {
                    return printCommandError(
                        command,
                        argv0,
                        "missing required option --out <image>.",
                        true,
                        std::string(argv0) + " embed --in input.jpg --out output.png --msg-block \"hello\""
                    );
                }
                if (!command.options.hasMessage) {
                    return printCommandError(
                        command,
                        argv0,
                        "missing required option --msg-block <text>.",
                        true,
                        std::string(argv0) + " embed --in input.jpg --out output.png --msg-block \"hello\""
                    );
                }
                return 0;
            case CommandKind::Extract:
                if (command.options.inputPath.empty()) {
                    return printCommandError(
                        command,
                        argv0,
                        "missing required option --in <image>.",
                        true,
                        std::string(argv0) + " extract --in output.png"
                    );
                }
                return 0;
            case CommandKind::Verify:
                if (command.options.inputPath.empty()) {
                    return printCommandError(
                        command,
                        argv0,
                        "missing required option --in <image>.",
                        true,
                        std::string(argv0) + " verify --in output.png"
                    );
                }
                return 0;
        }
        return 0;
    }

    int checkImageGuardrails(const cv::Mat &image, const CommandIR &command) {
        const CommonOptions &options = command.options;
        const long long width = static_cast<long long>(image.cols);
        const long long height = static_cast<long long>(image.rows);

        if (options.maxWidth.has_value() && width > *options.maxWidth) {
            return printCommandError(command, "", "image width exceeds --max-width.", false);
        }

        if (options.maxHeight.has_value() && height > *options.maxHeight) {
            return printCommandError(command, "", "image height exceeds --max-height.", false);
        }

        if (options.maxPixels.has_value()) {
            if (width > std::numeric_limits<long long>::max() / height) {
                return printCommandError(
                    command,
                    "",
                    "image pixel count overflows guardrail calculation.",
                    false
                );
            }

            const long long pixels = width * height;
            if (pixels > *options.maxPixels) {
                return printCommandError(command, "", "image pixel count exceeds --max-pixels.", false);
            }
        }

        return 0;
    }

    int checkMessageGuardrail(std::string_view message, const CommandIR &command) {
        const CommonOptions &options = command.options;
        if (options.maxMessageBytes.has_value() &&
            static_cast<unsigned long long>(message.size()) >
                static_cast<unsigned long long>(*options.maxMessageBytes)) {
            return printCommandError(command, "", "message size exceeds --max-message-bytes.", false);
        }
        return 0;
    }
} // namespace roadscript::cli
