cmake_minimum_required(VERSION 3.25)

project(RoadscriptCLI VERSION 0.1.0 LANGUAGES CXX)

option(RS_ENABLE_WARNINGS "Enable compiler warnings for Cyphra CLI targets." ON)
option(RS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for Cyphra CLI targets." OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(roadscript_configure_target target)
    if(NOT RS_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(RS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        return()
    endif()

    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(RS_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

find_package(RoadscriptEngine CONFIG REQUIRED)

add_subdirectory(apps/cli)
