#pragma once

#include "cli_types.hpp"
#include "roadscript/watermark/extract.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <string_view>

namespace roadscript::cli {
    // Debug artifacts are rendered in the CLI so the engine can expose
    // structured telemetry without taking on presentation-only dependencies.
    std::string buildMosaicDebugJson(
        const CommonOptions &options,
        const roadscript::watermark::DecodeResult &decoded,
        const cv::Mat &image,
        std::string_view layout
    );

    std::string buildMosaicDebugSvg(
        const CommonOptions &options,
        const roadscript::watermark::DecodeResult &decoded,
        const cv::Mat &image,
        std::string_view layout
    );

    bool writeDebugArtifactFile(
        const std::string &path,
        const std::string &payload,
        std::string_view label,
        std::string &error
    );
} // namespace roadscript::cli
