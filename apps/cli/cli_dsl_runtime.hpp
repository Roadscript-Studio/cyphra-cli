#pragma once

#include "cli_parse.hpp"

#include <string_view>

namespace roadscript::cli {
    int runDslWorkflow(const RunCommandOptions &options, std::string_view argv0);
} // namespace roadscript::cli
