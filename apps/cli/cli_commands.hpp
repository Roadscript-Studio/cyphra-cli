#pragma once

#include "cli_ir.hpp"

#include <string_view>

namespace roadscript::cli {
    int executeCommandIR(CommandIR &command, std::string_view argv0);
    int runCli(int argc, char **argv);
} // namespace roadscript::cli
