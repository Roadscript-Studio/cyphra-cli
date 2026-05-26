# Getting Started

Roadscript CLI is the standalone application repository for:

- the `rse` command-line interface
- the workflow DSL runtime
- corpus evaluation and probe tooling
- the experimental TUI frontend

It depends on an installed `RoadscriptEngine` package.

## Prerequisite

Install the Engine repository first, for example:

```bash
cmake -S /path/to/roadscript-engine -B /path/to/roadscript-engine/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/tmp/roadscript-install
cmake --build /path/to/roadscript-engine/build
cmake --install /path/to/roadscript-engine/build
```

## Configure and Build

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DRoadscriptEngine_DIR=/tmp/roadscript-install/lib/cmake/RoadscriptEngine
cmake --build build
```

The local CLI binary will be:

```bash
./build/rse
```

## First CLI Run

Classic is the stable default protocol family and the best starting point for a
first run.

```bash
./build/rse embed \
  --protocol classic \
  --in tests/fixtures/input/input.jpg \
  --out /tmp/rse_classic.png \
  --msg-block "hello roadscript"

./build/rse verify \
  --protocol classic \
  --in /tmp/rse_classic.png
```

## Next Steps

- See [CLI](cli.md) for the command surface.
- See [DSL Reference](dsl.md) for scripted workflows.
- See [Corpus Evaluation](corpus_eval.md) for dataset-scale tooling.
