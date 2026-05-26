#include "cli_print.hpp"

#include <cstdio>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <opencv2/core/version.hpp>

#ifndef RS_CLI_VERSION
#define RS_CLI_VERSION "0.1.0"
#endif

namespace roadscript::cli {
    namespace {
        constexpr int kKeyWidth = 20;

        enum class StyleRole {
            Title,
            Section,
            Label,
            Value,
            Success,
            Failure,
            Warning,
            ProtocolClassic,
            ProtocolMosaic,
            Path,
            Command,
            Note,
            Error
        };

        ColorMode gCliColorMode = ColorMode::Auto;

        int streamFileDescriptor(std::ostream &out) {
            if (&out == &std::cout) {
                return
#if defined(_WIN32)
                    _fileno(stdout);
#else
                    fileno(stdout);
#endif
            }
            if (&out == &std::cerr) {
                return
#if defined(_WIN32)
                    _fileno(stderr);
#else
                    fileno(stderr);
#endif
            }
            return -1;
        }

        bool streamSupportsColor(std::ostream &out) {
            switch (gCliColorMode) {
                case ColorMode::Always:
                    return true;
                case ColorMode::Never:
                    return false;
                case ColorMode::Auto: {
                    const int fd = streamFileDescriptor(out);
                    if (fd < 0) {
                        return false;
                    }
#if defined(_WIN32)
                    return _isatty(fd) != 0;
#else
                    return isatty(fd) != 0;
#endif
                }
            }
            return false;
        }

        std::string styleText(std::ostream &out, std::string_view text, std::string_view ansiCode) {
            if (!streamSupportsColor(out)) {
                return std::string(text);
            }
            return "\x1b[" + std::string(ansiCode) + "m" + std::string(text) + "\x1b[0m";
        }

        std::string styleByRole(std::ostream &out, std::string_view text, StyleRole role) {
            switch (role) {
                case StyleRole::Title:
                    return styleText(out, text, "1;38;5;81");
                case StyleRole::Section:
                    return styleText(out, text, "1;38;5;110");
                case StyleRole::Label:
                    return styleText(out, text, "38;5;246");
                case StyleRole::Value:
                    return std::string(text);
                case StyleRole::Success:
                    return styleText(out, text, "38;5;78");
                case StyleRole::Failure:
                    return styleText(out, text, "38;5;203");
                case StyleRole::Warning:
                    return styleText(out, text, "38;5;221");
                case StyleRole::ProtocolClassic:
                    return styleText(out, text, "38;5;117");
                case StyleRole::ProtocolMosaic:
                    return styleText(out, text, "38;5;221");
                case StyleRole::Path:
                    return styleText(out, text, "38;5;116");
                case StyleRole::Command:
                    return styleText(out, text, "38;5;81");
                case StyleRole::Note:
                    return styleText(out, text, "38;5;244");
                case StyleRole::Error:
                    return styleText(out, text, "1;38;5;203");
            }
            return std::string(text);
        }

        bool isStatusKey(std::string_view key) {
            return key == "Verification" || key == "Payload Recovery" || key == "Embedding" ||
                key == "Supported" || key == "Fits" || key == "Status" || key == "CLI" ||
                key == "Engine" || key == "OpenCV" || key == "Output exists" ||
                key == "Debug JSON exists" || key == "Debug SVG exists";
        }

        bool isProtocolKey(std::string_view key) {
            return key == "Protocol" || key == "Protocol default";
        }

        bool isPathKey(std::string_view key) {
            return key == "Input" || key == "Output" || key == "Script" || key == "Source" ||
                key == "Debug JSON" || key == "Debug SVG" || key == "Failed source" ||
                key.starts_with("Artifact ");
        }

        bool isCommandKey(std::string_view key) {
            return key == "Command";
        }

        std::string styleStatusValue(std::ostream &out, std::string_view value) {
            if (value == "PASS" || value == "succeeded" || value == "yes" || value == "OK" ||
                value == "available" || value == "ready" || value == "linked") {
                return styleByRole(out, value, StyleRole::Success);
            }
            if (value == "FAIL" || value == "FAILED" || value == "failed" || value == "runtime_failure" ||
                value == "recovery_failure") {
                return styleByRole(out, value, StyleRole::Failure);
            }
            if (value == "expected_rejection" || value == "warning") {
                return styleByRole(out, value, StyleRole::Warning);
            }
            if (value.starts_with("available (")) {
                return styleByRole(out, value, StyleRole::Success);
            }
            if (value.find("experimental") != std::string_view::npos) {
                return styleByRole(out, value, StyleRole::Warning);
            }
            return std::string(value);
        }

