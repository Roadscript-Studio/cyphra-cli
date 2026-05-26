#pragma once

#include "cli_dsl_ast.hpp"

#include <string_view>
#include <vector>

namespace roadscript::cli::dsl {
    struct ParseResult {
        Program program;
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool ok() const { return diagnostics.empty(); }
    };

    ParseResult parseWorkflowScript(std::string_view file, std::string_view source);
} // namespace roadscript::cli::dsl
