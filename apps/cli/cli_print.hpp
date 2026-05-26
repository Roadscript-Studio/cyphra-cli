#pragma once

#include "cli_dsl_ast.hpp"
#include "cli_types.hpp"
#include "roadscript/watermark/api.hpp"
#include "roadscript/watermark/extract.hpp"

#include <iosfwd>
#include <string>
#include <string_view>

namespace roadscript::cli {
    // Output formatting is centralized here so command handlers can focus on
    // orchestration while keeping stdout/stderr behavior stable.
    void setCliColorMode(ColorMode mode);
    ColorMode cliColorMode();
    void printTitle(std::ostream &out, std::string_view title);
    void printSectionHeader(std::ostream &out, std::string_view title);
    void printExampleLine(std::ostream &out, std::string_view commandLine);
    void printKeyValue(std::ostream &out, std::string_view key, std::string_view value);
    void printKeyValue(std::ostream &out, std::string_view key, int value);
    void printKeyValue(std::ostream &out, std::string_view key, std::size_t value);
    void printKeyValue(std::ostream &out, std::string_view key, long long value);
    void printKeyValue(std::ostream &out, std::string_view key, double value, int precision = 2);
    void printMultilineKeyValue(std::ostream &out, std::string_view key, std::string_view value);
    void printStatusLine(std::ostream &out, std::string_view label, bool succeeded);
    void printHumanError(std::ostream &out, std::string_view reason);
    void printTryLine(std::ostream &out, std::string_view commandLine);
    void printNoteLine(std::ostream &out, std::string_view note);
    void printDiagnostic(std::ostream &out, const roadscript::cli::dsl::Diagnostic &diagnostic);

    std::string formatProtocolLabel(roadscript::watermark::Protocol protocol);
    std::string formatBoolYesNo(bool value);
    std::string formatBoolOnOff(bool value);
    std::string formatPassFail(bool value);
    std::string jsonEscape(std::string_view input);
    std::string xmlEscape(std::string_view input);
    std::string jsonString(std::string_view input);
    std::string jsonNumber(double value, int precision = 2);

    void jsonFieldString(std::ostream &out, bool &first, std::string_view key, std::string_view value);
    void jsonFieldInt(std::ostream &out, bool &first, std::string_view key, int value);
    void jsonFieldBool(std::ostream &out, bool &first, std::string_view key, bool value);
    void jsonFieldNull(std::ostream &out, bool &first, std::string_view key);
    void jsonFieldDouble(
        std::ostream &out,
        bool &first,
        std::string_view key,
        double value,
        int precision = 2
    );

    void emitMosaicEmbedJsonFields(
        std::ostream &out,
        bool &first,
        const roadscript::watermark::EmbedBGRAResult &result
    );
    void emitMosaicDecodeJsonFields(
        std::ostream &out,
        bool &first,
        const roadscript::watermark::DecodeResult &result
    );

    void printMosaicEmbedLines(std::ostream &out, const roadscript::watermark::EmbedBGRAResult &result);
    void printMosaicDecodeLines(std::ostream &out, const roadscript::watermark::DecodeResult &result);
    void printProtocolBannerIfNeeded(std::ostream &out, roadscript::watermark::Protocol protocol);
    void printJsonError(std::ostream &out, std::string_view reason);

    void printVersion(std::ostream &out);
    void printDoctor(std::ostream &out);
    void printConfig(std::ostream &out);
} // namespace roadscript::cli