        std::string paddedKey(std::string_view key) {
            std::ostringstream out;
            out << std::left << std::setw(kKeyWidth) << key;
            return out.str();
        }

        std::string styleLabel(std::ostream &out, std::string_view label) {
            return styleByRole(out, paddedKey(label), StyleRole::Label);
        }

        std::string styleKeyValue(std::ostream &out, std::string_view key, std::string_view value) {
            if (isStatusKey(key)) {
                return styleStatusValue(out, value);
            }
            if (isProtocolKey(key)) {
                if (value.find("mosaic") != std::string_view::npos) {
                    return styleByRole(out, value, StyleRole::ProtocolMosaic);
                }
                return styleByRole(out, value, StyleRole::ProtocolClassic);
            }
            if (isPathKey(key)) {
                return styleByRole(out, value, StyleRole::Path);
            }
            if (isCommandKey(key)) {
                return styleByRole(out, value, StyleRole::Command);
            }
            return styleByRole(out, value, StyleRole::Value);
        }
    } // namespace

    void setCliColorMode(ColorMode mode) {
        gCliColorMode = mode;
    }

    ColorMode cliColorMode() {
        return gCliColorMode;
    }

    void printTitle(std::ostream &out, std::string_view title) {
        out << styleByRole(out, title, StyleRole::Title) << "\n";
    }

    void printSectionHeader(std::ostream &out, std::string_view title) {
        out << styleByRole(out, title, StyleRole::Section) << "\n";
    }

    void printExampleLine(std::ostream &out, std::string_view commandLine) {
        out << "  " << styleByRole(out, commandLine, StyleRole::Command) << "\n";
    }

    void printKeyValue(std::ostream &out, std::string_view key, std::string_view value) {
        out << styleLabel(out, key) << ": " << styleKeyValue(out, key, value) << "\n";
    }

    void printKeyValue(std::ostream &out, std::string_view key, int value) {
        out << styleLabel(out, key) << ": " << value << "\n";
    }

    void printKeyValue(std::ostream &out, std::string_view key, std::size_t value) {
        out << styleLabel(out, key) << ": " << value << "\n";
    }

    void printKeyValue(std::ostream &out, std::string_view key, long long value) {
        out << styleLabel(out, key) << ": " << value << "\n";
    }

    void printKeyValue(std::ostream &out, std::string_view key, double value, int precision) {
        std::ostringstream formatted;
        formatted << std::fixed << std::setprecision(precision) << value;
        printKeyValue(out, key, formatted.str());
    }

    void printMultilineKeyValue(std::ostream &out, std::string_view key, std::string_view value) {
        const std::string padding(kKeyWidth + 2, ' ');
        const std::size_t newline = value.find('\n');
        if (newline == std::string_view::npos) {
            printKeyValue(out, key, value);
            return;
        }

        printKeyValue(out, key, value.substr(0, newline));
        std::size_t lineStart = newline + 1;
        while (lineStart <= value.size()) {
            const std::size_t nextNewline = value.find('\n', lineStart);
            const std::string_view line = nextNewline == std::string_view::npos
                ? value.substr(lineStart)
                : value.substr(lineStart, nextNewline - lineStart);
            out << padding << line << "\n";
            if (nextNewline == std::string_view::npos) {
                break;
            }
            lineStart = nextNewline + 1;
        }
    }

    void printStatusLine(std::ostream &out, std::string_view label, bool succeeded) {
        printKeyValue(out, label, succeeded ? "PASS" : "FAIL");
    }

    void printHumanError(std::ostream &out, std::string_view reason) {
        out << styleByRole(out, "Error:", StyleRole::Error) << " " << reason << "\n";
    }

    void printTryLine(std::ostream &out, std::string_view commandLine) {
        out << styleByRole(out, "Try:", StyleRole::Warning) << "\n";
        printExampleLine(out, commandLine);
    }

