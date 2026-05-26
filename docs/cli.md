# CLI

`rse` is the standalone command-line interface for Roadscript.

This repository builds `rse` against an installed `RoadscriptEngine` package.
In practice that package is expected to come from a separate Engine install.
If you have not installed the engine package yet, start with
[Getting Started](getting-started.md).

Core commands:

- `version`
- `doctor`
- `config show`
- `info`
- `embed`
- `extract`
- `verify`
- `run`

## Common Usage

Classic embed and verify:

```bash
./build/rse embed \
  --protocol classic \
  --in tests/fixtures/input/input.jpg \
  --out ./rse_classic.png \
  --msg-block "hello roadscript"

./build/rse verify \
  --protocol classic \
  --in ./rse_classic.png
```

Protocol planning with `info`:

```bash
./build/rse info \
  --protocol mosaic \
  --in tests/fixtures/input/input.jpg
```

## JSON Mode

Most operational commands support `--json` for machine-readable output.

```bash
./build/rse verify \
  --protocol classic \
  --in ./rse_classic.png \
  --json
```

## Workflow Execution

The CLI also exposes the Roadscript workflow runtime:

```bash
./build/rse run examples/classic_roundtrip.rsx --dry-run
./build/rse run examples/mosaic_debug.rsx --dry-run
```

See [DSL Reference](dsl.md) for workflow syntax and limitations.

## Related Topics

- [Getting Started](getting-started.md)
- [Corpus Evaluation](corpus_eval.md)
