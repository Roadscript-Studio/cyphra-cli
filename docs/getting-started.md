# Getting Started

Roadscript Engine is a native watermarking system with:

- a C++ engine library
- the `rse` command-line interface
- a small workflow DSL for scripted CLI runs

For most users, the recommended starting point is:

1. build the project
2. run the regression suite
3. try a small Classic embed and verify flow

## Build and Test

```bash
cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug --output-on-failure
```

Or use the wrapper:

```bash
./scripts/test.sh
```

## First CLI Run

Classic is the stable default protocol family and the best starting point for a
first run.

```bash
./cmake-build-debug/rse embed \
  --protocol classic \
  --in tests/fixtures/input/input.jpg \
  --out /tmp/rse_classic.png \
  --msg-block "hello roadscript"

./cmake-build-debug/rse verify \
  --protocol classic \
  --in /tmp/rse_classic.png
```

## Next Steps

- See [CLI](cli.md) for the command surface.
- See [DSL Reference](dsl.md) for scripted workflows.
- See [Classic Protocol](protocols/classic.md) and
  [Mosaic Protocol](protocols/mosaic.md) for protocol-specific guidance.
- See [Contributing](contributing.md) for local development workflows.
