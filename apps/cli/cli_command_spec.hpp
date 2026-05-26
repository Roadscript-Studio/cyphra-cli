#pragma once

#include "cli_ir.hpp"

#include <optional>
#include <span>
#include <string_view>

namespace roadscript::cli {
    enum class CommandField {
        In,
        Out,
        MsgBlock,
        Key,
        KeyFile,
        KeyEnv,
        Protocol,
        Step,
        Layout,
        StepSearch,
        DebugJson,
        DebugSvg,
        Json,
        MaxPixels,
        MaxWidth,
        MaxHeight,
        MaxMessageBytes,
    };

    enum class CommandFieldType {
        StringLike,
        Number,
        Boolean,
        PositiveInteger,
        Protocol,
        LayoutInfoEmbed,
        LayoutDecode,
    };

    struct CommandFieldSpec {
        CommandField field;
        std::string_view dslName;
        CommandFieldType type;
    };

    struct CommandSpec {
        CommandKind kind;
        std::string_view canonicalName;
        std::string_view displayLabel;
        std::span<const CommandField> allowedFields;
        std::span<const CommandField> requiredFields;
        roadscript::watermark::Protocol defaultProtocol;
        LayoutMode defaultLayout;
        bool allowKeyResolution = false;
        bool allowsOutput = false;
        bool allowsMessage = false;
        bool allowsAutoLayout = false;
        bool allowsStepSearch = false;
        bool allowsDebugJson = false;
        bool allowsDebugSvg = false;
        bool allowsMaxMessageBytes = false;
        bool allowsJsonCli = false;
        bool allowsJsonWorkflow = false;
        bool executableInWorkflow = true;
    };

    const CommandSpec &commandSpec(CommandKind kind);
    const CommandFieldSpec &commandFieldSpec(CommandField field);
    std::span<const CommandSpec> allCommandSpecs();
    std::span<const CommandFieldSpec> allCommandFieldSpecs();

    std::optional<CommandKind> commandKindFromCanonicalName(std::string_view name);
    std::optional<CommandField> parseDslFieldName(std::string_view name);

    bool commandAllowsField(CommandKind kind, CommandField field);
    bool commandRequiresField(CommandKind kind, CommandField field);
    std::string_view commandDisplayLabel(CommandKind kind);
    std::string_view commandCanonicalName(CommandKind kind);
    std::string_view commandFieldDslName(CommandField field);
} // namespace roadscript::cli
