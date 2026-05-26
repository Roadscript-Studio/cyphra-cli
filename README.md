# Roadscript CLI

A standalone command-line and local TUI application for Roadscript workflows.

## Overview

This is the public application-layer repository for Roadscript. It focuses on
the developer-facing surfaces around the project rather than the private core
engine implementation.

This repository contains:

- a C++ CLI frontend
- the Roadscript workflow DSL
- a local TUI prototype
- runnable examples
- CLI-level tests
- corpus evaluation tooling

## Architecture

The CLI is built as a consumer of an installed CMake package:

```cmake
find_package(RoadscriptEngine CONFIG REQUIRED)
target_link_libraries(rse PRIVATE Roadscript::rsengine)
```

The core `RoadscriptEngine` package is currently private while the underlying
technology is being productized. The public integration boundary for this
repository is the installed CMake package and its exported
`Roadscript::rsengine` target.

This split is intentional:

- the private Engine repository owns the core C++ engine implementation
- this public repository owns application-layer tooling and integration
- the CLI demonstrates how the application is structured around a package
  consumer boundary rather than a monorepo-only build

## Build

Configure the project with an installed `RoadscriptEngine` package:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DRoadscriptEngine_DIR=/path/to/RoadscriptEngine/cmake

cmake --build build
```

## Example Usage

```bash
./build/rse --help
./build/rse run examples/classic_roundtrip.rsx --dry-run
```

## Repository Layout

- `apps/cli`
  C++ command-line application entrypoint, command handling, output formatting,
  and workflow execution glue.
- `apps/tui`
  Local TUI prototype for running `rse` commands in a more interactive shell.
- `docs`
  Public-facing CLI, DSL, and tooling documentation.
- `examples`
  Example workflow scripts for dry-run and local execution.
- `tests`
  CLI-level regression coverage, fixtures, and smoke-style validation.
- `tools`
  Helper utilities for evaluation, dataset workflows, and reporting.

## Current Status

- Active development
- `RoadscriptEngine` is currently private
- This CLI repository is published to demonstrate application architecture,
  tooling, tests, and package-consumer design

The repository should be read as an engineering portfolio artifact and a
working application shell around the private engine package. It is not yet being
presented as a fully public end-user product release.

## Notes For Reviewers

This repository is intended to show:

- CMake package consumption against a separately installed dependency
- CLI command structure and command-surface design
- workflow DSL parsing, lowering, and runtime orchestration
- examples, docs, and local developer ergonomics
- regression-style tests and fixtures at the application layer
- a repository split between a public application repo and a private core engine

## License

License information will be added before external reuse. For now, this
repository is published for portfolio review.
