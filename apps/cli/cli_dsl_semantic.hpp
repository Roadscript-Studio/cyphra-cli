#pragma once

#include "cli_dsl_ast.hpp"

namespace roadscript::cli::dsl {
    struct SemanticResult {
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool ok() const { return diagnostics.empty(); }
    };

    SemanticResult validateWorkflowProgram(const Program &program);
} // namespace roadscript::cli::dsl
