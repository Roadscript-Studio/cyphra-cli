#pragma once

#include "cli_ir.hpp"

#include <opencv2/core.hpp>

#include <string_view>

namespace roadscript::cli {
    int resolveCommandKeySource(CommandIR &command);
    int validateCommandRequirements(const CommandIR &command, std::string_view argv0);
    int checkImageGuardrails(const cv::Mat &image, const CommandIR &command);
    int checkMessageGuardrail(std::string_view message, const CommandIR &command);
} // namespace roadscript::cli
