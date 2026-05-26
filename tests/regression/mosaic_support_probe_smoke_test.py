#!/usr/bin/env python3
from __future__ import annotations

import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mosaic_support_probe_smoke_test.py <repo-root> <rse-path>", file=sys.stderr)
        return 1

    repo_root = Path(sys.argv[1]).resolve()
    rse_path = Path(sys.argv[2]).resolve()
    if not rse_path.exists():
        print(f"[FAIL] rse binary not found: {rse_path}", file=sys.stderr)
        return 1

    temp_root = Path(tempfile.mkdtemp(prefix="roadscript_mosaic_support_probe_smoke_"))
    output_dir = temp_root / "output"
    csv_path = output_dir / "results.csv"
    json_path = output_dir / "results.json"

    command = [
        sys.executable,
        str(repo_root / "tools" / "mosaic_support_probe.py"),
        "--rse",
        str(rse_path),
        "--output-dir",
        str(output_dir),
        "--sizes",
        "32,64",
        "--jobs",
        "2",
        "--csv",
        str(csv_path),
        "--json",
        str(json_path),
    ]
    result = subprocess.run(command, capture_output=True, text=True, cwd=repo_root)
    if result.returncode != 0:
        print("[FAIL] mosaic_support_probe.py exited nonzero", file=sys.stderr)
        print("--- stdout ---", file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        print("--- stderr ---", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return 1

    if not csv_path.exists() or not json_path.exists():
        print("[FAIL] expected CSV and JSON outputs", file=sys.stderr)
        return 1

    rows = list(csv.DictReader(csv_path.open(newline="", encoding="utf-8")))
    if len(rows) != 2:
        print("[FAIL] expected two probe rows", file=sys.stderr)
        return 1
    for row in rows:
        if row.get("classification") != "expected_rejection":
            print("[FAIL] expected tiny probe sizes to be classified as expected_rejection", file=sys.stderr)
            return 1
        if row.get("embedding_succeeded") not in ("False", "false", "0", "") and row.get("embedding_succeeded") not in (False, None):
            print("[FAIL] expected tiny probe sizes to report embedding_succeeded=false", file=sys.stderr)
            return 1
        if row.get("failure_domain") != "support_envelope":
            print("[FAIL] expected tiny probe sizes to use support_envelope failure_domain", file=sys.stderr)
            return 1
        if row.get("failure_category") != "mosaic_input_too_small":
            print("[FAIL] expected tiny probe sizes to use mosaic_input_too_small classification", file=sys.stderr)
            return 1
        if row.get("failure_category") == "runtime_failure":
            print("[FAIL] expected rejections must not be marked as runtime failures", file=sys.stderr)
            return 1
        for field in ("width", "height", "min_dimension", "max_dimension", "aspect_ratio", "megapixels"):
            if row.get(field) in ("", None):
                print(f"[FAIL] missing dimension field {field}", file=sys.stderr)
                return 1

    payload = json.loads(json_path.read_text(encoding="utf-8"))
    config = payload.get("config", {})
    summary = payload.get("summary", {})
    json_rows = payload.get("rows", [])
    if len(json_rows) != 2:
        print("[FAIL] expected two JSON rows", file=sys.stderr)
        return 1
    if config.get("jobs_requested") != "2" or config.get("jobs_resolved") != 2:
        print("[FAIL] expected jobs metadata for --jobs 2", file=sys.stderr)
        return 1
    if config.get("execution_mode") != "parallel":
        print("[FAIL] expected execution_mode=parallel for --jobs 2", file=sys.stderr)
        return 1
    if [row.get("size_label") for row in json_rows] != ["32x32", "64x64"]:
        print("[FAIL] expected deterministic row ordering for parallel probe execution", file=sys.stderr)
        return 1
    if summary.get("support_rate") != 0.0:
        print("[FAIL] expected support_rate 0.0 for tiny rejection-only probe", file=sys.stderr)
        return 1
    if summary.get("expected_rejection_rate") != 1.0:
        print("[FAIL] expected expected_rejection_rate 1.0 for tiny rejection-only probe", file=sys.stderr)
        return 1
    if summary.get("runtime_failure_rate") != 0.0:
        print("[FAIL] expected runtime_failure_rate 0.0 for tiny rejection-only probe", file=sys.stderr)
        return 1
    if summary.get("dominant_rejection_category") != "mosaic_input_too_small":
        print("[FAIL] expected dominant_rejection_category mosaic_input_too_small", file=sys.stderr)
        return 1
    if summary.get("largest_expected_rejection") is None:
        print("[FAIL] expected largest_expected_rejection summary", file=sys.stderr)
        return 1

    auto_output_dir = temp_root / "auto-output"
    auto_json_path = auto_output_dir / "results.json"
    auto_command = [
        sys.executable,
        str(repo_root / "tools" / "mosaic_support_probe.py"),
        "--rse",
        str(rse_path),
        "--output-dir",
        str(auto_output_dir),
        "--sizes",
        "32",
        "--jobs",
        "auto",
        "--json",
        str(auto_json_path),
    ]
    auto_result = subprocess.run(auto_command, capture_output=True, text=True, cwd=repo_root)
    if auto_result.returncode != 0:
        print("[FAIL] auto-jobs probe run exited nonzero", file=sys.stderr)
        print("--- stdout ---", file=sys.stderr)
        print(auto_result.stdout, file=sys.stderr)
        print("--- stderr ---", file=sys.stderr)
        print(auto_result.stderr, file=sys.stderr)
        return 1
    auto_payload = json.loads(auto_json_path.read_text(encoding="utf-8"))
    auto_config = auto_payload.get("config", {})
    if auto_config.get("jobs_requested") != "auto":
        print("[FAIL] expected jobs_requested=auto for auto probe run", file=sys.stderr)
        return 1
    if auto_config.get("jobs_resolved") != 1:
        print("[FAIL] expected auto jobs to resolve to one worker for a single probe case", file=sys.stderr)
        return 1

    invalid_jobs_command = [
        sys.executable,
        str(repo_root / "tools" / "mosaic_support_probe.py"),
        "--rse",
        str(rse_path),
        "--output-dir",
        str(temp_root / "invalid-jobs"),
        "--sizes",
        "32",
        "--jobs",
        "fast",
    ]
    invalid_jobs_result = subprocess.run(invalid_jobs_command, capture_output=True, text=True, cwd=repo_root)
    if invalid_jobs_result.returncode == 0:
        print("[FAIL] expected invalid --jobs value to fail", file=sys.stderr)
        return 1

    print("[PASS] mosaic_support_probe_smoke_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
