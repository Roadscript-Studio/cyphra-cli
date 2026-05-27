# Roadscript CLI

`roadscript-cli` is the public command-line, workflow DSL, and local TUI repository for Roadscript.

Roadscript is an authenticity tooling project focused on media provenance, invisible watermarking workflows, and verification-oriented application infrastructure. This repository demonstrates the public application layer around Roadscript while keeping the private core engine implementation separate.

## Repository Role

This repository is part of the Roadscript public/private repository ecosystem:

| Repository | Visibility | Purpose |
|---|---:|---|
| [`roadscript-docs`](https://github.com/Roadscript-Studio/roadscript-docs) | Public | Public overview, architecture notes, repository boundaries, and development milestones. |
| [`roadscript-cli`](https://github.com/Roadscript-Studio/roadscript-cli) | Public | Standalone CLI, workflow DSL, local TUI prototype, examples, tests, and tooling. |
| [`roadscript-site`](https://github.com/Roadscript-Studio/roadscript-site) | Public | Public website and sample-driven demo surface. |
| `roadscript-engine` | Private | Private C++ core package consumed by the application layer. |

`roadscript-cli` focuses on developer-facing workflow surfaces, command orchestration, examples, and application-layer tests. It does not include the private Engine source.

## Overview

This repository contains:

- a C++ CLI frontend
- the `rse` command-line application
- the Roadscript workflow DSL
- a local TUI prototype
- runnable workflow examples
- CLI-level regression tests and fixtures
- corpus/evaluation helper tooling

The repository should be read as a public engineering artifact and a working application shell around the private Engine package. It is not presented as a finished end-user product release.

## Architecture

The CLI is built as a consumer of an installed CMake package:

```cmake
find_package(RoadscriptEngine CONFIG REQUIRED)
target_link_libraries(rse PRIVATE Roadscript::rsengine)
```

The private `roadscript-engine` repository owns the core C++ engine implementation. Its public integration boundary is the installed `RoadscriptEngine` CMake package and the exported `Roadscript::rsengine` target.

This split is intentional:

- `roadscript-engine` owns the private core package and implementation details.
- `roadscript-cli` owns the public CLI, workflow DSL, local TUI prototype, tests, examples, and tooling.
- The application layer consumes the Engine through a package boundary instead of directly bundling Engine source.
- Public code demonstrates application architecture without exposing private Engine internals.

## Build

Configure the project with an installed `RoadscriptEngine` package:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DRoadscriptEngine_DIR=/path/to/RoadscriptEngine/cmake

cmake --build build
```

The resulting CLI binary is expected at:

```text
build/rse
```

## Example Usage

```bash
./build/rse --help
./build/rse run examples/classic_roundtrip.rsx --dry-run
```

The example workflow is designed to exercise the CLI and DSL planning path without requiring a production backend or public Engine source.

## Repository Layout

```text
.
├── apps/
│   ├── cli/        # C++ command-line application
│   └── tui/        # Local TUI prototype
├── docs/           # CLI, DSL, and tooling documentation
├── examples/       # Example workflow scripts
├── tests/          # CLI-level regression tests and fixtures
├── tools/          # Evaluation and reporting helpers
├── CMakeLists.txt
└── README.md
```

## Documentation

- [`docs/cli.md`](docs/cli.md) — command-line usage and command surface
- [`docs/dsl.md`](docs/dsl.md) — workflow DSL concepts and examples
- [`docs/corpus_eval.md`](docs/corpus_eval.md) — corpus evaluation tooling notes

## Current Status

- Active development
- Public application/tooling repository
- Private Engine dependency through `RoadscriptEngine`
- Standalone CMake build against an installed Engine package
- Runnable workflow examples and CLI-level tests
- Local TUI prototype for command exploration

This repository is published to demonstrate Roadscript’s application architecture, tooling, tests, examples, and package-consumer design. It should not be interpreted as a complete commercial release.

## Notes For Reviewers

This repository demonstrates:

- CMake package consumption against a separately installed dependency
- CLI command structure and output design
- workflow DSL parsing, lowering, and runtime orchestration
- examples, docs, and local developer ergonomics
- regression-style tests and fixtures at the application layer
- a clean repository split between a public application repo and a private core Engine
- public-safe documentation around a protected implementation boundary

For the full project map and architecture context, start with [`roadscript-docs`](https://github.com/Roadscript-Studio/roadscript-docs).

## Related Repositories

- [`roadscript-docs`](https://github.com/Roadscript-Studio/roadscript-docs) — public overview, architecture notes, and repository boundaries.
- [`roadscript-site`](https://github.com/Roadscript-Studio/roadscript-site) — public website and sample-driven demo surface.
- `roadscript-engine` — private C++ core package consumed through `RoadscriptEngine` and `Roadscript::rsengine`.

## License

License information will be added before external reuse. For now, this repository is published for portfolio and project review.
