#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import sys
import time
from collections import Counter, defaultdict
from datetime import datetime, timezone
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
    list_images,
    load_json_output,
    parse_jobs_value,
    resolve_job_count,
    run_command,
    run_indexed_work_items,
    select_paths,
    execution_mode_for_jobs,
    write_csv,
    write_json,
)
from corpus.sqlite_export import export_run

DEFAULT_MESSAGE = "roadscript-corpus-eval"
DEFAULT_QUICK_LIMIT = 25


def parse_args() -> argparse.Namespace:
    """Parse corpus-evaluation CLI arguments."""
    parser = argparse.ArgumentParser(
        description="Evaluate Cyphra CLI behavior and performance across a local image corpus.",
    )
    parser.add_argument("--rse", type=Path, required=True, help="Path to the rse executable.")
    parser.add_argument("--input-dir", type=Path, required=True, help="Directory of input images.")
    parser.add_argument("--output-dir", type=Path, required=True, help="Directory for generated corpus artifacts.")
    parser.add_argument(
        "--protocol",
        choices=["classic", "mosaic", "both"],
        default="classic",
        help="Protocol to evaluate (default: classic).",
    )
    parser.add_argument("--limit", type=int, default=None, help="Maximum number of input images to process.")
    parser.add_argument("--quick", action="store_true", help=f"Apply a small default limit ({DEFAULT_QUICK_LIMIT}) if --limit is not set.")
    parser.add_argument("--full", action="store_true", help="Run the full available image set unless --limit is set.")
    parser.add_argument("--shuffle", action="store_true", help="Shuffle image order deterministically with --seed.")
    parser.add_argument("--seed", type=int, default=1337, help="Seed used with --shuffle (default: 1337).")
    parser.add_argument("--message", default=DEFAULT_MESSAGE, help="Message payload to embed.")
    parser.add_argument("--step", type=float, default=30.0, help="Watermark step/SNR control (default: 30.0).")
    parser.add_argument("--csv", type=Path, default=None, help="Write detailed per-image results as CSV.")
    parser.add_argument("--json", dest="json_path", type=Path, default=None, help="Write combined summary and rows as JSON.")
    parser.add_argument("--sqlite", type=Path, default=None, help="Append this run to a SQLite database for local analysis.")
    parser.add_argument("--failed-manifest", type=Path, default=None, help="Write failed-case rows as JSON.")
    parser.add_argument("--analysis-json", type=Path, default=None, help="Write a compact support-envelope analysis report as JSON.")
    parser.add_argument("--support-summary-json", type=Path, default=None, help="Write protocol support-boundary summary as JSON.")
    parser.add_argument(
        "--jobs",
        default="1",
        help="Execution worker count: a positive integer or 'auto' (default: 1).",
    )
    parser.add_argument("--keep-outputs", action="store_true", help="Keep generated embedded images.")
    parser.add_argument("--fail-fast", action="store_true", help="Stop after the first embed or verify failure.")
    return parser.parse_args()


def selected_protocols(protocol_arg: str) -> list[str]:
    """Expand the protocol mode into the concrete protocol list to evaluate."""
    return ["classic", "mosaic"] if protocol_arg == "both" else [protocol_arg]


def effective_limit(args: argparse.Namespace) -> int | None:
    """Resolve the effective image limit from quick/full/default options."""
    if args.limit is not None:
        return args.limit
    if args.quick and not args.full:
        return DEFAULT_QUICK_LIMIT
    return None


