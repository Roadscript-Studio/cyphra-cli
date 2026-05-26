# Corpus Evaluation

Roadscript's corpus evaluation harness is for large-scale watermark robustness
and performance measurement across real-world image sets.

This tooling is:

- for evaluation and regression-style measurement
- not for AI training
- intentionally isolated from the core engine, CLI semantics, and protocol logic

## Purpose

Use the corpus harness to:

- run `rse embed` and `rse verify` across many images automatically
- compare `classic` and `mosaic` behavior at scale
- collect structured timing, support-rate, payload-recovery-rate, and SNR metrics
- build repeatable evaluation runs over public datasets such as COCO

See [Benchmarks](benchmarks.md) for smaller manual performance runs that stay
separate from dataset-scale evaluation.

## Dataset layout

Explicit dataset downloads belong under:

- `datasets/coco/`

Generated evaluation artifacts belong under:

- `tests/artifacts/corpus/`

Datasets and generated artifacts are not part of normal tests or CI.

## Downloading COCO validation images

Dataset download is always explicit. Nothing auto-downloads during:

- normal CTest runs
- `./scripts/test.sh`
- CI

Example:

```sh
python3 tools/download_dataset.py \
  --dataset coco-val \
  --output-root datasets/coco \
  --limit 100
```

This downloads the COCO `val2017` archive if needed, then extracts images in a
deterministic order. Re-running is resume-safe: already-downloaded archives and
already-extracted files are reused.

## Running corpus evaluation

Example single-protocol run:

```sh
python3 tools/corpus_eval.py \
  --rse ./cmake-build-debug/rse \
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
  --rse ./cmake-build-debug/rse \
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
- `--full` leaves the full discovered corpus available unless `--limit` is set
- `--jobs 1` keeps the current serial behavior
- `--jobs N` runs exactly `N` concurrent subprocess workers
- `--jobs auto` resolves conservatively with
  `max(1, min((os.cpu_count() or 1) - 1, 8, total_cases))`
- `--keep-outputs` preserves generated embedded or watermarked output images
  only; original dataset inputs are never copied into the artifact tree
- without `--keep-outputs`, embedded outputs are cleaned up after evaluation
- `--fail-fast` stops scheduling new work after the first runtime failure;
  already-running parallel workers are allowed to finish

## Parallel execution model

Corpus and probe parallelism is orchestration-only:

- one worker handles one independent image/protocol case
- each worker launches normal `rse` subprocesses
- workers never write SQLite directly
- the main process preserves deterministic row ordering and performs final
  CSV/JSON/SQLite export

Human-readable output reports the resolved execution mode:

- `execution_mode`
- `jobs_requested`
- `jobs_resolved`

Run-level timing metadata is also reported:

- `started_at`
- `finished_at`
- `wall_time_s`
- `throughput_cases_per_s`
- `estimated_serial_work_s`
- `estimated_speedup_vs_serial`
- `effective_parallelism`

`estimated_speedup_vs_serial` and `effective_parallelism` are conservative
derived ratios based on per-case embedding and payload-recovery timings. They
are useful for comparing serial and parallel orchestration modes, but they are
not exact protocol-level speedup measurements.

## Metrics

The harness collects per-image rows and aggregate summaries including:

- total images and total cases
- support rate
- expected rejection rate
- recovery failure rate
- runtime failure rate
- embedding success rate
- payload recovery rate
- average embedding time
- average payload-recovery time
- average, min, and max SNR when available
- output file size
- failure breakdown by reason
- failure breakdown by category
- dimension envelopes for successes, expected rejections, and recovery failures

## Classification model

Each evaluation case is classified as one of:

- `success`
- `expected_rejection`
- `recovery_failure`
- `runtime_failure`

Expected rejections are deterministic protocol/layout boundary cases rather than
engine instability. Current examples include Mosaic cases such as:

- `metadata cell too small`
- capacity-driven layout rejection
- unsupported geometry/layout constraints

Recovery failures are deterministic payload-recovery outcomes after embedding
has already succeeded. The current known example is:

- `payload_crc_mismatch`

Unexpected process failures, malformed outputs, invalid JSON, and subprocess
execution failures remain `runtime_failure`.

This distinction lets dataset-scale evaluation answer two different questions:

- where a protocol is supported
- where payload recovery is failing without a crash
- where the toolchain or runtime environment is unstable

For backward compatibility, row-level CSV and JSON exports still include the
machine-oriented fields `embed_ok` and `verify_ok`. They also include the
clearer aliases `embedding_succeeded`, `payload_recovered`, and
`failure_domain`.

Human-facing summaries interpret them as:

- embedding succeeded / failed
- payload recovery succeeded / failed / not applicable

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

The JSON report contains:

- run summary
- execution metadata
- per-protocol aggregates
- per-image rows

Execution metadata includes the run-level timing and throughput fields listed
above so serial and parallel corpus runs can be compared directly.

Per-image path fields:

- `input_file`: original dataset image path
- `output_file`: embedded or watermarked image path only
- `image`: source image basename

When multiple input images share the same basename stem, preserved embedded
artifacts add a short stable relative-path hash to the output filename so
parallel workers never target the same file.

For failed embed cases, `output_file` stays empty because no embedded output was
produced.

Support-boundary reporting includes per protocol:

- successful image dimensions
- expected-rejection image dimensions
- recovery-failure image dimensions
- runtime-failure image dimensions
- smallest successful image when available
- largest expected rejection when available
- width/height/min-dimension/megapixel envelopes for successes and expected rejections

The compact analysis JSON is intended for support-envelope reporting rather than
raw per-row inspection.

## SQLite export for DataGrip

Both corpus tools can also append runs to a SQLite database:

```sh
python3 tools/corpus_eval.py \
  --rse ./cmake-build-debug/rse \
  --input-dir datasets/coco/val2017 \
  --output-dir tests/artifacts/corpus/coco-classic \
  --protocol classic \
  --limit 100 \
  --sqlite tests/artifacts/corpus/coco-classic/results.sqlite
