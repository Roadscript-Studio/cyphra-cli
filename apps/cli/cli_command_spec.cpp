#include "cli_command_spec.hpp"

#include <array>
#include <stdexcept>

namespace roadscript::cli {
    namespace {
        constexpr std::array<CommandField, 0> kNoFields{};
        constexpr std::array kInfoFields = std::to_array<CommandField>({
            CommandField::In,
            CommandField::MsgBlock,
            CommandField::Key,
            CommandField::KeyFile,
            CommandField::KeyEnv,
            CommandField::Protocol,
            CommandField::Step,
            CommandField::Layout,
            CommandField::Json,
            CommandField::MaxPixels,
            CommandField::MaxWidth,
            CommandField::MaxHeight,
            CommandField::MaxMessageBytes,
        });
        constexpr std::array kEmbedFields = std::to_array<CommandField>({
            CommandField::In,
            CommandField::Out,
            CommandField::MsgBlock,
            CommandField::Key,
            CommandField::KeyFile,
            CommandField::KeyEnv,
            CommandField::Protocol,
            CommandField::Step,
            CommandField::Layout,
            CommandField::Json,
            CommandField::MaxPixels,
            CommandField::MaxWidth,
            CommandField::MaxHeight,
            CommandField::MaxMessageBytes,
        });
        constexpr std::array kDecodeFields = std::to_array<CommandField>({
            CommandField::In,
            CommandField::Key,
            CommandField::KeyFile,
            CommandField::KeyEnv,
            CommandField::Protocol,
            CommandField::Step,
            CommandField::Layout,
            CommandField::StepSearch,
            CommandField::DebugJson,
            CommandField::DebugSvg,
            CommandField::Json,
            CommandField::MaxPixels,
            CommandField::MaxWidth,
            CommandField::MaxHeight,
        });
        constexpr std::array kInfoRequired = std::to_array<CommandField>({
            CommandField::In,
        });
        constexpr std::array kEmbedRequired = std::to_array<CommandField>({
            CommandField::In,
            CommandField::Out,
            CommandField::MsgBlock,
        });
        constexpr std::array kDecodeRequired = std::to_array<CommandField>({
            CommandField::In,
        });

        constexpr std::array<CommandFieldSpec, 17> kFieldSpecs{{
            {CommandField::In, "in", CommandFieldType::StringLike},
            {CommandField::Out, "out", CommandFieldType::StringLike},
            {CommandField::MsgBlock, "msg_block", CommandFieldType::StringLike},
            {CommandField::Key, "key", CommandFieldType::StringLike},
            {CommandField::KeyFile, "key_file", CommandFieldType::StringLike},
            {CommandField::KeyEnv, "key_env", CommandFieldType::StringLike},
            {CommandField::Protocol, "protocol", CommandFieldType::Protocol},
            {CommandField::Step, "step", CommandFieldType::Number},
            {CommandField::Layout, "layout", CommandFieldType::LayoutDecode},
            {CommandField::StepSearch, "step_search", CommandFieldType::Boolean},
            {CommandField::DebugJson, "debug_json", CommandFieldType::StringLike},
            {CommandField::DebugSvg, "debug_svg", CommandFieldType::StringLike},
            {CommandField::Json, "json", CommandFieldType::Boolean},
            {CommandField::MaxPixels, "max_pixels", CommandFieldType::PositiveInteger},
            {CommandField::MaxWidth, "max_width", CommandFieldType::PositiveInteger},
            {CommandField::MaxHeight, "max_height", CommandFieldType::PositiveInteger},
            {CommandField::MaxMessageBytes, "max_message_bytes", CommandFieldType::PositiveInteger},
        }};

