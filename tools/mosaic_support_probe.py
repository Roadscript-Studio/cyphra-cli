#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
import time
from collections import Counter
from pathlib import Path
from typing import Any

from corpus.evaluation_support import (
    CLASSIFICATION_EXPECTED_REJECTION,
    CLASSIFICATION_RECOVERY_FAILURE,
    CLASSIFICATION_RUNTIME_FAILURE,
    CLASSIFICATION_SUCCESS,
    average_or_none,
    classification_status_label,
    classify_failure,
    command_succeeded,
    ensure_dir,
    failure_domain_for_classification,
    failure_reason,
    image_metrics,
    load_json_output,
    parse_jobs_value,
    resolve_job_count,
    run_command,
    run_indexed_work_items,
    execution_mode_for_jobs,
    write_csv,
    write_json,
)
from corpus.sqlite_export import export_run

DEFAULT_MESSAGE = "Roadscript Mosaic support probe"
DEFAULT_SQUARE_SIZES = [320, 480, 640, 800, 1024, 1200, 1400, 1600, 1800, 2048]
DEFAULT_RECTANGLES = [
    (640, 480),
    (800, 600),
    (1024, 768),
    (1280, 720),
    (1280, 960),
    (1600, 900),
    (1600, 1200),
    (1920, 1080),
    (2048, 1536),
]


def parse_args() -> argparse.Namespace:
    """Parse CLI options for the synthetic Mosaic support probe."""
    parser = argparse.ArgumentParser(
        description="Generate deterministic synthetic images to probe the current Mosaic support envelope.",
    )
    parser.add_argument("--rse", type=Path, required=True, help="Path to the rse executable.")
    parser.add_argument("--output-dir", type=Path, required=True, help="Directory for generated probe artifacts.")
    parser.add_argument("--message", default=DEFAULT_MESSAGE, help=f"Message payload to embed (default: {DEFAULT_MESSAGE!r}).")
    parser.add_argument("--step", type=float, default=30.0, help="Watermark step/SNR control (default: 30.0).")
    parser.add_argument(
        "--sizes",
        default=",".join(str(size) for size in DEFAULT_SQUARE_SIZES),
        help="Comma-separated square sizes to probe (default: 320,480,640,800,1024,1200,1400,1600,1800,2048).",
    )
    parser.add_argument("--rectangles", action="store_true", help="Also probe a small set of controlled rectangle sizes.")
    parser.add_argument("--csv", type=Path, default=None, help="Write detailed per-size results as CSV.")
    parser.add_argument("--json", dest="json_path", type=Path, default=None, help="Write combined config, rows, and summary as JSON.")
    parser.add_argument("--sqlite", type=Path, default=None, help="Append this probe run to a SQLite database for DataGrip analysis.")
    parser.add_argument(
        "--jobs",
        default="1",
        help="Execution worker count: a positive integer or 'auto' (default: 1).",
    )
    parser.add_argument("--keep-images", action="store_true", help="Keep generated synthetic inputs and embedded outputs.")
    parser.add_argument("--fail-fast", action="store_true", help="Stop on the first runtime failure.")
    return parser.parse_args()


def parse_square_sizes(raw_sizes: str) -> list[int]:
    """Parse a deduplicated, positive list of square probe sizes."""
    sizes: list[int] = []
    seen: set[int] = set()
    for chunk in raw_sizes.split(","):
        token = chunk.strip()
        if not token:
            continue
        try:
            size = int(token)
        except ValueError as exc:
            raise ValueError(f"invalid square size {token!r}") from exc
        if size <= 0:
            raise ValueError(f"square size must be greater than 0: {size}")
        if size not in seen:
            sizes.append(size)
            seen.add(size)
    if not sizes:
        raise ValueError("at least one square size is required")
    return sizes


