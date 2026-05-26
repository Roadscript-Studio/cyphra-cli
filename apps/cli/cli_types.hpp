#pragma once

#include "roadscript/watermark/api.hpp"

#include <optional>
#include <string>

namespace roadscript::cli {
    enum class ColorMode {
        Auto,
        Always,
        Never,
    };

    enum class LayoutMode {
        CenterRing,
        KeyedShuffle,
        Auto,
    };

    struct CommonOptions {
        std::string inputPath;
        std::string outputPath;
        std::string debugJsonPath;
        std::string debugSvgPath;
        std::string message;
        std::optional<std::string> key;
        std::string keyFilePath;
        std::string keyEnvName;
        std::optional<long long> maxPixels;
        std::optional<long long> maxWidth;
        std::optional<long long> maxHeight;
        std::optional<long long> maxMessageBytes;
        float step = 30.0f;
        roadscript::watermark::Protocol protocol = roadscript::watermark::Protocol::Classic;
        LayoutMode layout = LayoutMode::CenterRing;
        ColorMode colorMode = ColorMode::Auto;
        bool enableStepSearch = false;
        bool hasMessage = false;
        bool json = false;
    };
} // namespace roadscript::cli