    void printNoteLine(std::ostream &out, std::string_view note) {
        out << styleByRole(out, "Note:", StyleRole::Note) << " "
            << styleByRole(out, note, StyleRole::Note) << "\n";
    }

    void printDiagnostic(std::ostream &out, const roadscript::cli::dsl::Diagnostic &diagnostic) {
        out << diagnostic.span.file
            << ":" << diagnostic.span.begin.line
            << ":" << diagnostic.span.begin.column
            << ": " << diagnostic.message << "\n";
    }

    std::string formatProtocolLabel(roadscript::watermark::Protocol protocol) {
        if (roadscript::watermark::isMosaicProtocol(protocol)) {
            return "mosaic (experimental)";
        }
        return std::string(roadscript::watermark::protocolName(protocol));
    }

    std::string formatBoolYesNo(bool value) {
        return value ? "yes" : "no";
    }

    std::string formatBoolOnOff(bool value) {
        return value ? "on" : "off";
    }

    std::string formatPassFail(bool value) {
        return value ? "PASS" : "FAIL";
    }

    std::string jsonEscape(std::string_view input) {
        std::string out;
        out.reserve(input.size() + 8);

        for (const unsigned char c : input) {
            switch (c) {
                case '\"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        std::ostringstream hex;
                        hex << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(c);
                        out += hex.str();
                    } else {
                        out += static_cast<char>(c);
                    }
                    break;
            }
        }

