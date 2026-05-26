set(RSE_CLI_SOURCES
    cli_command_spec.cpp
    cli_commands.cpp
    cli_dsl_lexer.cpp
    cli_dsl_lowering.cpp
    cli_dsl_parser.cpp
    cli_dsl_runtime.cpp
    cli_dsl_semantic.cpp
    cli_debug_artifacts.cpp
    cli_ir.cpp
    cli_ir_validation.cpp
    cli_parse.cpp
    cli_print.cpp
    main.cpp
)

add_executable(rse ${RSE_CLI_SOURCES})
target_link_libraries(rse PRIVATE Roadscript::rsengine)
target_compile_definitions(rse PRIVATE RS_CLI_VERSION="${PROJECT_VERSION}")
set_target_properties(rse PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
roadscript_configure_target(rse)