def probe_dimensions(square_sizes: list[int], include_rectangles: bool) -> list[tuple[int, int]]:
    """Expand probe dimensions into square-only or square-plus-rectangle cases."""
    dimensions = [(size, size) for size in square_sizes]
    if include_rectangles:
        dimensions.extend(DEFAULT_RECTANGLES)
    return dimensions


def ppm_name(width: int, height: int) -> str:
    """Build a deterministic synthetic-input filename."""
    if width == height:
        return f"square_{width:04d}.ppm"
    return f"rect_{width:04d}x{height:04d}.ppm"


def png_name(width: int, height: int) -> str:
    """Build a deterministic embedded-output filename."""
    if width == height:
        return f"square_{width:04d}.png"
    return f"rect_{width:04d}x{height:04d}.png"


def write_synthetic_ppm(path: Path, width: int, height: int) -> None:
    """Generate a deterministic non-trivial probe image without extra dependencies."""
    ensure_dir(path.parent)
    with path.open("wb") as handle:
        handle.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        for y in range(height):
            row = bytearray()
            for x in range(width):
                r = (x * 255) // max(width - 1, 1)
                g = (y * 255) // max(height - 1, 1)
                checker = 28 if ((x // 32 + y // 32) % 2 == 0) else -28
                noise = ((x * 17) + (y * 31) + (width * 3) + (height * 5)) % 23 - 11
                b = max(0, min(255, ((r + g) // 2) + checker + noise))
                row.extend((r, g, b))
            handle.write(row)


def boundary_row(rows: list[dict[str, Any]], *, smallest: bool) -> dict[str, Any] | None:
    """Pick the smallest or largest probe row by image area."""
    candidates = [
        row for row in rows
        if isinstance(row.get("width"), int) and isinstance(row.get("height"), int)
    ]
    if not candidates:
        return None
    key_fn = lambda row: (row["width"] * row["height"], row["min_dimension"], row["max_dimension"])
    chosen = min(candidates, key=key_fn) if smallest else max(candidates, key=key_fn)
    return {
        "label": chosen.get("size_label"),
        "width": chosen.get("width"),
        "height": chosen.get("height"),
        "min_dimension": chosen.get("min_dimension"),
        "max_dimension": chosen.get("max_dimension"),
        "megapixels": chosen.get("megapixels"),
        "classification": chosen.get("classification"),
        "failure_category": chosen.get("failure_category"),
        "failure_reason": chosen.get("failure_reason"),
    }


def numeric_stats(rows: list[dict[str, Any]], key: str) -> dict[str, float | None]:
    """Compute min/max/average for a numeric probe field."""
    values = [float(row[key]) for row in rows if isinstance(row.get(key), (int, float))]
    if not values:
        return {"min": None, "max": None, "average": None}
    return {
        "min": min(values),
        "max": max(values),
        "average": sum(values) / len(values),
    }


def envelope_stats(rows: list[dict[str, Any]]) -> dict[str, dict[str, float | None]]:
    """Summarize the geometric envelope of a probe result set."""
    return {
        "width": numeric_stats(rows, "width"),
        "height": numeric_stats(rows, "height"),
        "min_dimension": numeric_stats(rows, "min_dimension"),
        "megapixels": numeric_stats(rows, "megapixels"),
    }


def dominant_rejection_category(rows: list[dict[str, Any]]) -> str | None:
    """Return the most common expected-rejection category in probe results."""
    counts = Counter(
        str(row.get("failure_category", "unknown"))
        for row in rows
        if row.get("classification") == CLASSIFICATION_EXPECTED_REJECTION and row.get("failure_category") not in ("", "none")
    )
    if not counts:
        return None
    return max(counts.items(), key=lambda item: (item[1], item[0]))[0]


def first_successful_case(rows: list[dict[str, Any]]) -> dict[str, Any] | None:
    """Return the first successful probe row in evaluation order."""
    for row in rows:
        if row.get("classification") == CLASSIFICATION_SUCCESS:
            return boundary_row([row], smallest=True)
    return None


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    """Aggregate per-size probe results into a support-envelope summary."""
    success_rows = [row for row in rows if row["classification"] == CLASSIFICATION_SUCCESS]
    rejection_rows = [row for row in rows if row["classification"] == CLASSIFICATION_EXPECTED_REJECTION]
    recovery_failure_rows = [row for row in rows if row["classification"] == CLASSIFICATION_RECOVERY_FAILURE]
    runtime_failure_rows = [row for row in rows if row["classification"] == CLASSIFICATION_RUNTIME_FAILURE]
    embedding_success_rows = [row for row in rows if row["embedding_succeeded"]]
    payload_recovery_rows = [row for row in rows if row["payload_recovered"]]
    embed_times = [float(row["embed_time_ms"]) for row in rows if row["embed_time_ms"] is not None]
    verify_times = [float(row["verify_time_ms"]) for row in rows if row["verify_time_ms"] is not None]
    snr_values = [float(row["snr"]) for row in rows if row["snr"] is not None]
    total = len(rows)

    return {
        "total_cases": total,
        "support_count": len(success_rows),
        "expected_rejection_count": len(rejection_rows),
        "recovery_failure_count": len(recovery_failure_rows),
        "runtime_failure_count": len(runtime_failure_rows),
        "embedding_success_count": len(embedding_success_rows),
        "payload_recovery_count": len(payload_recovery_rows),
        "support_rate": (len(success_rows) / total) if total else 0.0,
        "expected_rejection_rate": (len(rejection_rows) / total) if total else 0.0,
        "recovery_failure_rate": (len(recovery_failure_rows) / total) if total else 0.0,
        "runtime_failure_rate": (len(runtime_failure_rows) / total) if total else 0.0,
        "embedding_success_rate": (len(embedding_success_rows) / total) if total else 0.0,
        "payload_recovery_rate": (len(payload_recovery_rows) / total) if total else 0.0,
        "average_embed_time_ms": average_or_none(embed_times),
        "average_verify_time_ms": average_or_none(verify_times),
        "average_embedding_time_ms": average_or_none(embed_times),
        "average_payload_recovery_time_ms": average_or_none(verify_times),
        "average_snr": average_or_none(snr_values),
        "min_snr": min(snr_values) if snr_values else None,
        "max_snr": max(snr_values) if snr_values else None,
        "dominant_rejection_category": dominant_rejection_category(rejection_rows),
        "first_successful_size": first_successful_case(rows),
        "largest_expected_rejection": boundary_row(rejection_rows, smallest=False),
        "smallest_successful_image": boundary_row(success_rows, smallest=True),
        "success_envelope": envelope_stats(success_rows),
        "expected_rejection_envelope": envelope_stats(rejection_rows),
        "recovery_failure_envelope": envelope_stats(recovery_failure_rows),
        "runtime_failure_envelope": envelope_stats(runtime_failure_rows),
        "failure_category_breakdown": dict(
            sorted(
                Counter(
                    str(row.get("failure_category", "unknown"))
                    for row in rows
                    if row.get("failure_category") not in ("", "none")
                ).items()
            )
        ),
    }


def evaluate_probe_case(
    rse_path: Path,
    inputs_dir: Path,
    outputs_dir: Path,
    width: int,
    height: int,
    message: str,
    message_bytes: int,
    step: float,
    keep_images: bool,
) -> dict[str, Any]:
    """Run one synthetic probe case and normalize the result row."""
    input_path = inputs_dir / ppm_name(width, height)
    output_path = outputs_dir / png_name(width, height)
    write_synthetic_ppm(input_path, width, height)
    ensure_dir(output_path.parent)

    metrics = image_metrics(width, height)
    label = f"{width}x{height}"

    embed_command = [
        str(rse_path),
        "embed",
        "--protocol",
        "mosaic",
        "--in",
        str(input_path),
        "--out",
        str(output_path),
        "--msg-block",
        message,
        "--step",
        str(step),
        "--json",
    ]
    embed_start = time.perf_counter()
    embed_result = run_command(embed_command)
    embed_elapsed_ms = (time.perf_counter() - embed_start) * 1000.0
    embed_payload = load_json_output(embed_result.stdout)

    row: dict[str, Any] = {
        "size_label": label,
        "input_file": str(input_path),
        "output_file": str(output_path),
        "message_bytes": message_bytes,
        "step": step,
        "embedding_succeeded": command_succeeded(embed_result, embed_payload, "OK"),
        "embed_ok": command_succeeded(embed_result, embed_payload, "OK"),
        "payload_recovered": False,
        "verify_ok": False,
        "embed_exit_code": embed_result.returncode,
        "verify_exit_code": None,
        "embed_time_ms": embed_payload.get("embed_time_ms") if embed_payload is not None else round(embed_elapsed_ms, 2),
        "verify_time_ms": None,
        "embed_wall_time_ms": round(embed_elapsed_ms, 2),
        "verify_wall_time_ms": None,
        "snr": embed_payload.get("snr") if embed_payload is not None else None,
        "output_file_size": output_path.stat().st_size if output_path.exists() else None,
        "classification": CLASSIFICATION_SUCCESS,
        "failure_domain": "none",
        "failure_category": "none",
        "failure_reason": "none",
        **metrics,
    }

    if not row["embedding_succeeded"]:
        row["failure_reason"] = failure_reason(embed_payload, embed_result.stderr)
        row["classification"], row["failure_category"] = classify_failure(row["failure_reason"], stage="embed")
        row["failure_domain"] = failure_domain_for_classification(row["classification"])
        if output_path.exists() and not keep_images:
            output_path.unlink()
        if input_path.exists() and not keep_images:
            input_path.unlink()
        return row

    verify_command = [
        str(rse_path),
        "verify",
        "--protocol",
        "mosaic",
        "--in",
        str(output_path),
        "--step",
        str(step),
        "--json",
    ]
    verify_start = time.perf_counter()
    verify_result = run_command(verify_command)
    verify_elapsed_ms = (time.perf_counter() - verify_start) * 1000.0
    verify_payload = load_json_output(verify_result.stdout)

    row["verify_exit_code"] = verify_result.returncode
    row["payload_recovered"] = command_succeeded(verify_result, verify_payload, "PASS")
    row["verify_ok"] = row["payload_recovered"]
    row["verify_time_ms"] = verify_payload.get("verify_time_ms") if verify_payload is not None else round(verify_elapsed_ms, 2)
    row["verify_wall_time_ms"] = round(verify_elapsed_ms, 2)
    row["decoded_message"] = verify_payload.get("decoded_message") if verify_payload is not None else None
    row["decoded_match"] = (
        row["decoded_message"] == message if row["decoded_message"] is not None else None
    )
    row["failure_reason"] = "none" if row["payload_recovered"] else failure_reason(verify_payload, verify_result.stderr)
    row["classification"], row["failure_category"] = classify_failure(
        row["failure_reason"],
        stage="success" if row["payload_recovered"] else "verify",
    )
    row["failure_domain"] = failure_domain_for_classification(row["classification"])
    row["output_file_size"] = output_path.stat().st_size if output_path.exists() else None

    if output_path.exists() and not keep_images:
        output_path.unlink()
    if input_path.exists() and not keep_images:
        input_path.unlink()

    return row


def execute_indexed_probe_case(work_item: dict[str, Any]) -> dict[str, Any]:
    """Dispatch one synthetic probe case for optional threaded execution."""
    return evaluate_probe_case(
        rse_path=Path(work_item["rse_path"]),
        inputs_dir=Path(work_item["inputs_dir"]),
        outputs_dir=Path(work_item["outputs_dir"]),
        width=int(work_item["width"]),
        height=int(work_item["height"]),
        message=str(work_item["message"]),
        message_bytes=int(work_item["message_bytes"]),
        step=float(work_item["step"]),
        keep_images=bool(work_item["keep_images"]),
    )


def main() -> int:
    """Run the synthetic Mosaic support-envelope probe."""
    args = parse_args()

    if not args.rse.exists():
        print(f"[FAIL] rse binary not found: {args.rse}", file=sys.stderr)
        return 1
    try:
        requested_jobs = parse_jobs_value(args.jobs)
    except ValueError as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    try:
        square_sizes = parse_square_sizes(args.sizes)
    except ValueError as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    if args.step <= 0 or not (args.step < float("inf")):
        print("[FAIL] --step must be a finite positive value", file=sys.stderr)
        return 1

    dimensions = probe_dimensions(square_sizes, args.rectangles)
    message_bytes = len(args.message.encode("utf-8"))
    if not dimensions:
        print("[FAIL] no probe dimensions selected", file=sys.stderr)
        return 1

    ensure_dir(args.output_dir)
    inputs_dir = args.output_dir / "inputs"
    outputs_dir = args.output_dir / "outputs"
    csv_path = args.csv or (args.output_dir / "results.csv")
    json_path = args.json_path or (args.output_dir / "results.json")
    jobs_requested_text = "auto" if requested_jobs == "auto" else str(requested_jobs)
    jobs_resolved = resolve_job_count(requested_jobs, len(dimensions))
    execution_mode = execution_mode_for_jobs(jobs_resolved)

    print("Roadscript Mosaic support probe")
    print(f"  rse              : {args.rse}")
    print(f"  output_dir       : {args.output_dir}")
    print(f"  input_dir        : {inputs_dir}")
    print(f"  output_images    : {outputs_dir}")
    print(f"  message_bytes    : {message_bytes}")
    print(f"  step             : {args.step}")
    print(f"  cases            : {len(dimensions)}")
    print(f"  execution_mode   : {execution_mode}")
    print(f"  jobs_requested   : {jobs_requested_text}")
    print(f"  jobs_resolved    : {jobs_resolved}")
    print(f"  rectangles       : {'yes' if args.rectangles else 'no'}")
    print(f"  keep_images      : {'yes' if args.keep_images else 'no'}")
    print(f"  csv              : {csv_path}")
    print(f"  json             : {json_path}")
    if args.sqlite is not None:
        print(f"  sqlite           : {args.sqlite}")

    work_items = [
        {
            "case_index": index,
            "rse_path": str(args.rse),
            "inputs_dir": str(inputs_dir),
            "outputs_dir": str(outputs_dir),
            "width": width,
            "height": height,
            "message": args.message,
            "message_bytes": message_bytes,
            "step": args.step,
            "keep_images": args.keep_images,
        }
        for index, (width, height) in enumerate(dimensions)
    ]
    rows, stopped_early = run_indexed_work_items(
        work_items,
        execute_indexed_probe_case,
        jobs_resolved=jobs_resolved,
        fail_fast=args.fail_fast,
        should_stop_on_result=lambda row: row["classification"] == CLASSIFICATION_RUNTIME_FAILURE,
    )
    runtime_failure_rows: list[dict[str, Any]] = []

    for row in rows:

        status = classification_status_label(row["classification"])
        print(
            f"  [{status}] size={row['size_label']} "
            f"embedding={'succeeded' if row['embedding_succeeded'] else 'failed'} "
            f"payload_recovery={'succeeded' if row['payload_recovered'] else ('not_applicable' if row['classification'] == CLASSIFICATION_EXPECTED_REJECTION else 'failed')} "
            f"snr={row['snr'] if row['snr'] is not None else 'n/a'} "
            f"classification={row['classification']} "
            f"failure_domain={row['failure_domain']} "
            f"category={row['failure_category']} "
            f"reason={row['failure_reason']}"
        )

        if row["classification"] == CLASSIFICATION_RUNTIME_FAILURE:
            runtime_failure_rows.append(row)

    summary = summarize(rows)
    payload = {
        "config": {
            "rse": str(args.rse),
            "output_dir": str(args.output_dir),
            "message": args.message,
            "message_bytes": message_bytes,
            "step": args.step,
            "sizes": square_sizes,
            "rectangles": args.rectangles,
            "rectangle_sizes": DEFAULT_RECTANGLES if args.rectangles else [],
            "keep_images": args.keep_images,
            "fail_fast": args.fail_fast,
            "jobs_requested": jobs_requested_text,
            "jobs_resolved": jobs_resolved,
            "execution_mode": execution_mode,
        },
        "rows": rows,
        "summary": summary,
    }

    write_csv(csv_path, rows)
    write_json(json_path, payload)
    sqlite_run_id: str | None = None
    if args.sqlite is not None:
        sqlite_run_id = export_run(
            args.sqlite,
            tool="mosaic_support_probe",
            run_meta={
                "rse_path": str(args.rse),
                "input_dir": None,
                "output_dir": str(args.output_dir),
                "protocol_mode": "mosaic",
                "message_bytes": message_bytes,
                "step": args.step,
                "limit_value": len(dimensions),
                "seed": None,
                "notes": (
                    f"square_sizes={','.join(str(size) for size in square_sizes)}; "
                    f"execution_mode={execution_mode}; "
                    f"jobs_requested={jobs_requested_text}; "
                    f"jobs_resolved={jobs_resolved}; "
                    f"rectangles={'yes' if args.rectangles else 'no'}; "
                    f"keep_images={'yes' if args.keep_images else 'no'}; "
                    f"fail_fast={'yes' if args.fail_fast else 'no'}; "
                    f"stopped_early={'yes' if stopped_early else 'no'}"
                ),
            },
            rows=rows,
            summaries_by_protocol={"mosaic": summary},
        )

    print("\nSummary")
    print(f"  execution_mode             : {execution_mode}")
    print(f"  jobs_requested             : {jobs_requested_text}")
    print(f"  jobs_resolved              : {jobs_resolved}")
    print(f"  total_cases                 : {summary['total_cases']}")
    print(f"  support_rate                : {summary['support_rate']:.3f}")
    print(f"  expected_rejection_rate     : {summary['expected_rejection_rate']:.3f}")
    print(f"  recovery_failure_rate       : {summary['recovery_failure_rate']:.3f}")
    print(f"  runtime_failure_rate        : {summary['runtime_failure_rate']:.3f}")
    print(f"  embedding_success_rate      : {summary['embedding_success_rate']:.3f}")
    print(f"  payload_recovery_rate       : {summary['payload_recovery_rate']:.3f}")
    print(f"  dominant_rejection_category : {summary['dominant_rejection_category']}")
    first_success = summary.get("first_successful_size")
    largest_rejection = summary.get("largest_expected_rejection")
    smallest_success = summary.get("smallest_successful_image")
    if first_success is not None:
        print(f"  first_successful_size       : {first_success.get('label')}")
    else:
        print("  first_successful_size       : none observed")
    if largest_rejection is not None:
        print(f"  largest_expected_rejection  : {largest_rejection.get('label')}")
    else:
        print("  largest_expected_rejection  : none observed")
    if smallest_success is not None:
        print(f"  smallest_success_min_dim    : {smallest_success.get('min_dimension')}")
    else:
        print("  smallest_success_min_dim    : none observed")
    print(f"  csv                         : {csv_path}")
    print(f"  json                        : {json_path}")
    if args.sqlite is not None:
        print(f"  sqlite                      : {args.sqlite}")
        print(f"  sqlite_run_id               : {sqlite_run_id}")
    if stopped_early:
        print("  stop_reason                 : fail-fast triggered after a runtime failure")

    return 1 if runtime_failure_rows else 0


if __name__ == "__main__":
    raise SystemExit(main())
