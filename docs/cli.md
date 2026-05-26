# CLI

`rse` is the reference command-line interface for Roadscript Engine.

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
./cmake-build-debug/rse embed \
  --protocol classic \
  --in tests/fixtures/input/input.jpg \
  --out /tmp/rse_classic.png \
  --msg-block "hello roadscript"

./cmake-build-debug/rse verify \
  --protocol classic \
  --in /tmp/rse_classic.png
```

Protocol planning with `info`:

```bash
./cmake-build-debug/rse info \
  --protocol mosaic \
  --in tests/fixtures/input/input.jpg
```

## JSON Mode

Most operational commands support `--json` for machine-readable output.

```bash
./cmake-build-debug/rse verify \
  --protocol classic \
  --in /tmp/rse_classic.png \
  --json
```

## Workflow Execution

The CLI also exposes the Roadscript workflow runtime:

```bash
./cmake-build-debug/rse run workflow.rsx
./cmake-build-debug/rse run workflow.rsx --dry-run
```

See [DSL Reference](dsl.md) for workflow syntax and limitations.

## Related Topics

- [Getting Started](getting-started.md)
- [Classic Protocol](protocols/classic.md)
- [Mosaic Protocol](protocols/mosaic.md)
- [Corpus Evaluation](corpus_eval.md)
