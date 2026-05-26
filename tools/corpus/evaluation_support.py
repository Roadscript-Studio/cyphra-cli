from __future__ import annotations

import csv
import json
import os
import random
import subprocess
from concurrent.futures import FIRST_COMPLETED, Future, ThreadPoolExecutor, wait
from collections.abc import Iterable
from pathlib import Path
from typing import Any

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp", ".ppm", ".pgm", ".pbm"}
CLASSIFICATION_SUCCESS = "success"
CLASSIFICATION_EXPECTED_REJECTION = "expected_rejection"
CLASSIFICATION_RECOVERY_FAILURE = "recovery_failure"
CLASSIFICATION_RUNTIME_FAILURE = "runtime_failure"

EXPECTED_REJECTION_PATTERNS: tuple[tuple[str, str], ...] = (
    ("mosaic input too small", "mosaic_input_too_small"),
    ("minimum supported dimension", "mosaic_input_too_small"),
    ("metadata cell too small", "metadata_cell_too_small"),
    ("capacity", "capacity_limit"),
    ("geometry", "geometry_limit"),
    ("unsupported", "unsupported_constraint"),
    ("too small", "geometry_limit"),
    ("layout", "layout_constraint"),
)
PAYLOAD_RECOVERY_PATTERNS: tuple[tuple[str, str], ...] = (
    ("crc_mismatch", "payload_crc_mismatch"),
    ("crc mismatch", "payload_crc_mismatch"),
    ("final crc", "payload_crc_mismatch"),
    ("payload crc", "payload_crc_mismatch"),
    ("payload mismatch", "payload_recovery_failure"),
    ("verification failed", "payload_recovery_failure"),
    ("recovery failed", "payload_recovery_failure"),
    ("decode failed", "payload_recovery_failure"),
)
RUNTIME_FAILURE_PATTERNS: tuple[tuple[str, str], ...] = (
    ("jsondecodeerror", "invalid_json_output"),
    ("invalid json", "invalid_json_output"),
    ("traceback", "subprocess_failure"),
    ("exception", "subprocess_failure"),
    ("timed out", "subprocess_timeout"),
    ("permission denied", "io_failure"),
    ("no such file", "io_failure"),
    ("not found", "io_failure"),
    ("segmentation fault", "subprocess_crash"),
    ("abort", "subprocess_crash"),
    ("killed", "subprocess_crash"),
)


def ensure_dir(path: Path) -> None:
    """Create a directory tree when a tool needs an artifact destination."""
    path.mkdir(parents=True, exist_ok=True)


def list_images(input_dir: Path) -> list[Path]:
    """Return corpus images in deterministic relative-path order."""
    return sorted(
        (
            path for path in input_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS
        ),
        key=lambda path: path.relative_to(input_dir).as_posix().lower(),
    )


def select_paths(paths: Iterable[Path], limit: int | None, shuffle: bool, seed: int) -> list[Path]:
    """Apply deterministic shuffle and optional truncation to a path list."""
    ordered = list(paths)
    if shuffle:
        rng = random.Random(seed)
        rng.shuffle(ordered)
    if limit is not None:
        ordered = ordered[:limit]
    return ordered


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    """Write a schema-flexible CSV that preserves every discovered row field."""
    ensure_dir(path.parent)
    fieldnames: list[str] = []
    for row in rows:
        for key in row.keys():
            if key not in fieldnames:
                fieldnames.append(key)

    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def write_json(path: Path, payload: Any) -> None:
    """Write a human-readable JSON artifact for later inspection."""
    ensure_dir(path.parent)
    path.write_text(json.dumps(payload, indent=2, sort_keys=False) + "\n", encoding="utf-8")


def average_or_none(values: Iterable[float]) -> float | None:
    """Return an arithmetic mean when values are present, otherwise None."""
    total = 0.0
    count = 0
    for value in values:
        total += float(value)
        count += 1
    if count == 0:
        return None
    return total / count


def load_json_output(text: str) -> dict[str, Any] | None:
    """Parse CLI JSON output when a subprocess emits a top-level object."""
    try:
        payload = json.loads(text)
    except json.JSONDecodeError:
        return None
    return payload if isinstance(payload, dict) else None


def run_command(command: list[str]) -> subprocess.CompletedProcess[str]:
    """Run a tool command without shell expansion and capture both streams."""
    return subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
    )


def command_succeeded(
    process_result: subprocess.CompletedProcess[str],
    payload: dict[str, Any] | None,
    expected_status: str,
) -> bool:
    """Return whether a JSON-mode CLI subprocess completed with the expected status."""
    return (
        process_result.returncode == 0
        and payload is not None
        and payload.get("status") == expected_status
    )


def failure_reason(payload: dict[str, Any] | None, stderr_text: str) -> str:
    """Extract the most specific failure string available from JSON or stderr."""
    if payload is not None:
        for key in ("error", "failure_reason", "reason"):
            value = payload.get(key)
            if isinstance(value, str) and value:
                return value
    stderr = stderr_text.strip()
    return stderr if stderr else "unknown"