        constexpr std::array<CommandSpec, 7> kCommandSpecs{{
            {
                CommandKind::Version,
                "version",
                "version",
                std::span<const CommandField>(kNoFields),
                std::span<const CommandField>(kNoFields),
                roadscript::watermark::Protocol::Classic,
                LayoutMode::CenterRing,
                false, false, false, false, false, false, false, false, false, false, true
            },
            {
                CommandKind::Doctor,
                "doctor",
                "doctor",
                std::span<const CommandField>(kNoFields),
                std::span<const CommandField>(kNoFields),
                roadscript::watermark::Protocol::Classic,
                LayoutMode::CenterRing,
                false, false, false, false, false, false, false, false, false, false, true
            },
            {
                CommandKind::ConfigShow,
                "config show",
                "config show",
                std::span<const CommandField>(kNoFields),
                std::span<const CommandField>(kNoFields),
                roadscript::watermark::Protocol::Classic,
                LayoutMode::CenterRing,
                false, false, false, false, false, false, false, false, false, false, true
            },
            {
                CommandKind::Info,
                "info",
                "info",
                std::span<const CommandField>(kInfoFields),
                std::span<const CommandField>(kInfoRequired),
                roadscript::watermark::Protocol::Classic,
                LayoutMode::CenterRing,
                true, false, true, false, false, false, false, true, true, false, true
            },
            {
                CommandKind::Embed,
                "embed",
                "embed",
                std::span<const CommandField>(kEmbedFields),
                std::span<const CommandField>(kEmbedRequired),
                roadscript::watermark::Protocol::Classic,
                LayoutMode::CenterRing,
                true, true, true, false, false, false, false, true, true, false, true
            },
            {
                CommandKind::Extract,
                "extract",
                "extract",
                std::span<const CommandField>(kDecodeFields),
                std::span<const CommandField>(kDecodeRequired),
                roadscript::watermark::Protocol::Classic,
                LayoutMode::Auto,
                true, false, false, true, true, true, true, false, true, false, true
            },
            {
                CommandKind::Verify,
                "verify",
                "verify",
                std::span<const CommandField>(kDecodeFields),
                std::span<const CommandField>(kDecodeRequired),
                roadscript::watermark::Protocol::Classic,
                LayoutMode::Auto,
                true, false, false, true, true, true, true, false, true, false, true
            },
        }};

        const CommandSpec *findCommandSpec(CommandKind kind) {
            for (const auto &spec : kCommandSpecs) {
                if (spec.kind == kind) {
                    return &spec;
                }
            }
            return nullptr;
        }

        const CommandFieldSpec *findFieldSpec(CommandField field) {
            for (const auto &spec : kFieldSpecs) {
                if (spec.field == field) {
                    return &spec;
                }
            }
            return nullptr;
        }
    } // namespace

    const CommandSpec &commandSpec(CommandKind kind) {
        const CommandSpec *spec = findCommandSpec(kind);
        if (spec == nullptr) {
            throw std::runtime_error("unknown CommandKind");
        }
        return *spec;
    }

    const CommandFieldSpec &commandFieldSpec(CommandField field) {
        const CommandFieldSpec *spec = findFieldSpec(field);
        if (spec == nullptr) {
            throw std::runtime_error("unknown CommandField");
        }
        return *spec;
    }

    std::span<const CommandSpec> allCommandSpecs() {
        return std::span<const CommandSpec>(kCommandSpecs);
    }

    std::span<const CommandFieldSpec> allCommandFieldSpecs() {
        return std::span<const CommandFieldSpec>(kFieldSpecs);
    }

    std::optional<CommandKind> commandKindFromCanonicalName(std::string_view name) {
        for (const auto &spec : kCommandSpecs) {
            if (spec.canonicalName == name) {
                return spec.kind;
            }
        }
        return std::nullopt;
    }

    std::optional<CommandField> parseDslFieldName(std::string_view name) {
        for (const auto &spec : kFieldSpecs) {
            if (spec.dslName == name) {
                return spec.field;
            }
        }
        return std::nullopt;
    }

    bool commandAllowsField(CommandKind kind, CommandField field) {
        const auto &spec = commandSpec(kind);
        for (CommandField allowed : spec.allowedFields) {
            if (allowed == field) {
                return true;
            }
        }
        return false;
    }

    bool commandRequiresField(CommandKind kind, CommandField field) {
        const auto &spec = commandSpec(kind);
        for (CommandField required : spec.requiredFields) {
            if (required == field) {
                return true;
            }
        }
        return false;
    }

    std::string_view commandDisplayLabel(CommandKind kind) {
        return commandSpec(kind).displayLabel;
    }

    std::string_view commandCanonicalName(CommandKind kind) {
        return commandSpec(kind).canonicalName;
    }

    std::string_view commandFieldDslName(CommandField field) {
        return commandFieldSpec(field).dslName;
    }
} // namespace roadscript::cli
