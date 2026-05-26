from __future__ import annotations

import sqlite3
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def make_run_id(tool: str) -> str:
    """Create a compact run identifier that stays readable in DataGrip."""
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return f"{tool}-{timestamp}-{uuid.uuid4().hex[:8]}"


def _bool_to_int(value: Any) -> int | None:
    """Normalize Python truthy values to SQLite-friendly integer booleans."""
    if value is None:
        return None
    return 1 if bool(value) else 0


def _connect(path: Path) -> sqlite3.Connection:
    """Open the SQLite database and ensure its parent directory exists."""
    path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(path)
    conn.row_factory = sqlite3.Row
    return conn


def _create_schema(conn: sqlite3.Connection) -> None:
    """Create the append-only corpus-analysis schema when it is missing."""
    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS runs (
            run_id TEXT PRIMARY KEY,
            tool TEXT NOT NULL,
            created_at TEXT NOT NULL,
            started_at TEXT,
            finished_at TEXT,
            wall_time_s REAL,
            throughput_cases_per_s REAL,
            estimated_serial_work_s REAL,
            estimated_speedup_vs_serial REAL,
            effective_parallelism REAL,
            rse_path TEXT NOT NULL,
            input_dir TEXT,
            output_dir TEXT NOT NULL,
            protocol_mode TEXT NOT NULL,
            message_bytes INTEGER,
            step REAL,
            limit_value INTEGER,
            seed INTEGER,
            jobs_requested TEXT,
            jobs_resolved INTEGER,
            execution_mode TEXT,
            notes TEXT
        );

        CREATE TABLE IF NOT EXISTS cases (
            case_id INTEGER PRIMARY KEY AUTOINCREMENT,
            run_id TEXT NOT NULL,
            protocol TEXT,
            image TEXT,
            input_file TEXT,
            output_file TEXT,
            width INTEGER,
            height INTEGER,
            min_dimension INTEGER,
            max_dimension INTEGER,
            aspect_ratio REAL,
            megapixels REAL,
            message_bytes INTEGER,
            step REAL,
            classification TEXT,
            failure_domain TEXT,
            failure_category TEXT,
            failure_reason TEXT,
            embedding_succeeded INTEGER,
            payload_recovered INTEGER,
            decoded_match INTEGER,
            snr REAL,
            embed_ms REAL,
            verify_ms REAL,
            output_size_bytes INTEGER,
            FOREIGN KEY(run_id) REFERENCES runs(run_id)
        );

        CREATE TABLE IF NOT EXISTS summaries (
            run_id TEXT NOT NULL,
            protocol TEXT NOT NULL,
            total_cases INTEGER,
            support_rate REAL,
            expected_rejection_rate REAL,
            recovery_failure_rate REAL,
            runtime_failure_rate REAL,
            embedding_success_rate REAL,
            payload_recovery_rate REAL,
            average_embedding_ms REAL,
            average_payload_recovery_ms REAL,
            average_snr REAL,
            FOREIGN KEY(run_id) REFERENCES runs(run_id)
        );

        CREATE TABLE IF NOT EXISTS failure_categories (
            run_id TEXT NOT NULL,
            protocol TEXT NOT NULL,
            failure_category TEXT NOT NULL,
            count INTEGER NOT NULL,
            FOREIGN KEY(run_id) REFERENCES runs(run_id)
        );

        CREATE INDEX IF NOT EXISTS idx_cases_run_id ON cases(run_id);
        CREATE INDEX IF NOT EXISTS idx_cases_protocol ON cases(protocol);
        CREATE INDEX IF NOT EXISTS idx_cases_classification ON cases(classification);
        CREATE INDEX IF NOT EXISTS idx_cases_failure_category ON cases(failure_category);
        CREATE INDEX IF NOT EXISTS idx_cases_dimensions ON cases(width, height);
        CREATE INDEX IF NOT EXISTS idx_summaries_run_id ON summaries(run_id);
        """
    )
    _migrate_schema(conn)


def _table_columns(conn: sqlite3.Connection, table_name: str) -> set[str]:
    """Inspect the current column set for a table."""
    rows = conn.execute(f"PRAGMA table_info({table_name})").fetchall()
    return {str(row["name"]) for row in rows}


def _ensure_column(
    conn: sqlite3.Connection,
    table_name: str,
    known_columns: set[str],
    column_name: str,
    definition: str,
) -> None:
    """Add a column lazily so older SQLite exports can accept new writes."""
    if column_name in known_columns:
        return
    conn.execute(f"ALTER TABLE {table_name} ADD COLUMN {column_name} {definition}")
    known_columns.add(column_name)


def _migrate_schema(conn: sqlite3.Connection) -> None:
    """Keep append-only exports working when the local schema evolves."""
    run_columns = _table_columns(conn, "runs")
    case_columns = _table_columns(conn, "cases")
    summary_columns = _table_columns(conn, "summaries")
    _ensure_column(conn, "runs", run_columns, "jobs_requested", "TEXT")
    _ensure_column(conn, "runs", run_columns, "jobs_resolved", "INTEGER")
    _ensure_column(conn, "runs", run_columns, "execution_mode", "TEXT")
    _ensure_column(conn, "runs", run_columns, "started_at", "TEXT")
    _ensure_column(conn, "runs", run_columns, "finished_at", "TEXT")
    _ensure_column(conn, "runs", run_columns, "wall_time_s", "REAL")
    _ensure_column(conn, "runs", run_columns, "throughput_cases_per_s", "REAL")
    _ensure_column(conn, "runs", run_columns, "estimated_serial_work_s", "REAL")
    _ensure_column(conn, "runs", run_columns, "estimated_speedup_vs_serial", "REAL")
    _ensure_column(conn, "runs", run_columns, "effective_parallelism", "REAL")
    _ensure_column(conn, "cases", case_columns, "failure_domain", "TEXT")
    _ensure_column(conn, "cases", case_columns, "embedding_succeeded", "INTEGER")
    _ensure_column(conn, "cases", case_columns, "payload_recovered", "INTEGER")
    _ensure_column(conn, "summaries", summary_columns, "recovery_failure_rate", "REAL")
    _ensure_column(conn, "summaries", summary_columns, "embedding_success_rate", "REAL")
    _ensure_column(conn, "summaries", summary_columns, "payload_recovery_rate", "REAL")
    _ensure_column(conn, "summaries", summary_columns, "average_embedding_ms", "REAL")
    _ensure_column(conn, "summaries", summary_columns, "average_payload_recovery_ms", "REAL")


def export_run(
    sqlite_path: Path,
    *,
    tool: str,
    run_meta: dict[str, Any],
    rows: list[dict[str, Any]],
    summaries_by_protocol: dict[str, dict[str, Any]],
) -> str:
    """Append one corpus or probe run plus its cases and summaries to SQLite."""
    run_id = make_run_id(tool)
    created_at = datetime.now(timezone.utc).isoformat()

    conn = _connect(sqlite_path)
    try:
        with conn:
            _create_schema(conn)

            conn.execute(
                """
                INSERT INTO runs (
                    run_id, tool, created_at, started_at, finished_at, wall_time_s,
                    throughput_cases_per_s, estimated_serial_work_s, estimated_speedup_vs_serial,
                    effective_parallelism, rse_path, input_dir, output_dir,
                    protocol_mode, message_bytes, step, limit_value, seed,
                    jobs_requested, jobs_resolved, execution_mode, notes
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    run_id,
                    tool,
                    created_at,
                    run_meta.get("started_at"),
                    run_meta.get("finished_at"),
                    run_meta.get("wall_time_s"),
                    run_meta.get("throughput_cases_per_s"),
                    run_meta.get("estimated_serial_work_s"),
                    run_meta.get("estimated_speedup_vs_serial"),
                    run_meta.get("effective_parallelism"),
                    run_meta.get("rse_path"),
                    run_meta.get("input_dir"),
                    run_meta.get("output_dir"),
                    run_meta.get("protocol_mode"),
                    run_meta.get("message_bytes"),
                    run_meta.get("step"),
                    run_meta.get("limit_value"),
                    run_meta.get("seed"),
                    run_meta.get("jobs_requested"),
                    run_meta.get("jobs_resolved"),
                    run_meta.get("execution_mode"),
                    run_meta.get("notes"),
                ),
            )

            case_rows = []
            for row in rows:
                image_label = row.get("image") or row.get("relative_image") or row.get("size_label") or row.get("image_name")
                case_rows.append(
                    (
                        run_id,
                        row.get("protocol"),
                        image_label,
                        row.get("image_path") or row.get("input_file"),
                        row.get("output_file"),
                        row.get("width"),
                        row.get("height"),
                        row.get("min_dimension"),
                        row.get("max_dimension"),
                        row.get("aspect_ratio"),
                        row.get("megapixels"),
                        row.get("message_bytes"),
                        row.get("step"),
                        row.get("classification"),
                        row.get("failure_domain"),
                        row.get("failure_category"),
                        row.get("failure_reason"),
                        _bool_to_int(row.get("embedding_succeeded", row.get("embed_ok"))),
                        _bool_to_int(row.get("payload_recovered", row.get("verify_ok"))),
                        _bool_to_int(row.get("decoded_match")),
                        row.get("snr"),
                        row.get("embed_time_ms"),
                        row.get("verify_time_ms"),
                        row.get("output_file_size"),
                    )
                )

            conn.executemany(
                """
                INSERT INTO cases (
                    run_id, protocol, image, input_file, output_file,
                    width, height, min_dimension, max_dimension, aspect_ratio, megapixels,
                    message_bytes, step, classification, failure_domain, failure_category, failure_reason,
                    embedding_succeeded, payload_recovered, decoded_match, snr, embed_ms, verify_ms, output_size_bytes
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                case_rows,
            )

            summary_rows = []
            failure_rows = []
            for protocol, summary in summaries_by_protocol.items():
                summary_rows.append(
                    (
                        run_id,
                        protocol,
                        summary.get("total_cases"),
                        summary.get("support_rate"),
                        summary.get("expected_rejection_rate"),
                        summary.get("recovery_failure_rate"),
                        summary.get("runtime_failure_rate"),
                        summary.get("embedding_success_rate", summary.get("embed_success_rate")),
                        summary.get("payload_recovery_rate", summary.get("verify_success_rate")),
                        summary.get("average_embedding_time_ms", summary.get("average_embed_time_ms")),
                        summary.get("average_payload_recovery_time_ms", summary.get("average_verify_time_ms")),
                        summary.get("average_snr"),
                    )
                )
                for category, count in sorted(summary.get("failure_category_breakdown", {}).items()):
                    failure_rows.append((run_id, protocol, category, count))

            conn.executemany(
                """
                INSERT INTO summaries (
                    run_id, protocol, total_cases, support_rate, expected_rejection_rate,
                    recovery_failure_rate, runtime_failure_rate, embedding_success_rate, payload_recovery_rate,
                    average_embedding_ms, average_payload_recovery_ms, average_snr
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                summary_rows,
            )

            if failure_rows:
                conn.executemany(
                    """
                    INSERT INTO failure_categories (
                        run_id, protocol, failure_category, count
                    ) VALUES (?, ?, ?, ?)
                    """,
                    failure_rows,
                )
    finally:
        conn.close()

    return run_id
