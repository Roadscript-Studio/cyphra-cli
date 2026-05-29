# Corpus Evaluation

Cyphra CLI includes dataset-scale evaluation utilities for running `rse`
across local image collections and recording structured outputs for regression
tracking.

This tooling is intended for:

- local validation and repeatable regression checks
- protocol comparison at the CLI level
- structured CSV, JSON, and SQLite reporting

It is not part of the default CLI build or normal test flow.

## Dataset layout

Local datasets can live under paths such as:

- `datasets/coco/`

Generated evaluation artifacts can live under:

- `tests/artifacts/corpus/`

Datasets and generated artifacts should stay out of version control.

## Downloading sample data

Dataset download is always explicit. Nothing auto-downloads during:

- normal CTest runs
- normal CLI builds
- CI

Example:

```sh
python3 tools/download_dataset.py \
  --dataset coco-val \
  --output-root datasets/coco \
  --limit 100
```

## Running corpus evaluation

Example single-protocol run:

```sh
python3 tools/corpus_eval.py \
  --rse ./build/rse \
  --input-dir datasets/coco/val2017 \
  --output-dir tests/artifacts/corpus/coco-classic \
  --protocol classic \
  --limit 100 \
  --jobs auto \
  --csv tests/artifacts/corpus/coco-classic/results.csv \
  --json tests/artifacts/corpus/coco-classic/results.json
```

Example protocol comparison run:

```sh
python3 tools/corpus_eval.py \
  --rse ./build/rse \
  --input-dir datasets/coco/val2017 \
  --output-dir tests/artifacts/corpus/coco-compare \
  --protocol both \
  --quick \
  --jobs auto \
  --shuffle \
  --seed 1337
```

## Supported options

- `--protocol classic|mosaic|both`
- `--limit N`
- `--quick`
- `--full`
- `--shuffle`
- `--seed`
- `--keep-outputs`
- `--fail-fast`
- `--jobs 1|N|auto`
- `--csv <path>`
- `--json <path>`
- `--sqlite <path>`
- `--failed-manifest <path>`
- `--analysis-json <path>`

Behavior notes:

- `--quick` applies a small default limit when `--limit` is not set
- `--full` uses the full discovered input set unless `--limit` is set
- `--jobs auto` resolves conservatively for the current machine
- `--keep-outputs` preserves generated output images only
- without `--keep-outputs`, generated outputs are cleaned up after evaluation
- `--fail-fast` stops scheduling new work after the first runtime failure

## Execution model

Corpus and probe parallelism is orchestration-only:

- one worker handles one independent image/protocol case
- each worker launches normal `rse` subprocesses
- the main process preserves deterministic row ordering and performs final
  CSV/JSON/SQLite export

Run metadata reports fields such as:

- `execution_mode`
- `jobs_requested`
- `jobs_resolved`
- `started_at`
- `finished_at`
- `wall_time_s`
- `throughput_cases_per_s`

## Reported results

The harness records per-image rows and aggregate summaries including:

- total images and total cases
- timing metrics
- output size metrics
- SNR values when available
- success and failure counts
- machine-readable classification fields

Each case is classified into one of the tool's public result buckets:

- `success`
- `expected_rejection`
- `recovery_failure`
- `runtime_failure`

These labels are meant to help compare CLI behavior across runs without
embedding engine-specific implementation notes in the repository docs.

For backward compatibility, row-level CSV and JSON exports include both the
older machine-oriented fields `embed_ok` and `verify_ok` and the clearer aliases
`embedding_succeeded`, `payload_recovered`, and `failure_domain`.

## Output artifacts

Typical outputs:

- `results.csv`
- `results.json`
- optional `results.sqlite`
- optional failed-image manifest JSON
- optional analysis JSON

Artifact layout:

```text
<output-dir>/
  classic/
    embedded/
      <image-stem>.png
      <image-stem>-<stable-hash>.png
  mosaic/
    embedded/
      <image-stem>.png
      <image-stem>-<stable-hash>.png
  results.csv
  results.json
  results.sqlite
  analysis.json
  failed.json
```

When multiple input images share the same basename stem, preserved outputs add a
short stable relative-path hash so parallel workers do not target the same file.

## SQLite export

Both corpus tools can append runs to a SQLite database:

```sh
python3 tools/corpus_eval.py \
  --rse ./build/rse \
  --input-dir datasets/coco/val2017 \
  --output-dir tests/artifacts/corpus/coco-classic \
  --protocol classic \
  --limit 100 \
  --sqlite tests/artifacts/corpus/coco-classic/results.sqlite
```

```sh
python3 tools/mosaic_support_probe.py \
  --rse ./build/rse \
  --output-dir tests/artifacts/corpus/mosaic-support-probe \
  --sizes 640,1024,1600 \
  --sqlite tests/artifacts/corpus/mosaic-support-probe/results.sqlite
```

The database is append-only by default: each invocation adds a new `run_id`
rather than replacing previous results.

Current tables:

- `runs`
- `cases`
- `summaries`
- `failure_categories`

The schema is intentionally simple so SQLite-compatible tools can inspect it
directly.

## Synthetic Mosaic probe

`tools/mosaic_support_probe.py` generates deterministic synthetic inputs and
records how the CLI behaves across a configurable size range.

Example:

```sh
python3 tools/mosaic_support_probe.py \
  --rse ./build/rse \
  --output-dir tests/artifacts/corpus/mosaic-support-probe \
  --sizes 320,640,1024 \
  --csv tests/artifacts/corpus/mosaic-support-probe/results.csv \
  --json tests/artifacts/corpus/mosaic-support-probe/results.json
```

Optional rectangle coverage:

```sh
python3 tools/mosaic_support_probe.py \
  --rse ./build/rse \
  --output-dir tests/artifacts/corpus/mosaic-support-probe \
  --rectangles
```

The probe:

- generates deterministic synthetic images locally
- stores generated inputs under `<output-dir>/inputs/`
- stores generated outputs under `<output-dir>/outputs/`
- writes one row per synthetic image to CSV and JSON
- summarizes results by size and classification
