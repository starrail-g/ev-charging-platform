from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import pandas as pd


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ml/models/build_outputs.py"


def test_outputs_accept_ads_feature_schema(tmp_path: Path) -> None:
    features = tmp_path / "features"
    output = tmp_path / "ads"
    pd.DataFrame([
        {"date": "2026-09-10", "start_hour": 8, "station_id": 1, "load_kwh": 20.0,
         "session_count": 2, "user_count": 2, "utilization": 0.4, "idle_pile_estimate": 6},
        {"date": "2026-09-11", "start_hour": 8, "station_id": 1, "load_kwh": 24.0,
         "session_count": 3, "user_count": 3, "utilization": 0.5, "idle_pile_estimate": 5},
        {"date": "2026-09-12", "start_hour": 8, "station_id": 1, "load_kwh": 22.0,
         "session_count": 2, "user_count": 2, "utilization": 0.45, "idle_pile_estimate": 6},
    ]).to_parquet(features)
    subprocess.run([sys.executable, str(SCRIPT), "--features", str(features), "--output", str(output)],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    forecast = json.loads((output / "forecast.json").read_text())
    assert len(forecast["items"]) == 3
    assert {item["horizon_hours"] for item in forecast["items"]} == {1, 6, 24}
    assert len(json.loads((output / "recommendations.json").read_text())["items"]) == 1
