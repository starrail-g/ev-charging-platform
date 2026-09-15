from __future__ import annotations

import hashlib
import json
import sqlite3
import subprocess
import sys
from datetime import datetime, timedelta
from pathlib import Path

import pyarrow as pa
import pyarrow.parquet as pq
import pytest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "ml/data/generate_analysis_dataset.py"
SNAPSHOT = ROOT / "ml/service/build_dashboard_snapshot.py"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(tmp_path: Path, seed: int = 7) -> tuple[Path, Path]:
    database = tmp_path / "analysis.sqlite"
    ods = tmp_path / "ods"
    subprocess.run([
        sys.executable, str(GENERATOR), "--output", str(database), "--ods-dir", str(ods),
        "--seed", str(seed), "--days", "10", "--users", "4", "--stations", "2",
        "--piles-per-station", "3", "--orders", "30", "--end-date", "2026-09-17",
    ], cwd=ROOT, check=True, capture_output=True, text=True)
    return database, ods


def write_dws_stub(tmp_path: Path, rows: list[dict]) -> Path:
    """构造最小 DWS station_day 台账（快照利用率现在只认 DWS 产物，不再回落业务库推算）。"""
    dws = tmp_path / "dws"
    (dws / "station_day").mkdir(parents=True, exist_ok=True)
    table = pa.table({
        "station_id": [row["station_id"] for row in rows],
        "charge_seconds": [row["charge_seconds"] for row in rows],
        "capacity_pile_seconds": [row["capacity_pile_seconds"] for row in rows],
    })
    pq.write_table(table, dws / "station_day" / "part-0.parquet")
    return dws


def test_schema_and_ledger_are_consistent(tmp_path: Path) -> None:
    database, ods = generate(tmp_path)
    connection = sqlite3.connect(database)
    assert connection.execute("SELECT value FROM schema_meta WHERE key='schema_version'").fetchone() == ("0.4",)
    assert connection.execute("PRAGMA foreign_key_check").fetchall() == []
    assert connection.execute("PRAGMA integrity_check").fetchone() == ("ok",)
    completed = connection.execute("SELECT COUNT(*) FROM charging_orders WHERE id >= 100001 AND status='completed'").fetchone()[0]
    cancelled = connection.execute("SELECT COUNT(*) FROM charging_orders WHERE id >= 100001 AND status='cancelled'").fetchone()[0]
    exception = connection.execute("SELECT COUNT(*) FROM charging_orders WHERE id >= 100001 AND status='exception'").fetchone()[0]
    assert completed + cancelled + exception == 30
    assert 0 < completed < 30
    assert connection.execute("SELECT COUNT(*) FROM wallet_transactions WHERE id >= 200001 AND transaction_type='charge'").fetchone()[0] == completed
    assert connection.execute("SELECT COALESCE(SUM(revenue_cents), 0) FROM revenue_daily WHERE revenue_date >= '2026-09-07'").fetchone()[0] > 0
    connection.close()
    assert len(ods.joinpath("quality_events.jsonl").read_text(encoding="utf-8").splitlines()) == 4


def test_generation_is_reproducible(tmp_path: Path) -> None:
    first_db, first_ods = generate(tmp_path / "first", seed=33)
    second_db, second_ods = generate(tmp_path / "second", seed=33)
    assert digest(first_db) == digest(second_db)
    assert digest(first_ods / "charging_orders.csv") == digest(second_ods / "charging_orders.csv")
    assert json.loads((first_ods / "manifest.json").read_text())['metadata'] == json.loads((second_ods / "manifest.json").read_text())['metadata']


def test_dashboard_snapshot_exposes_stage2_analysis_domains(tmp_path: Path) -> None:
    database, _ = generate(tmp_path / "snapshot", seed=44)
    dws = write_dws_stub(tmp_path / "snapshot",
                         [{"station_id": 101, "charge_seconds": 3600, "capacity_pile_seconds": 7200}])
    output = tmp_path / "snapshot.json"
    subprocess.run([sys.executable, str(SNAPSHOT), "--database", str(database), "--dws", str(dws),
                    "--output", str(output)],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    analytics = json.loads(output.read_text(encoding="utf-8"))["analytics"]
    assert {"users", "equipment", "orders", "energy", "revenue", "stations", "service"} <= analytics.keys()
    assert analytics["orders"]["total"] >= analytics["orders"]["completed"]
    assert analytics["energy"]["total_kwh"] >= 0


def test_dashboard_snapshot_preserves_zero_day_rfm_recency(tmp_path: Path) -> None:
    database, _ = generate(tmp_path / "rfm", seed=44)
    dws = write_dws_stub(tmp_path / "rfm",
                         [{"station_id": 101, "charge_seconds": 3600, "capacity_pile_seconds": 7200}])
    output = tmp_path / "rfm-snapshot.json"
    subprocess.run([sys.executable, str(SNAPSHOT), "--database", str(database), "--dws", str(dws),
                    "--output", str(output)],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    users = json.loads(output.read_text(encoding="utf-8"))["analytics"]["user_mining"]["top_users"]
    assert users
    assert any(row["recency_days"] == 0 for row in users)


def test_dashboard_snapshot_utilization_consumes_dws(tmp_path: Path) -> None:
    """#4：快照利用率直接消费 DWS station_day——台账给定即得预期值，与链上严格同源。"""
    database, _ = generate(tmp_path / "utilization", seed=55)
    dws = write_dws_stub(tmp_path / "utilization", [
        {"station_id": 101, "charge_seconds": 1800, "capacity_pile_seconds": 7200},
        {"station_id": 101, "charge_seconds": 600, "capacity_pile_seconds": 7200},
        {"station_id": 102, "charge_seconds": 0, "capacity_pile_seconds": 14400},
    ])
    output = tmp_path / "utilization-snapshot.json"
    subprocess.run([sys.executable, str(SNAPSHOT), "--database", str(database), "--dws", str(dws),
                    "--output", str(output)],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    snapshot = json.loads(output.read_text(encoding="utf-8"))
    by_id = {row["station_id"]: row for row in snapshot["stationUtilization"]}
    assert by_id[101]["charge_seconds"] == 2400
    assert by_id[101]["capacity_pile_seconds"] == 14400
    assert by_id[101]["utilization"] == round(min(1.0, 2400 / 14400), 4)    # 0.1667
    assert by_id[102]["utilization"] == 0.0 and by_id[102]["capacity_pile_seconds"] == 14400
    assert by_id[1]["utilization"] == 0.0        # 不在 DWS 台账的站点 → 0（不回落业务库推算）
    rows = snapshot["stationUtilization"]
    mean = sum(r["utilization"] for r in rows) / max(1, len(rows))
    assert snapshot["overview"]["avgStationUtilization"] == pytest.approx(round(mean, 4), abs=1e-4)
    # analytics.stations 的 utilization 与 stationUtilization 同源（同一 DWS 台账）
    station_facts = {row["station_id"]: row for row in snapshot["analytics"]["stations"]}
    assert station_facts[101]["utilization"] == round(min(1.0, 2400 / 14400), 4)