def classify_failure(reason: str, *, stage: str) -> tuple[str, str]:
    """Classify a failure by support-envelope, payload-recovery, or runtime domain."""
    normalized = reason.strip().lower()
    if normalized in ("", "none"):
        return CLASSIFICATION_SUCCESS, "none"

    if stage == "embed":
        for pattern, category in EXPECTED_REJECTION_PATTERNS:
            if pattern in normalized:
                return CLASSIFICATION_EXPECTED_REJECTION, category

    if stage == "verify":
        for pattern, category in PAYLOAD_RECOVERY_PATTERNS:
            if pattern in normalized:
                return CLASSIFICATION_RECOVERY_FAILURE, category
        for pattern, category in RUNTIME_FAILURE_PATTERNS:
            if pattern in normalized:
                return CLASSIFICATION_RUNTIME_FAILURE, category
        return CLASSIFICATION_RECOVERY_FAILURE, "payload_recovery_failure"

    return CLASSIFICATION_RUNTIME_FAILURE, "runtime_failure"


def failure_domain_for_classification(classification: str) -> str:
    """Map a row classification to a stable higher-level failure domain label."""
    return {
        CLASSIFICATION_SUCCESS: "none",
        CLASSIFICATION_EXPECTED_REJECTION: "support_envelope",
        CLASSIFICATION_RECOVERY_FAILURE: "payload_recovery",
        CLASSIFICATION_RUNTIME_FAILURE: "runtime",
    }.get(classification, "runtime")


def classification_status_label(classification: str) -> str:
    """Return a concise human-facing status label for terminal summaries."""
    return {
        CLASSIFICATION_SUCCESS: "PASS",
        CLASSIFICATION_EXPECTED_REJECTION: "SKIP",
        CLASSIFICATION_RECOVERY_FAILURE: "RECOVERY-FAIL",
        CLASSIFICATION_RUNTIME_FAILURE: "RUNTIME-FAIL",
    }.get(classification, "FAIL")


def image_metrics(width: int, height: int) -> dict[str, Any]:
    """Compute basic geometry fields shared across corpus and probe reports."""
    min_dimension = min(width, height)
    max_dimension = max(width, height)
    aspect_ratio = round(width / height, 6) if height > 0 else None
    megapixels = round((width * height) / 1_000_000.0, 6)
    return {
        "width": width,
        "height": height,
        "min_dimension": min_dimension,
        "max_dimension": max_dimension,
        "aspect_ratio": aspect_ratio,
        "megapixels": megapixels,
    }


def parse_jobs_value(raw_jobs: str) -> str | int:
    """Parse a jobs setting as a positive integer or the literal ``auto``."""
    normalized = raw_jobs.strip().lower()
    if normalized == "auto":
        return "auto"
    try:
        jobs = int(normalized)
    except ValueError as exc:
        raise ValueError(f"invalid --jobs value {raw_jobs!r}: expected a positive integer or 'auto'") from exc
    if jobs <= 0:
        raise ValueError(f"invalid --jobs value {raw_jobs!r}: expected a positive integer or 'auto'")
    return jobs


def resolve_job_count(requested_jobs: str | int, total_cases: int) -> int:
    """Resolve a conservative worker count for a fixed set of independent cases."""
    if total_cases <= 0:
        return 1
    if requested_jobs == "auto":
        cpu_count = os.cpu_count() or 1
        return max(1, min(cpu_count - 1, 8, total_cases))
    return max(1, min(int(requested_jobs), total_cases))


def execution_mode_for_jobs(jobs_resolved: int) -> str:
    """Return a stable execution-mode label for terminal and JSON summaries."""
    return "serial" if jobs_resolved == 1 else "parallel"


def run_indexed_work_items(
    work_items: list[dict[str, Any]],
    worker_fn,
    *,
    jobs_resolved: int,
    fail_fast: bool,
    should_stop_on_result,
) -> tuple[list[dict[str, Any]], bool]:
    """Execute indexed work items while preserving deterministic result ordering.

    Workers return plain row dictionaries. The caller remains responsible for
    final CSV/JSON/SQLite export so SQLite never sees concurrent writers.
    """
    if jobs_resolved <= 1:
        ordered_results: list[dict[str, Any]] = []
        stopped_early = False
        for work_item in work_items:
            row = worker_fn(work_item)
            ordered_results.append(row)
            if fail_fast and should_stop_on_result(row):
                stopped_early = True
                break
        return ordered_results, stopped_early

    results_by_index: dict[int, dict[str, Any]] = {}
    stopped_early = False
    next_work_index = 0
    in_flight: dict[Future[dict[str, Any]], int] = {}

    def submit_next(executor: ThreadPoolExecutor) -> bool:
        nonlocal next_work_index
        if next_work_index >= len(work_items):
            return False
        work_item = work_items[next_work_index]
        future = executor.submit(worker_fn, work_item)
        in_flight[future] = int(work_item["case_index"])
        next_work_index += 1
        return True

    with ThreadPoolExecutor(max_workers=jobs_resolved) as executor:
        while len(in_flight) < jobs_resolved and submit_next(executor):
            pass

        while in_flight:
            completed, _ = wait(set(in_flight), return_when=FIRST_COMPLETED)
            for future in completed:
                case_index = in_flight.pop(future)
                row = future.result()
                results_by_index[case_index] = row
                if fail_fast and should_stop_on_result(row):
                    stopped_early = True

            if stopped_early:
                for future in list(in_flight):
                    future.cancel()
                break

            while len(in_flight) < jobs_resolved and submit_next(executor):
                pass

    ordered_indexes = sorted(results_by_index)
    return [results_by_index[index] for index in ordered_indexes], stopped_early
