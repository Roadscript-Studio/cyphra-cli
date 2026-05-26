#!/usr/bin/env python3

'''
python3 tools/download_dataset.py --dataset coco-val --limit 50

python3 tools/corpus_eval.py \
  --rse ./cmake-build-debug/rse \
  --input-dir datasets/coco/val2017 \
  --output-dir tests/artifacts/corpus/coco-smoke \
  --protocol both \
  --limit 50 \
  --csv tests/artifacts/corpus/coco-smoke/results.csv \
  --json tests/artifacts/corpus/coco-smoke/results.json \
  --failed-manifest tests/artifacts/corpus/coco-smoke/failed.json
'''

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: corpus_eval_smoke_test.py <repo-root> <rse-path>", file=sys.stderr)
        return 1

    repo_root = Path(sys.argv[1]).resolve()
    rse_path = Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(repo_root / "tools"))
    from corpus.evaluation_support import classify_failure  # pylint: disable=import-outside-toplevel

    input_dir = repo_root / "tests" / "fixtures" / "input"

    if not rse_path.exists():
        print(f"[FAIL] rse binary not found: {rse_path}", file=sys.stderr)
        return 1
    if classify_failure("crc mismatch", stage="verify") != ("recovery_failure", "payload_crc_mismatch"):
        print("[FAIL] expected crc mismatch to classify as recovery_failure", file=sys.stderr)
        return 1

    temp_root = Path(tempfile.mkdtemp(prefix="roadscript_corpus_eval_smoke_"))
    output_dir = temp_root / "output"
    csv_path = output_dir / "results.csv"
    json_path = output_dir / "results.json"
    failed_manifest = output_dir / "failed.json"
    analysis_json = output_dir / "analysis.json"

    command = [
        sys.executable,
        str(repo_root / "tools" / "corpus_eval.py"),
        "--rse",
        str(rse_path),
        "--input-dir",
        str(input_dir),
        "--output-dir",
        str(output_dir),
        "--protocol",
        "both",
        "--limit",
        "1",
        "--jobs",
        "2",
        "--csv",
        str(csv_path),
        "--json",
        str(json_path),
        "--failed-manifest",
        str(failed_manifest),
        "--analysis-json",
        str(analysis_json),
    ]

    result = subprocess.run(command, capture_output=True, text=True, cwd=repo_root)
    if result.returncode != 0:
        print("[FAIL] corpus_eval.py exited nonzero", file=sys.stderr)
        print("--- stdout ---", file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        print("--- stderr ---", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return 1

    if not csv_path.exists() or not json_path.exists():
        print("[FAIL] corpus evaluation outputs were not created", file=sys.stderr)
        return 1

    payload = json.loads(json_path.read_text(encoding="utf-8"))
    summary = payload.get("summary", {})
    execution = payload.get("execution", {})
    rows = payload.get("rows", [])

    if summary.get("total_cases") != 2:
        print("[FAIL] expected two protocol cases in summary", file=sys.stderr)
        return 1
    if len(rows) != 2:
        print("[FAIL] expected two result rows", file=sys.stderr)
        return 1
    if execution.get("jobs_requested") != "2":
        print("[FAIL] expected jobs_requested=2 in JSON execution metadata", file=sys.stderr)
        return 1
    if execution.get("jobs_resolved") != 2:
        print("[FAIL] expected jobs_resolved=2 for two-case corpus run", file=sys.stderr)
        return 1
    if execution.get("execution_mode") != "parallel":
        print("[FAIL] expected execution_mode=parallel for --jobs 2", file=sys.stderr)
        return 1
    if not isinstance(execution.get("started_at"), str) or not execution.get("started_at"):
        print("[FAIL] expected execution started_at timing field", file=sys.stderr)
        return 1
    if not isinstance(execution.get("finished_at"), str) or not execution.get("finished_at"):
        print("[FAIL] expected execution finished_at timing field", file=sys.stderr)
        return 1
    if not isinstance(execution.get("wall_time_s"), (int, float)) or execution.get("wall_time_s") <= 0:
        print("[FAIL] expected positive execution wall_time_s", file=sys.stderr)
        return 1
    if not isinstance(execution.get("throughput_cases_per_s"), (int, float)) or execution.get("throughput_cases_per_s") <= 0:
        print("[FAIL] expected positive execution throughput_cases_per_s", file=sys.stderr)
        return 1
    estimated_serial_work_s = execution.get("estimated_serial_work_s")
    if estimated_serial_work_s is None or not isinstance(estimated_serial_work_s, (int, float)) or estimated_serial_work_s <= 0:
        print("[FAIL] expected positive execution estimated_serial_work_s", file=sys.stderr)
        return 1
    estimated_speedup = execution.get("estimated_speedup_vs_serial")
    if estimated_speedup is None or not isinstance(estimated_speedup, (int, float)) or estimated_speedup <= 0:
        print("[FAIL] expected positive execution estimated_speedup_vs_serial", file=sys.stderr)
        return 1
    if execution.get("effective_parallelism") != estimated_speedup:
        print("[FAIL] expected effective_parallelism to mirror estimated_speedup_vs_serial", file=sys.stderr)
        return 1

    protocols = {row.get("protocol") for row in rows}
    if protocols != {"classic", "mosaic"}:
        print("[FAIL] expected classic and mosaic rows", file=sys.stderr)
        return 1
    if [row.get("protocol") for row in rows] != ["classic", "mosaic"]:
        print("[FAIL] expected deterministic protocol row ordering", file=sys.stderr)
        return 1

    for row in rows:
        if not row.get("embedding_succeeded") or not row.get("payload_recovered"):
            print("[FAIL] expected embedding_succeeded and payload_recovered in smoke run", file=sys.stderr)
            return 1
        if not row.get("embed_ok") or not row.get("verify_ok"):
            print("[FAIL] expected legacy embed_ok and verify_ok compatibility fields in smoke run", file=sys.stderr)
            return 1
        if row.get("classification") != "success":
            print("[FAIL] expected success classification in smoke run", file=sys.stderr)
            return 1
        if row.get("failure_domain") != "none":
            print("[FAIL] expected success rows to carry failure_domain=none", file=sys.stderr)
            return 1
        if row.get("input_file") != row.get("image_path"):
            print("[FAIL] expected input_file to mirror the source image path", file=sys.stderr)
            return 1
        if row.get("image") != row.get("image_name"):
            print("[FAIL] expected image field to hold the source basename", file=sys.stderr)
            return 1
        for field in ("width", "height", "min_dimension", "max_dimension", "aspect_ratio", "megapixels"):
            if row.get(field) is None:
                print(f"[FAIL] expected dimension field {field} in smoke run", file=sys.stderr)
                return 1
        output_file = row.get("output_file")
        if output_file is None or "/embedded/" not in output_file.replace("\\", "/"):
            print("[FAIL] expected output_file to point to the embedded output path", file=sys.stderr)
            return 1
        if output_file and Path(output_file).exists():
            print("[FAIL] output file should have been cleaned without --keep-outputs", file=sys.stderr)
            return 1

    keep_output_dir = temp_root / "keep-output"
    keep_json = keep_output_dir / "results.json"
    keep_command = [
        sys.executable,
        str(repo_root / "tools" / "corpus_eval.py"),
        "--rse",
        str(rse_path),
        "--input-dir",
        str(input_dir),
        "--output-dir",
        str(keep_output_dir),
        "--protocol",
        "classic",
        "--limit",
        "1",
        "--jobs",
        "1",
        "--keep-outputs",
        "--json",
        str(keep_json),
    ]
    keep_result = subprocess.run(keep_command, capture_output=True, text=True, cwd=repo_root)
    if keep_result.returncode != 0:
        print("[FAIL] keep-output corpus run exited nonzero", file=sys.stderr)
        print("--- stdout ---", file=sys.stderr)
        print(keep_result.stdout, file=sys.stderr)
        print("--- stderr ---", file=sys.stderr)
        print(keep_result.stderr, file=sys.stderr)
        return 1

    keep_payload = json.loads(keep_json.read_text(encoding="utf-8"))
    keep_execution = keep_payload.get("execution", {})
    keep_rows = keep_payload.get("rows", [])
    if len(keep_rows) != 1:
        print("[FAIL] expected one keep-output row", file=sys.stderr)
        return 1
    if keep_execution.get("execution_mode") != "serial" or keep_execution.get("jobs_resolved") != 1:
        print("[FAIL] expected serial execution metadata for --jobs 1", file=sys.stderr)
        return 1
    if not isinstance(keep_execution.get("wall_time_s"), (int, float)) or keep_execution.get("wall_time_s") <= 0:
        print("[FAIL] expected keep-output run to include positive wall_time_s", file=sys.stderr)
        return 1
    keep_row = keep_rows[0]
    keep_output_file = keep_row.get("output_file")
    if not keep_output_file:
        print("[FAIL] expected keep-output row to record embedded output path", file=sys.stderr)
        return 1
    keep_output_path = Path(keep_output_file)
    if not keep_output_path.exists():
        print("[FAIL] expected embedded output file to exist with --keep-outputs", file=sys.stderr)
        return 1
    normalized_output = keep_output_path.as_posix()
    if "/classic/embedded/" not in normalized_output:
        print("[FAIL] expected keep-output artifact under classic/embedded", file=sys.stderr)
        return 1
    if keep_output_path.resolve() == Path(keep_row["input_file"]).resolve():
        print("[FAIL] keep-output artifact must not be the original input file", file=sys.stderr)
        return 1

    if not failed_manifest.exists():
        print("[FAIL] failed manifest was not written", file=sys.stderr)
        return 1

    failed_payload = json.loads(failed_manifest.read_text(encoding="utf-8"))
    if failed_payload.get("failed_rows") != []:
        print("[FAIL] expected empty failed_rows manifest", file=sys.stderr)
        return 1

    if not analysis_json.exists():
        print("[FAIL] analysis JSON was not written", file=sys.stderr)
        return 1

    support_payload = json.loads(analysis_json.read_text(encoding="utf-8"))
    classic_summary = support_payload.get("per_protocol", {}).get("classic", {})
    mosaic_summary = support_payload.get("per_protocol", {}).get("mosaic", {})
    if classic_summary.get("support_rate") != 1.0 or mosaic_summary.get("support_rate") != 1.0:
        print("[FAIL] expected support_rate 1.0 for both protocols in smoke run", file=sys.stderr)
        return 1
    if classic_summary.get("success_envelope", {}).get("width", {}).get("min") is None:
        print("[FAIL] expected success envelope width stats", file=sys.stderr)
        return 1

    tiny_dir = temp_root / "tiny"
    tiny_dir.mkdir(parents=True, exist_ok=True)
    tiny_ppm = tiny_dir / "tiny.ppm"
    tiny_ppm.write_text("P3\n1 1\n255\n255 0 0\n", encoding="ascii")

    reject_output_dir = temp_root / "reject-output"
    reject_json = reject_output_dir / "results.json"
    reject_failed = reject_output_dir / "failed.json"
    reject_analysis = reject_output_dir / "analysis.json"
    reject_command = [
        sys.executable,
        str(repo_root / "tools" / "corpus_eval.py"),
        "--rse",
        str(rse_path),
        "--input-dir",
        str(tiny_dir),
        "--output-dir",
        str(reject_output_dir),
        "--protocol",
        "mosaic",
        "--limit",
        "1",
        "--jobs",
        "auto",
        "--json",
        str(reject_json),
        "--failed-manifest",
        str(reject_failed),
        "--analysis-json",
        str(reject_analysis),
    ]
    reject_result = subprocess.run(reject_command, capture_output=True, text=True, cwd=repo_root)
    if reject_result.returncode != 0:
        print("[FAIL] expected-rejection run should exit 0", file=sys.stderr)
        print("--- stdout ---", file=sys.stderr)
        print(reject_result.stdout, file=sys.stderr)
        print("--- stderr ---", file=sys.stderr)
        print(reject_result.stderr, file=sys.stderr)
        return 1

    reject_payload = json.loads(reject_json.read_text(encoding="utf-8"))
    reject_execution = reject_payload.get("execution", {})
    reject_rows = reject_payload.get("rows", [])
    if len(reject_rows) != 1:
        print("[FAIL] expected one reject row", file=sys.stderr)
        return 1
    if reject_execution.get("jobs_requested") != "auto":
        print("[FAIL] expected jobs_requested=auto in reject run", file=sys.stderr)
        return 1
    reject_jobs_resolved = reject_execution.get("jobs_resolved")
    if not isinstance(reject_jobs_resolved, int) or reject_jobs_resolved != 1:
        print("[FAIL] expected auto jobs to resolve to one worker for a single-case run", file=sys.stderr)
        return 1
    if not isinstance(reject_execution.get("throughput_cases_per_s"), (int, float)) or reject_execution.get("throughput_cases_per_s") <= 0:
        print("[FAIL] expected reject run to include positive throughput_cases_per_s", file=sys.stderr)
        return 1
    reject_row = reject_rows[0]
    if reject_row.get("classification") != "expected_rejection":
        print("[FAIL] expected rejection classification for tiny mosaic case", file=sys.stderr)
        return 1
    if reject_row.get("embedding_succeeded") not in (False, None):
        print("[FAIL] expected tiny mosaic case to report embedding_succeeded=false", file=sys.stderr)
        return 1
    if reject_row.get("failure_domain") != "support_envelope":
        print("[FAIL] expected support-envelope failure_domain for tiny mosaic case", file=sys.stderr)
        return 1

    invalid_jobs_command = [
        sys.executable,
        str(repo_root / "tools" / "corpus_eval.py"),
        "--rse",
        str(rse_path),
        "--input-dir",
        str(input_dir),
        "--output-dir",
        str(temp_root / "invalid-jobs"),
        "--protocol",
        "classic",
        "--limit",
        "1",
        "--jobs",
        "0",
    ]
    invalid_jobs_result = subprocess.run(invalid_jobs_command, capture_output=True, text=True, cwd=repo_root)
    if invalid_jobs_result.returncode == 0:
        print("[FAIL] expected invalid --jobs 0 to fail", file=sys.stderr)
        return 1
    invalid_jobs_fast_command = invalid_jobs_command[:-1] + ["fast"]
    invalid_jobs_fast_result = subprocess.run(invalid_jobs_fast_command, capture_output=True, text=True, cwd=repo_root)
    if invalid_jobs_fast_result.returncode == 0:
        print("[FAIL] expected invalid --jobs fast to fail", file=sys.stderr)
        return 1
    if reject_row.get("failure_category") != "mosaic_input_too_small":
        print("[FAIL] expected tiny mosaic case to use mosaic_input_too_small category", file=sys.stderr)
        return 1
    if reject_row.get("failure_category") == "runtime_failure":
        print("[FAIL] expected rejection should not be marked runtime_failure", file=sys.stderr)
        return 1

    reject_summary = reject_payload.get("summary", {}).get("per_protocol", {}).get("mosaic", {})
    if reject_summary.get("expected_rejection_rate") != 1.0:
        print("[FAIL] expected rejection rate should be 1.0 for tiny mosaic case", file=sys.stderr)
        return 1
    if reject_summary.get("runtime_failure_rate") != 0.0:
        print("[FAIL] runtime failure rate should be 0.0 for tiny mosaic case", file=sys.stderr)
        return 1

    reject_analysis_payload = json.loads(reject_analysis.read_text(encoding="utf-8"))
    mosaic_analysis = reject_analysis_payload.get("per_protocol", {}).get("mosaic", {})
    if mosaic_analysis.get("expected_rejection_rate") != 1.0:
        print("[FAIL] analysis JSON expected rejection rate should be 1.0", file=sys.stderr)
        return 1
    if mosaic_analysis.get("mosaic_support_envelope", {}).get("dominant_rejection_category") != "mosaic_input_too_small":
        print("[FAIL] expected dominant rejection category to reflect the preflight threshold", file=sys.stderr)
        return 1

    print("[PASS] corpus_eval_smoke_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
