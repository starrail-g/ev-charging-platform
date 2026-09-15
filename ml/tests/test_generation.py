from __future__ import annotations

import hashlib
import json
import sqlite3
import subprocess
import sys
from pathlib import Path


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
    output = tmp_path / "snapshot.json"
    subprocess.run([sys.executable, str(SNAPSHOT), "--database", str(database), "--output", str(output)],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    analytics = json.loads(output.read_text(encoding="utf-8"))["analytics"]
    assert {"users", "equipment", "orders", "energy", "revenue", "stations", "service"} <= analytics.keys()
    assert analytics["orders"]["total"] >= analytics["orders"]["completed"]
    assert analytics["energy"]["total_kwh"] >= 0


def test_dashboard_snapshot_preserves_zero_day_rfm_recency(tmp_path: Path) -> None:
    database, _ = generate(tmp_path / "rfm", seed=44)
    output = tmp_path / "rfm-snapshot.json"
    subprocess.run([sys.executable, str(SNAPSHOT), "--database", str(database), "--output", str(output)],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    users = json.loads(output.read_text(encoding="utf-8"))["analytics"]["user_mining"]["top_users"]
    assert users
    assert any(row["recency_days"] == 0 for row in users)