        return out;
    }

    std::string xmlEscape(std::string_view input) {
        std::string out;
        out.reserve(input.size() + 8);
        for (const unsigned char c : input) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&apos;"; break;
                default: out += static_cast<char>(c); break;
            }
        }
        return out;
    }

    std::string jsonString(std::string_view input) {
        return "\"" + jsonEscape(input) + "\"";
    }

    std::string jsonNumber(double value, int precision) {
        if (!std::isfinite(value)) {
            return "null";
        }

        std::ostringstream out;
        out << std::fixed << std::setprecision(precision) << value;
        return out.str();
    }

    void jsonFieldString(std::ostream &out, bool &first, std::string_view key, std::string_view value) {
        if (!first) out << ",\n";
        first = false;
        out << "  " << jsonString(key) << ": " << jsonString(value);
    }

    void jsonFieldInt(std::ostream &out, bool &first, std::string_view key, int value) {
        if (!first) out << ",\n";
        first = false;
        out << "  " << jsonString(key) << ": " << value;
    }

    void jsonFieldBool(std::ostream &out, bool &first, std::string_view key, bool value) {
        if (!first) out << ",\n";
        first = false;
        out << "  " << jsonString(key) << ": " << (value ? "true" : "false");
    }

    void jsonFieldNull(std::ostream &out, bool &first, std::string_view key) {
        if (!first) out << ",\n";
        first = false;
        out << "  " << jsonString(key) << ": null";
    }

    void jsonFieldDouble(
        std::ostream &out,
        bool &first,
        std::string_view key,
        double value,
        int precision
    ) {
        if (!first) out << ",\n";
        first = false;
        out << "  " << jsonString(key) << ": " << jsonNumber(value, precision);
    }

    void emitMosaicEmbedJsonFields(
        std::ostream &out,
        bool &first,
        const roadscript::watermark::EmbedBGRAResult &result
    ) {
        jsonFieldInt(out, first, "overhead_bits", result.overheadBits);
        jsonFieldInt(out, first, "effective_payload_bits", result.effectivePayloadBits);
        jsonFieldBool(out, first, "metadata_recovered", result.metadataRecovered);
        jsonFieldInt(out, first, "valid_fragments", result.validFragments);
        jsonFieldInt(out, first, "unique_fragments", result.uniqueFragments);
        jsonFieldInt(out, first, "valid_parity_fragments", result.validParityFragments);
        jsonFieldInt(out, first, "recovered_via_parity", result.recoveredSourceFragmentsViaParity);
        jsonFieldBool(out, first, "local_search", result.localSearchEnabled);
    }

    void emitMosaicDecodeJsonFields(
        std::ostream &out,
        bool &first,
        const roadscript::watermark::DecodeResult &result
    ) {
        jsonFieldInt(out, first, "overhead_bits", result.overheadBits);
        jsonFieldInt(out, first, "effective_payload_bits", result.effectivePayloadBits);
        jsonFieldBool(out, first, "metadata_recovered", result.metadataRecovered);
        jsonFieldInt(out, first, "valid_fragments", result.validFragments);
        jsonFieldInt(out, first, "unique_fragments", result.uniqueFragments);
        jsonFieldInt(out, first, "valid_parity_fragments", result.validParityFragments);
        jsonFieldInt(out, first, "recovered_via_parity", result.recoveredSourceFragmentsViaParity);
        jsonFieldBool(out, first, "local_search", result.localSearchEnabled);
    }

    void printMosaicEmbedLines(std::ostream &out, const roadscript::watermark::EmbedBGRAResult &result) {
        if (result.protocol != "mosaic") {
            return;
        }
        printKeyValue(out, "Overhead bits", result.overheadBits);
        printKeyValue(out, "Effective payload", result.effectivePayloadBits);
        printKeyValue(out, "Metadata recovered", formatBoolYesNo(result.metadataRecovered));
        printKeyValue(out, "Valid fragments", result.validFragments);
        printKeyValue(out, "Unique fragments", result.uniqueFragments);
        printKeyValue(out, "Valid parity", result.validParityFragments);
        printKeyValue(out, "Recovered parity", result.recoveredSourceFragmentsViaParity);
        printKeyValue(out, "Local search", formatBoolOnOff(result.localSearchEnabled));
    }

    void printMosaicDecodeLines(std::ostream &out, const roadscript::watermark::DecodeResult &result) {
        if (result.protocol != "mosaic") {
            return;
        }
        printKeyValue(out, "Overhead bits", result.overheadBits);
        printKeyValue(out, "Effective payload", result.effectivePayloadBits);
        printKeyValue(out, "Metadata recovered", formatBoolYesNo(result.metadataRecovered));
        printKeyValue(out, "Valid fragments", result.validFragments);
        printKeyValue(out, "Unique fragments", result.uniqueFragments);
        printKeyValue(out, "Valid parity", result.validParityFragments);
        printKeyValue(out, "Recovered parity", result.recoveredSourceFragmentsViaParity);
        printKeyValue(out, "Local search", formatBoolOnOff(result.localSearchEnabled));
    }

    void printProtocolBannerIfNeeded(std::ostream &out, roadscript::watermark::Protocol protocol) {
        if (roadscript::watermark::isMosaicProtocol(protocol)) {
            printKeyValue(out, "Protocol", formatProtocolLabel(protocol));
        }
    }

    void printJsonError(std::ostream &out, std::string_view reason) {
        out << "{\n"
            << "  \"status\": \"FAIL\",\n"
            << "  \"error\": " << jsonString(reason) << "\n"
            << "}\n";
    }

    void printVersion(std::ostream &out) {
        printTitle(out, "Roadscript Engine");
        printKeyValue(out, "Version", RS_CLI_VERSION);
        printKeyValue(out, "CLI", "rse");
        printKeyValue(out, "Protocol default", "classic");
    }

    void printDoctor(std::ostream &out) {
        printTitle(out, "Roadscript Doctor");
        printKeyValue(out, "CLI", "available");
        printKeyValue(out, "Engine", "ready");
        printKeyValue(out, "OpenCV", std::string("available (") + CV_VERSION + ")");
        printKeyValue(out, "Protocol default", "classic");
    }

    void printConfig(std::ostream &out) {
        printTitle(out, "Roadscript Configuration");
        printSectionHeader(out, "Profile");
        printKeyValue(out, "Profile Name", "default");
        printKeyValue(out, "Message template", "not configured");
        out << "\n";

        printSectionHeader(out, "Defaults");
        printKeyValue(out, "Protocol", "classic");
        printKeyValue(out, "Step", 30.0, 2);
        printKeyValue(out, "Layout", "center-ring");
        printKeyValue(out, "Color", "auto");
        out << "\n";

        printSectionHeader(out, "Storage");
        printKeyValue(out, "Config source", "built-in defaults");
        printKeyValue(out, "Config file", "not configured");
        printKeyValue(out, "Status", "ready");
        out << "\n";

        printNoteLine(out, "Future planned behavior: config set color auto|always|never.");
        printNoteLine(out, "When implemented, command-line --color will override the configured default.");
    }
} // namespace roadscript::cli
