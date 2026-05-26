#!/usr/bin/env python3
from __future__ import annotations

import json
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> int:
    if condition:
        return 0
    print(f"[FAIL] {message}", file=sys.stderr)
    return 1


def scalar(conn: sqlite3.Connection, query: str, params: tuple[object, ...] = ()) -> object:
    row = conn.execute(query, params).fetchone()
    return row[0] if row is not None else None


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: corpus_sqlite_smoke_test.py <repo-root> <rse-path>", file=sys.stderr)
        return 1

    repo_root = Path(sys.argv[1]).resolve()
    rse_path = Path(sys.argv[2]).resolve()
    if not rse_path.exists():
        print(f"[FAIL] rse binary not found: {rse_path}", file=sys.stderr)
        return 1

    input_dir = repo_root / "tests" / "fixtures" / "input"
    temp_root = Path(tempfile.mkdtemp(prefix="roadscript_corpus_sqlite_smoke_"))

    corpus_output = temp_root / "corpus-output"
    corpus_sqlite = temp_root / "corpus.sqlite"
    corpus_json = corpus_output / "results.json"
    corpus_command = [
        sys.executable,
        str(repo_root / "tools" / "corpus_eval.py"),
        "--rse",
        str(rse_path),
        "--input-dir",
        str(input_dir),
        "--output-dir",
        str(corpus_output),
        "--protocol",
        "both",
        "--limit",
        "1",
        "--jobs",
        "2",
        "--sqlite",
        str(corpus_sqlite),
        "--csv",
        str(corpus_output / "results.csv"),
        "--json",
        str(corpus_json),
        "--analysis-json",
        str(corpus_output / "analysis.json"),
    ]
    corpus_result = subprocess.run(corpus_command, capture_output=True, text=True, cwd=repo_root)
    if corpus_result.returncode != 0:
        print("[FAIL] corpus_eval.py with --sqlite exited nonzero", file=sys.stderr)
        print("--- stdout ---", file=sys.stderr)
        print(corpus_result.stdout, file=sys.stderr)
        print("--- stderr ---", file=sys.stderr)
        print(corpus_result.stderr, file=sys.stderr)
        return 1

    if (rc := require(corpus_sqlite.exists(), "corpus SQLite database was not created")) != 0:
        return rc

    with sqlite3.connect(corpus_sqlite) as conn:
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM runs") == 1, "expected one run after first corpus export")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM runs WHERE jobs_requested = '2' AND jobs_resolved = 2 AND execution_mode = 'parallel'") == 1, "expected run metadata to record parallel job settings")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM runs WHERE started_at IS NOT NULL AND finished_at IS NOT NULL AND wall_time_s > 0 AND throughput_cases_per_s > 0 AND estimated_serial_work_s > 0 AND estimated_speedup_vs_serial > 0 AND effective_parallelism > 0") == 1, "expected run timing and throughput columns to be populated")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM cases") == 2, "expected two cases for one-image both-protocol corpus run")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM summaries") == 2, "expected two per-protocol summaries")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM cases WHERE classification = 'success'") == 2, "expected both initial corpus cases to succeed")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM cases WHERE input_file IS NOT NULL") == 2, "expected input_file to be populated for corpus cases")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM cases WHERE failure_domain = 'none'") == 2, "expected success corpus cases to carry failure_domain=none")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM cases WHERE embedding_succeeded = 1 AND payload_recovered = 1") == 2, "expected professionalized boolean columns to be populated")) != 0:
            return rc
        embedded_case_count = scalar(conn, "SELECT COUNT(*) FROM cases WHERE output_file LIKE '%/embedded/%'")
        if (rc := require(embedded_case_count == 2, "expected output_file to point to embedded artifact paths")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM summaries WHERE embedding_success_rate = 1.0 AND payload_recovery_rate = 1.0") == 2, "expected professionalized summary rate columns")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM summaries WHERE average_embedding_ms IS NOT NULL AND average_payload_recovery_ms IS NOT NULL") == 2, "expected professionalized summary timing columns")) != 0:
            return rc

    corpus_second_output = temp_root / "corpus-output-2"
    corpus_second_command = [
        sys.executable,
        str(repo_root / "tools" / "corpus_eval.py"),
        "--rse",
        str(rse_path),
        "--input-dir",
        str(input_dir),
        "--output-dir",
        str(corpus_second_output),
        "--protocol",
        "classic",
        "--limit",
        "1",
        "--jobs",
        "auto",
        "--sqlite",
        str(corpus_sqlite),
        "--csv",
        str(corpus_second_output / "results.csv"),
        "--json",
        str(corpus_second_output / "results.json"),
    ]
    corpus_second_result = subprocess.run(corpus_second_command, capture_output=True, text=True, cwd=repo_root)
    if corpus_second_result.returncode != 0:
        print("[FAIL] second corpus_eval.py append run exited nonzero", file=sys.stderr)
        print("--- stdout ---", file=sys.stderr)
        print(corpus_second_result.stdout, file=sys.stderr)
        print("--- stderr ---", file=sys.stderr)
        print(corpus_second_result.stderr, file=sys.stderr)
        return 1

    with sqlite3.connect(corpus_sqlite) as conn:
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM runs") == 2, "expected append behavior to preserve first corpus run")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM runs WHERE jobs_requested = 'auto' AND jobs_resolved = 1 AND execution_mode = 'serial'") == 1, "expected append run metadata to record resolved auto serial execution")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM cases") == 3, "expected second corpus run to append one more case")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM summaries") == 3, "expected second corpus run to append one more summary")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM runs WHERE tool = 'corpus_eval'") == 2, "expected two corpus_eval runs recorded")) != 0:
            return rc

    probe_output = temp_root / "probe-output"
    probe_sqlite = temp_root / "probe.sqlite"
    probe_command = [
        sys.executable,
        str(repo_root / "tools" / "mosaic_support_probe.py"),
        "--rse",
        str(rse_path),
        "--output-dir",
        str(probe_output),
        "--sizes",
        "1128,1136",
        "--jobs",
        "2",
        "--sqlite",
        str(probe_sqlite),
        "--csv",
        str(probe_output / "results.csv"),
        "--json",
        str(probe_output / "results.json"),
    ]
    probe_result = subprocess.run(probe_command, capture_output=True, text=True, cwd=repo_root)
    if probe_result.returncode != 0:
        print("[FAIL] mosaic_support_probe.py with --sqlite exited nonzero", file=sys.stderr)
        print("--- stdout ---", file=sys.stderr)
        print(probe_result.stdout, file=sys.stderr)
        print("--- stderr ---", file=sys.stderr)
        print(probe_result.stderr, file=sys.stderr)
        return 1

    if (rc := require(probe_sqlite.exists(), "probe SQLite database was not created")) != 0:
        return rc

    probe_payload = json.loads((probe_output / "results.json").read_text(encoding="utf-8"))
    if (rc := require(probe_payload.get("summary", {}).get("expected_rejection_rate") == 0.5, "expected probe JSON threshold mix to remain unchanged")) != 0:
        return rc

    with sqlite3.connect(probe_sqlite) as conn:
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM runs") == 1, "expected one probe run")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM cases") == 2, "expected two probe cases")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM summaries") == 1, "expected one probe summary")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM runs WHERE tool = 'mosaic_support_probe'") == 1, "expected mosaic_support_probe tool row")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM failure_categories WHERE failure_category = 'mosaic_input_too_small'") == 1, "expected probe failure category breakdown row")) != 0:
            return rc
        if (rc := require(scalar(conn, "SELECT COUNT(*) FROM cases WHERE failure_domain = 'support_envelope'") == 1, "expected expected rejection to map to support_envelope in SQLite")) != 0:
            return rc

    print("[PASS] corpus_sqlite_smoke_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