def execution_metadata_payload(
    *,
    requested_jobs: str,
    resolved_jobs: int,
    timing: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Build a stable execution descriptor for JSON reporting."""
    return {
        "jobs_requested": requested_jobs,
        "jobs_resolved": resolved_jobs,
        "execution_mode": execution_mode_for_jobs(resolved_jobs),
        **(timing or {}),
    }


def current_utc_timestamp() -> str:
    """Return an ISO-8601 UTC timestamp for run-level reporting."""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def run_timing_summary(
    rows: list[dict[str, Any]],
    *,
    total_cases: int,
    started_at: str,
    finished_at: str,
    wall_time_s: float,
) -> dict[str, Any]:
    """Build conservative run-level timing and throughput metrics."""
    estimated_serial_work_s = 0.0
    found_timing_sample = False
    for row in rows:
        # Prefer per-case wall timings because they include orchestration overhead
        # such as subprocess startup and image I/O. Fall back to engine-reported
        # timings when a wall field is unavailable.
        for preferred_field, fallback_field in (
            ("embed_wall_time_ms", "embed_time_ms"),
            ("verify_wall_time_ms", "verify_time_ms"),
        ):
            value = row.get(preferred_field)
            if not isinstance(value, (int, float)):
                value = row.get(fallback_field)
            if isinstance(value, (int, float)):
                estimated_serial_work_s += float(value) / 1000.0
                found_timing_sample = True

    throughput_cases_per_s = (total_cases / wall_time_s) if wall_time_s > 0.0 else None
    estimated_serial_value = estimated_serial_work_s if found_timing_sample else None
    estimated_speedup_vs_serial = (
        (estimated_serial_value / wall_time_s)
        if estimated_serial_value is not None and wall_time_s > 0.0
        else None
    )

    return {
        "started_at": started_at,
        "finished_at": finished_at,
        "wall_time_s": round(wall_time_s, 6),
        "throughput_cases_per_s": round(throughput_cases_per_s, 6) if throughput_cases_per_s is not None else None,
        "estimated_serial_work_s": round(estimated_serial_value, 6) if estimated_serial_value is not None else None,
        "estimated_speedup_vs_serial": round(estimated_speedup_vs_serial, 6) if estimated_speedup_vs_serial is not None else None,
        "effective_parallelism": round(estimated_speedup_vs_serial, 6) if estimated_speedup_vs_serial is not None else None,
    }


def output_filename_for_image(
    input_dir: Path,
    image_path: Path,
    duplicate_stems: set[str],
) -> str:
    """Build a stable embedded-output filename for one source image.

    Most corpus images keep the historical ``<stem>.png`` layout. When multiple
    source images share the same basename stem, add a short relative-path hash
    so parallel workers never target the same embedded artifact.
    """
    if image_path.stem not in duplicate_stems:
        return f"{image_path.stem}.png"

    relative_path = image_path.relative_to(input_dir).as_posix().lower()
    stable_suffix = hashlib.sha1(relative_path.encode("utf-8")).hexdigest()[:8]
    return f"{image_path.stem}-{stable_suffix}.png"


def embedded_output_path_for_image(output_root: Path, protocol: str, output_filename: str) -> Path:
    """Return the embedded output path for a preserved corpus artifact."""
    protocol_root = output_root / protocol / "embedded"
    return protocol_root / output_filename


def duplicate_stems_for_paths(image_paths: list[Path]) -> set[str]:
    """Return the set of source stems that appear more than once."""
    stem_counts = Counter(path.stem for path in image_paths)
    return {stem for stem, count in stem_counts.items() if count > 1}


def dimension_distribution(rows: list[dict[str, Any]]) -> dict[str, int]:
    """Count dimension occurrences for envelope summaries."""
    counts: Counter[str] = Counter()
    for row in rows:
        width = row.get("width")
        height = row.get("height")
        if isinstance(width, int) and isinstance(height, int):
            counts[f"{width}x{height}"] += 1
    return dict(sorted(counts.items()))


def boundary_image(rows: list[dict[str, Any]], *, smallest: bool) -> dict[str, Any] | None:
    """Pick the smallest or largest image row by area for summary reporting."""
    candidates = [
        row for row in rows
        if isinstance(row.get("width"), int) and isinstance(row.get("height"), int)
    ]
    if not candidates:
        return None

    key_fn = lambda row: (row["width"] * row["height"], row["width"], row["height"])
    chosen = min(candidates, key=key_fn) if smallest else max(candidates, key=key_fn)
    return {
        "image_name": chosen.get("image_name"),
        "relative_image": chosen.get("relative_image"),
        "width": chosen.get("width"),
        "height": chosen.get("height"),
        "min_dimension": chosen.get("min_dimension"),
        "max_dimension": chosen.get("max_dimension"),
        "megapixels": chosen.get("megapixels"),
        "classification": chosen.get("classification"),
        "failure_reason": chosen.get("failure_reason"),
    }


def numeric_stats(rows: list[dict[str, Any]], key: str) -> dict[str, float | None]:
    """Compute min/max/average for a numeric row field when present."""
    values = [float(row[key]) for row in rows if isinstance(row.get(key), (int, float))]
    if not values:
        return {"min": None, "max": None, "average": None}
    return {
        "min": min(values),
        "max": max(values),
        "average": sum(values) / len(values),
    }


def envelope_stats(rows: list[dict[str, Any]]) -> dict[str, dict[str, float | None]]:
    """Summarize the geometric envelope of a row set."""
    return {
        "width": numeric_stats(rows, "width"),
        "height": numeric_stats(rows, "height"),
        "min_dimension": numeric_stats(rows, "min_dimension"),
        "megapixels": numeric_stats(rows, "megapixels"),
    }


def dominant_counter_key(counter_map: dict[str, int]) -> str | None:
    """Return the most common counter key with deterministic tie-breaking."""
    if not counter_map:
        return None
    return max(counter_map.items(), key=lambda item: (item[1], item[0]))[0]


def category_dimension_ranges(rows: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    """Summarize geometric ranges for each failure category."""
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        grouped[str(row.get("failure_category", "unknown"))].append(row)

    ranges: dict[str, dict[str, Any]] = {}
    for category, category_rows in grouped.items():
        ranges[category] = {
            "count": len(category_rows),
            "envelope": envelope_stats(category_rows),
            "largest_image": boundary_image(category_rows, smallest=False),
            "smallest_image": boundary_image(category_rows, smallest=True),
        }
    return ranges


def observed_dimension_threshold(success_rows: list[dict[str, Any]], rejection_rows: list[dict[str, Any]]) -> dict[str, Any] | None:
    """Describe an observed success-vs-rejection threshold when one appears."""
    if not rejection_rows:
        return None

    rejection_min_dimensions = [
        int(row["min_dimension"]) for row in rejection_rows if isinstance(row.get("min_dimension"), int)
    ]
    if not rejection_min_dimensions:
        return None

    threshold: dict[str, Any] = {
        "largest_rejected_min_dimension": max(rejection_min_dimensions),
    }

    success_min_dimensions = [
        int(row["min_dimension"]) for row in success_rows if isinstance(row.get("min_dimension"), int)
    ]
    if success_min_dimensions:
        threshold["smallest_successful_min_dimension"] = min(success_min_dimensions)
        threshold["strict_separation_observed"] = min(success_min_dimensions) > max(rejection_min_dimensions)
    else:
        threshold["smallest_successful_min_dimension"] = None
        threshold["strict_separation_observed"] = None

    return threshold


def inspect_image_metadata(rse_path: Path, image_path: Path) -> dict[str, Any]:
    """Use `rse info` to inspect image dimensions without writing artifacts."""
    info_command = [
        str(rse_path),
        "info",
        "--in",
        str(image_path),
        "--protocol",
        "classic",
        "--json",
    ]
    result = run_command(info_command)
    payload = load_json_output(result.stdout)
    if result.returncode != 0 or payload is None or payload.get("status") != "OK":
        reason = failure_reason(payload, result.stderr)
        raise RuntimeError(f"failed to inspect image metadata for {image_path}: {reason}")

    width = int(payload["width"])
    height = int(payload["height"])
    return image_metrics(width, height)


def build_analysis_report(summary: dict[str, Any]) -> dict[str, Any]:
    """Build a compact protocol-level envelope report from raw summary data."""
    report = {
        "mode": summary.get("mode"),
        "protocols": summary.get("protocols"),
        "input_images": summary.get("input_images"),
        "requested_limit": summary.get("requested_limit"),
        "total_cases": summary.get("total_cases"),
        "per_protocol": {},
    }

    for protocol, protocol_summary in summary.get("per_protocol", {}).items():
        failure_category_breakdown = protocol_summary.get("failure_category_breakdown", {})
        mosaic_specific: dict[str, Any] | None = None
        if protocol == "mosaic":
            mosaic_specific = {
                "dominant_rejection_category": dominant_counter_key(failure_category_breakdown),
                "rejection_category_dimension_ranges": protocol_summary.get("expected_rejection_category_ranges", {}),
                "largest_expected_rejection_image": protocol_summary.get("largest_expected_rejection_image"),
                "smallest_successful_image": protocol_summary.get("smallest_successful_image"),
                "observed_dimension_threshold": protocol_summary.get("observed_dimension_threshold"),
            }

        report["per_protocol"][protocol] = {
            "support_count": protocol_summary.get("support_count"),
            "expected_rejection_count": protocol_summary.get("expected_rejection_count"),
            "recovery_failure_count": protocol_summary.get("recovery_failure_count"),
            "runtime_failure_count": protocol_summary.get("runtime_failure_count"),
            "support_rate": protocol_summary.get("support_rate"),
            "expected_rejection_rate": protocol_summary.get("expected_rejection_rate"),
            "recovery_failure_rate": protocol_summary.get("recovery_failure_rate"),
            "runtime_failure_rate": protocol_summary.get("runtime_failure_rate"),
            "embedding_success_rate": protocol_summary.get("embedding_success_rate"),
            "payload_recovery_rate": protocol_summary.get("payload_recovery_rate"),
            "success_envelope": protocol_summary.get("successful_envelope"),
            "expected_rejection_envelope": protocol_summary.get("expected_rejection_envelope"),
            "recovery_failure_envelope": protocol_summary.get("recovery_failure_envelope"),
            "runtime_failure_envelope": protocol_summary.get("runtime_failure_envelope"),
            "failure_category_breakdown": failure_category_breakdown,
            "mosaic_support_envelope": mosaic_specific,
        }

    return report


def summarize(rows: list[dict[str, Any]], input_count: int, protocols: list[str], limit: int | None, mode: str) -> dict[str, Any]:
    """Aggregate per-row corpus results into protocol-level summaries."""
    per_protocol: dict[str, dict[str, Any]] = {}
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        grouped[row["protocol"]].append(row)

    for protocol in protocols:
        protocol_rows = grouped.get(protocol, [])
        success_rows = [row for row in protocol_rows if row["classification"] == CLASSIFICATION_SUCCESS]
        expected_rejections = [row for row in protocol_rows if row["classification"] == CLASSIFICATION_EXPECTED_REJECTION]
        recovery_failures = [row for row in protocol_rows if row["classification"] == CLASSIFICATION_RECOVERY_FAILURE]
        runtime_failures = [row for row in protocol_rows if row["classification"] == CLASSIFICATION_RUNTIME_FAILURE]
        embedding_success_rows = [row for row in protocol_rows if row["embedding_succeeded"]]
        payload_recovery_rows = [row for row in protocol_rows if row["payload_recovered"]]
        snr_values = [float(row["snr"]) for row in protocol_rows if row["snr"] is not None]
        embed_times = [float(row["embed_time_ms"]) for row in protocol_rows if row["embed_time_ms"] is not None]
        verify_times = [float(row["verify_time_ms"]) for row in protocol_rows if row["verify_time_ms"] is not None]
        failure_reasons = Counter(row["failure_reason"] for row in protocol_rows if row["failure_reason"] not in ("", "none"))
        failure_categories = Counter(row["failure_category"] for row in protocol_rows if row["failure_category"] not in ("", "none"))

        per_protocol[protocol] = {
            "total_cases": len(protocol_rows),
            "support_count": len(success_rows),
            "expected_rejection_count": len(expected_rejections),
            "recovery_failure_count": len(recovery_failures),
            "runtime_failure_count": len(runtime_failures),
            "support_rate": (len(success_rows) / len(protocol_rows)) if protocol_rows else 0.0,
            "expected_rejection_rate": (len(expected_rejections) / len(protocol_rows)) if protocol_rows else 0.0,
            "recovery_failure_rate": (len(recovery_failures) / len(protocol_rows)) if protocol_rows else 0.0,
            "runtime_failure_rate": (len(runtime_failures) / len(protocol_rows)) if protocol_rows else 0.0,
            "embed_successes": len(embedding_success_rows),
            "verify_successes": len(payload_recovery_rows),
            "embedding_success_count": len(embedding_success_rows),
            "payload_recovery_count": len(payload_recovery_rows),
            "embed_success_rate": (len(embedding_success_rows) / len(protocol_rows)) if protocol_rows else 0.0,
            "verify_success_rate": (len(payload_recovery_rows) / len(protocol_rows)) if protocol_rows else 0.0,
            "embedding_success_rate": (len(embedding_success_rows) / len(protocol_rows)) if protocol_rows else 0.0,
            "payload_recovery_rate": (len(payload_recovery_rows) / len(protocol_rows)) if protocol_rows else 0.0,
            "average_embed_time_ms": average_or_none(embed_times),
            "average_verify_time_ms": average_or_none(verify_times),
            "average_embedding_time_ms": average_or_none(embed_times),
            "average_payload_recovery_time_ms": average_or_none(verify_times),
            "average_snr": average_or_none(snr_values),
            "min_snr": min(snr_values) if snr_values else None,
            "max_snr": max(snr_values) if snr_values else None,
            "failure_breakdown": dict(sorted(failure_reasons.items())),
            "failure_category_breakdown": dict(sorted(failure_categories.items())),
            "successful_envelope": envelope_stats(success_rows),
            "expected_rejection_envelope": envelope_stats(expected_rejections),
            "recovery_failure_envelope": envelope_stats(recovery_failures),
            "runtime_failure_envelope": envelope_stats(runtime_failures),
            "successful_dimensions": dimension_distribution(success_rows),
            "expected_rejection_dimensions": dimension_distribution(expected_rejections),
            "recovery_failure_dimensions": dimension_distribution(recovery_failures),
            "runtime_failure_dimensions": dimension_distribution(runtime_failures),
            "expected_rejection_category_ranges": category_dimension_ranges(expected_rejections),
            "smallest_successful_image": boundary_image(success_rows, smallest=True),
            "largest_expected_rejection_image": boundary_image(expected_rejections, smallest=False),
            "observed_dimension_threshold": observed_dimension_threshold(success_rows, expected_rejections),
        }

    return {
        "mode": mode,
        "protocols": protocols,
        "input_images": input_count,
        "requested_limit": limit,
        "total_cases": len(rows),
        "per_protocol": per_protocol,
    }


def payload_recovery_status_text(row: dict[str, Any]) -> str:
    """Translate row state into a human-facing payload-recovery status string."""
    if row["classification"] == CLASSIFICATION_EXPECTED_REJECTION:
        return "not_applicable"
    if row["payload_recovered"]:
        return "succeeded"
    if row["classification"] == CLASSIFICATION_RECOVERY_FAILURE:
        return "failed"
    return "interrupted"


def evaluate_corpus_case(
    rse_path: Path,
    input_dir: Path,
    output_dir: Path,
    image_path: Path,
    protocol: str,
    message: str,
    message_bytes: int,
    step: float,
    keep_outputs: bool,
    image_metadata: dict[str, Any],
    output_filename: str,
) -> dict[str, Any]:
    """Run embed+verify for one image/protocol pair and normalize the report row."""
    embedded_path = embedded_output_path_for_image(output_dir, protocol, output_filename)
    ensure_dir(embedded_path.parent)

    embed_command = [
        str(rse_path),
        "embed",
        "--protocol",
        protocol,
        "--in",
        str(image_path),
        "--out",
        str(embedded_path),
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
        "protocol": protocol,
        "image": image_path.name,
        "image_path": str(image_path),
        "input_file": str(image_path),
        "image_name": image_path.name,
        "relative_image": image_path.relative_to(input_dir).as_posix(),
        "message_bytes": message_bytes,
        "step": step,
        "embedding_succeeded": command_succeeded(embed_result, embed_payload, "OK"),
        "embed_ok": command_succeeded(embed_result, embed_payload, "OK"),
        "payload_recovered": False,
        "verify_ok": False,
        "embed_exit_code": embed_result.returncode,
        "verify_exit_code": None,
        "embed_time_ms": embed_payload.get("embed_time_ms") if embed_payload is not None else embed_elapsed_ms,
        "verify_time_ms": None,
        "embed_wall_time_ms": round(embed_elapsed_ms, 2),
        "verify_wall_time_ms": None,
        "snr": embed_payload.get("snr") if embed_payload is not None else None,
        "output_file": None,
        "output_file_size": embedded_path.stat().st_size if embedded_path.exists() else None,
        "width": image_metadata["width"],
        "height": image_metadata["height"],
        "min_dimension": image_metadata["min_dimension"],
        "max_dimension": image_metadata["max_dimension"],
        "aspect_ratio": image_metadata["aspect_ratio"],
        "megapixels": image_metadata["megapixels"],
        "classification": CLASSIFICATION_SUCCESS,
        "failure_domain": "none",
        "failure_category": "none",
        "failure_reason": "none",
    }

    if not row["embedding_succeeded"]:
        row["failure_reason"] = failure_reason(embed_payload, embed_result.stderr)
        row["classification"], row["failure_category"] = classify_failure(row["failure_reason"], stage="embed")
        row["failure_domain"] = failure_domain_for_classification(row["classification"])
        if embedded_path.exists() and not keep_outputs:
            embedded_path.unlink()
        return row

    row["output_file"] = str(embedded_path)

    verify_command = [
        str(rse_path),
        "verify",
        "--protocol",
        protocol,
        "--in",
        str(embedded_path),
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
    row["verify_time_ms"] = (
        verify_payload.get("verify_time_ms") if verify_payload is not None else verify_elapsed_ms
    )
    row["verify_wall_time_ms"] = round(verify_elapsed_ms, 2)
    row["decoded_message"] = verify_payload.get("decoded_message") if verify_payload is not None else None
    row["decoded_match"] = (
        row["decoded_message"] == message if row["decoded_message"] is not None else None
    )
    row["failure_reason"] = "none" if row["payload_recovered"] else failure_reason(verify_payload, verify_result.stderr)
    row["classification"], row["failure_category"] = classify_failure(
        row["failure_reason"],
        stage="verify" if not row["payload_recovered"] else "success",
    )
    row["failure_domain"] = failure_domain_for_classification(row["classification"])
    row["output_file_size"] = embedded_path.stat().st_size if embedded_path.exists() else None

    if embedded_path.exists() and not keep_outputs:
        embedded_path.unlink()

    return row


def execute_indexed_corpus_case(work_item: dict[str, Any]) -> dict[str, Any]:
    """Dispatch one corpus case for optional threaded execution."""
    return evaluate_corpus_case(
        rse_path=Path(work_item["rse_path"]),
        input_dir=Path(work_item["input_dir"]),
        output_dir=Path(work_item["output_dir"]),
        image_path=Path(work_item["image_path"]),
        protocol=str(work_item["protocol"]),
        message=str(work_item["message"]),
        message_bytes=int(work_item["message_bytes"]),
        step=float(work_item["step"]),
        keep_outputs=bool(work_item["keep_outputs"]),
        image_metadata=dict(work_item["image_metadata"]),
        output_filename=str(work_item["output_filename"]),
    )


def main() -> int:
    """Run a corpus evaluation job and write artifact summaries."""
    args = parse_args()
    run_started_at = current_utc_timestamp()
    wall_start = time.perf_counter()

    if args.limit is not None and args.limit <= 0:
        print("[FAIL] --limit must be greater than 0", file=sys.stderr)
        return 1
    if args.quick and args.full:
        print("[FAIL] --quick and --full are mutually exclusive", file=sys.stderr)
        return 1
    try:
        requested_jobs = parse_jobs_value(args.jobs)
    except ValueError as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    if not args.rse.exists():
        print(f"[FAIL] rse binary not found: {args.rse}", file=sys.stderr)
        return 1
    if not args.input_dir.is_dir():
        print(f"[FAIL] input directory not found: {args.input_dir}", file=sys.stderr)
        return 1

    protocols = selected_protocols(args.protocol)
    message_bytes = len(args.message.encode("utf-8"))
    limit = effective_limit(args)
    image_paths = select_paths(list_images(args.input_dir), limit=limit, shuffle=args.shuffle, seed=args.seed)
    duplicate_stems = duplicate_stems_for_paths(image_paths)

    if not image_paths:
        print("[FAIL] no input images found", file=sys.stderr)
        return 1

    ensure_dir(args.output_dir)
    mode = "quick" if args.quick and not args.full else "full" if args.full else "custom"
    csv_path = args.csv or (args.output_dir / "results.csv")
    json_path = args.json_path or (args.output_dir / "results.json")
    support_summary_json_path = args.analysis_json or args.support_summary_json
    total_cases = len(protocols) * len(image_paths)
    jobs_requested_text = "auto" if requested_jobs == "auto" else str(requested_jobs)
    jobs_resolved = resolve_job_count(requested_jobs, total_cases)
    execution_mode = execution_mode_for_jobs(jobs_resolved)

    print("Roadscript corpus evaluation")
    print(f"  rse              : {args.rse}")
    print(f"  input_dir        : {args.input_dir}")
    print(f"  output_dir       : {args.output_dir}")
    print(f"  protocol         : {args.protocol}")
    print(f"  mode             : {mode}")
    print(f"  image_count      : {len(image_paths)}")
    print(f"  shuffle          : {'yes' if args.shuffle else 'no'}")
    print(f"  seed             : {args.seed}")
    print(f"  execution_mode   : {execution_mode}")
    print(f"  jobs_requested   : {jobs_requested_text}")
    print(f"  jobs_resolved    : {jobs_resolved}")
    print(f"  keep_outputs     : {'yes' if args.keep_outputs else 'no'}")
    print(f"  csv              : {csv_path}")
    print(f"  json             : {json_path}")
    if args.sqlite is not None:
        print(f"  sqlite           : {args.sqlite}")
    if support_summary_json_path is not None:
        print(f"  analysis_json    : {support_summary_json_path}")

    image_metadata_cache: dict[Path, dict[str, Any]] = {}
    work_items: list[dict[str, Any]] = []
    case_index = 0

    for protocol in protocols:
        for image_path in image_paths:
            image_metadata = image_metadata_cache.get(image_path)
            if image_metadata is None:
                image_metadata = inspect_image_metadata(args.rse, image_path)
                image_metadata_cache[image_path] = image_metadata
            work_items.append(
                {
                    "case_index": case_index,
                    "rse_path": str(args.rse),
                    "input_dir": str(args.input_dir),
                    "output_dir": str(args.output_dir),
                    "image_path": str(image_path),
                    "protocol": protocol,
                    "message": args.message,
                    "message_bytes": message_bytes,
                    "step": args.step,
                    "keep_outputs": args.keep_outputs,
                    "image_metadata": dict(image_metadata),
                    "output_filename": output_filename_for_image(
                        args.input_dir,
                        image_path,
                        duplicate_stems,
                    ),
                }
            )
            case_index += 1

    rows, stopped_early = run_indexed_work_items(
        work_items,
        execute_indexed_corpus_case,
        jobs_resolved=jobs_resolved,
        fail_fast=args.fail_fast,
        should_stop_on_result=lambda row: row["classification"] == CLASSIFICATION_RUNTIME_FAILURE,
    )

    non_success_rows: list[dict[str, Any]] = []
    runtime_failure_rows: list[dict[str, Any]] = []
    for row in rows:
        status_text = classification_status_label(row["classification"])
        print(
            f"  [{status_text}] protocol={row['protocol']} image={row['relative_image']} "
            f"embedding={'succeeded' if row['embedding_succeeded'] else 'failed'} "
            f"payload_recovery={payload_recovery_status_text(row)} "
            f"dims={row['width']}x{row['height']} "
            f"snr={row['snr'] if row['snr'] is not None else 'n/a'} "
            f"embedding_ms={row['embed_time_ms'] if row['embed_time_ms'] is not None else 'n/a'} "
            f"recovery_ms={row['verify_time_ms'] if row['verify_time_ms'] is not None else 'n/a'} "
            f"classification={row['classification']} "
            f"failure_domain={row['failure_domain']} "
            f"category={row['failure_category']} "
            f"reason={row['failure_reason']}"
        )

        if row["classification"] != CLASSIFICATION_SUCCESS:
            non_success_rows.append(row)
        if row["classification"] == CLASSIFICATION_RUNTIME_FAILURE:
            runtime_failure_rows.append(row)

    wall_time_s = time.perf_counter() - wall_start
    run_finished_at = current_utc_timestamp()
    summary = summarize(rows, input_count=len(image_paths), protocols=protocols, limit=limit, mode=mode)
    timing_summary = run_timing_summary(
        rows,
        total_cases=len(rows),
        started_at=run_started_at,
        finished_at=run_finished_at,
        wall_time_s=wall_time_s,
    )
    payload = {
        "summary": summary,
        "execution": execution_metadata_payload(
            requested_jobs=jobs_requested_text,
            resolved_jobs=jobs_resolved,
            timing=timing_summary,
        ),
        "rows": rows,
    }
    analysis_report = build_analysis_report(summary)

    write_csv(csv_path, rows)
    write_json(json_path, payload)
    if args.failed_manifest is not None:
        write_json(args.failed_manifest, {"failed_rows": non_success_rows})
    if support_summary_json_path is not None:
        write_json(support_summary_json_path, analysis_report)
    sqlite_run_id: str | None = None
    if args.sqlite is not None:
        sqlite_run_id = export_run(
            args.sqlite,
            tool="corpus_eval",
            run_meta={
                "rse_path": str(args.rse),
                "input_dir": str(args.input_dir),
                "output_dir": str(args.output_dir),
                "protocol_mode": args.protocol,
                "message_bytes": message_bytes,
                "step": args.step,
                "limit_value": limit,
                "seed": args.seed if args.shuffle else None,
                "jobs_requested": jobs_requested_text,
                "jobs_resolved": jobs_resolved,
                "execution_mode": execution_mode,
                "started_at": timing_summary["started_at"],
                "finished_at": timing_summary["finished_at"],
                "wall_time_s": timing_summary["wall_time_s"],
                "throughput_cases_per_s": timing_summary["throughput_cases_per_s"],
                "estimated_serial_work_s": timing_summary["estimated_serial_work_s"],
                "estimated_speedup_vs_serial": timing_summary["estimated_speedup_vs_serial"],
                "effective_parallelism": timing_summary["effective_parallelism"],
                "notes": (
                    f"mode={mode}; execution_mode={execution_mode}; jobs_requested={jobs_requested_text}; "
                    f"jobs_resolved={jobs_resolved}; keep_outputs={'yes' if args.keep_outputs else 'no'}; "
                    f"fail_fast={'yes' if args.fail_fast else 'no'}; stopped_early={'yes' if stopped_early else 'no'}"
                ),
            },
            rows=rows,
            summaries_by_protocol=summary["per_protocol"],
        )

    print("\nSummary")
    print(f"  execution_mode   : {execution_mode}")
    print(f"  jobs_requested   : {jobs_requested_text}")
    print(f"  jobs_resolved    : {jobs_resolved}")
    print("  Timing")
    print(f"    started_at                    : {timing_summary['started_at']}")
    print(f"    finished_at                   : {timing_summary['finished_at']}")
    print(f"    wall_time_s                   : {timing_summary['wall_time_s']}")
    print(f"    throughput_cases_per_s        : {timing_summary['throughput_cases_per_s']}")
    print(f"    estimated_serial_work_s       : {timing_summary['estimated_serial_work_s']}")
    print(f"    estimated_speedup_vs_serial   : {timing_summary['estimated_speedup_vs_serial']}")
    print(f"    effective_parallelism         : {timing_summary['effective_parallelism']}")
    print(f"  total_cases       : {summary['total_cases']}")
    for protocol in protocols:
        protocol_summary = summary["per_protocol"].get(protocol, {})
        print(f"  {protocol}_support_rate            : {protocol_summary.get('support_rate', 0.0):.3f}")
        print(f"  {protocol}_expected_rejection_rate : {protocol_summary.get('expected_rejection_rate', 0.0):.3f}")
        print(f"  {protocol}_recovery_failure_rate   : {protocol_summary.get('recovery_failure_rate', 0.0):.3f}")
        print(f"  {protocol}_runtime_failure_rate    : {protocol_summary.get('runtime_failure_rate', 0.0):.3f}")
        print(f"  {protocol}_embedding_success_rate  : {protocol_summary.get('embedding_success_rate', 0.0):.3f}")
        print(f"  {protocol}_payload_recovery_rate   : {protocol_summary.get('payload_recovery_rate', 0.0):.3f}")
        print(f"  {protocol}_average_embedding_ms    : {protocol_summary.get('average_embedding_time_ms')}")
        print(f"  {protocol}_average_recovery_ms     : {protocol_summary.get('average_payload_recovery_time_ms')}")
        print(f"  {protocol}_average_snr             : {protocol_summary.get('average_snr')}")
        print(f"  {protocol}_failure_reasons         : {protocol_summary.get('failure_breakdown')}")
        print(f"  {protocol}_failure_categories      : {protocol_summary.get('failure_category_breakdown')}")
        success_smallest = protocol_summary.get("smallest_successful_image")
        rejection_largest = protocol_summary.get("largest_expected_rejection_image")
        dominant_category = dominant_counter_key(protocol_summary.get("failure_category_breakdown", {}))
        if success_smallest is not None:
            print(
                f"  {protocol}_smallest_success   : "
                f"{success_smallest.get('width')}x{success_smallest.get('height')}"
            )
        if rejection_largest is not None:
            print(
                f"  {protocol}_largest_rejection  : "
                f"{rejection_largest.get('width')}x{rejection_largest.get('height')}"
            )
        if dominant_category is not None:
            print(f"  {protocol}_dominant_category  : {dominant_category}")
    print(f"  csv              : {csv_path}")
    print(f"  json             : {json_path}")
    if args.sqlite is not None:
        print(f"  sqlite           : {args.sqlite}")
        print(f"  sqlite_run_id    : {sqlite_run_id}")
    if args.failed_manifest is not None:
        print(f"  failed_manifest  : {args.failed_manifest}")
    if support_summary_json_path is not None:
        print(f"  analysis_json    : {support_summary_json_path}")
    if stopped_early:
        print("  stop_reason      : fail-fast triggered after a runtime failure")

    return 1 if runtime_failure_rows else 0


if __name__ == "__main__":
    raise SystemExit(main())