```

```sh
python3 tools/mosaic_support_probe.py \
  --rse ./cmake-build-debug/rse \
  --output-dir tests/artifacts/corpus/mosaic-support-probe \
  --sizes 1128,1136 \
  --sqlite tests/artifacts/corpus/mosaic-support-probe/results.sqlite
```

The database is append-only by default: each invocation adds a new `run_id`
rather than replacing previous results.

Current tables:

- `runs`
- `cases`
- `summaries`
- `failure_categories`

This schema is intentionally simple so tools like DataGrip can inspect it
directly without additional adapters.

Key SQLite columns now use the clearer analysis-oriented names:

- `cases.embedding_succeeded`
- `cases.payload_recovered`
- `cases.failure_domain`
- `runs.started_at`
- `runs.finished_at`
- `runs.wall_time_s`
- `runs.throughput_cases_per_s`
- `runs.estimated_serial_work_s`
- `runs.estimated_speedup_vs_serial`
- `runs.effective_parallelism`
- `summaries.embedding_success_rate`
- `summaries.payload_recovery_rate`
- `summaries.average_embedding_ms`
- `summaries.average_payload_recovery_ms`

To open the database in DataGrip:

1. Open DataGrip.
2. Add a new SQLite data source.
3. Point it at `tests/artifacts/corpus/.../results.sqlite`.
4. Inspect `runs`, `cases`, and `summaries`.

Example queries:

```sql
SELECT protocol, classification, COUNT(*)
FROM cases
GROUP BY protocol, classification;
```

```sql
SELECT protocol, AVG(snr), AVG(embed_ms), AVG(verify_ms)
FROM cases
WHERE classification = 'success'
GROUP BY protocol;
```

```sql
SELECT protocol, AVG(embedding_success_rate), AVG(payload_recovery_rate)
FROM summaries
GROUP BY protocol;
```

```sql
SELECT protocol, AVG(average_embedding_ms), AVG(average_payload_recovery_ms)
FROM summaries
GROUP BY protocol;
```

```sql
SELECT failure_category, COUNT(*), MIN(min_dimension), MAX(min_dimension)
FROM cases
WHERE protocol = 'mosaic'
GROUP BY failure_category;
```

```sql
SELECT protocol, COUNT(*)
FROM cases
WHERE classification = 'recovery_failure'
GROUP BY protocol;
```

## Mosaic support-envelope analysis

For Mosaic specifically, the harness now highlights:

- dominant expected-rejection category
- rejection-category dimension ranges
- largest expected rejection
- smallest successful image when available
- observed min-dimension threshold, if one appears in the sampled corpus

This is useful for characterizing protocol geometry/layout constraints without
treating them as engine failures.

Current corpus observations such as `metadata cell too small` should be read as
support-boundary results, not runtime instability.

Roadscript also now emits a clearer Mosaic preflight rejection for obviously
unsupported inputs below the current conservative guardrail:

- `mosaic input too small: current experimental Mosaic profile requires min dimension >= 1136 px`
- `mosaic input too small: minimum supported dimension is 1136 px for the current experimental Mosaic profile`

Corpus tooling classifies that message as:

- `classification = expected_rejection`
- `failure_category = mosaic_input_too_small`

## Deferred work

`--preflight-only` is intentionally deferred for now.

The current harness uses real `embed` + `verify` evaluation because that keeps
the support classification grounded in actual execution behavior. A preflight
path based on `rse info` would be useful later, but it needs a careful contract
so it does not overstate what is truly executable.

## Current scope

Current v0.1 evaluation focuses on:

- embed + verify flows
- structured metrics
- repeatable local evaluation over large corpora

Later work can add optional perturbation passes such as:

- JPEG recompression
- resize/crop transforms
- noise injection
- rotation experiments

Those are intentionally separate from the first conservative harness.

## Synthetic Mosaic support probe

When a real-world corpus shows Mosaic expected rejections rather than runtime
failures, use the synthetic support probe to characterize the current geometry
support threshold directly.

Example:

```sh
python3 tools/mosaic_support_probe.py \
  --rse ./cmake-build-debug/rse \
  --output-dir tests/artifacts/corpus/mosaic-support-probe \
  --sizes 320,640,1024 \
  --csv tests/artifacts/corpus/mosaic-support-probe/results.csv \
  --json tests/artifacts/corpus/mosaic-support-probe/results.json
```

Optional rectangle coverage:

```sh
python3 tools/mosaic_support_probe.py \
  --rse ./cmake-build-debug/rse \
  --output-dir tests/artifacts/corpus/mosaic-support-probe \
  --rectangles
```

The probe:

- generates deterministic synthetic images locally
- stores generated inputs under `<output-dir>/inputs/`
- stores embedded outputs under `<output-dir>/outputs/`
- classifies each size as `success`, `expected_rejection`, `recovery_failure`,
  or `runtime_failure`
- writes one row per synthetic image to CSV and JSON

The probe summary highlights:

- first successful size, if any
- largest expected rejection
- smallest successful minimum dimension
- dominant rejection category
- support / expected rejection / runtime failure rates
- embedding success rate
- payload recovery rate

Expected rejections such as `metadata cell too small` are support-boundary
results, not engine instability. A run with only expected rejections still
exits successfully.

For the current default Mosaic profile, the practical support threshold is
conservatively treated as:

- `min(width, height) >= 1136`

The probe is intentionally analysis-only. It does not make Mosaic more
permissive; it only characterizes the current experimental support envelope.
