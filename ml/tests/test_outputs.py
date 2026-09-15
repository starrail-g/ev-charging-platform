from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
from pathlib import Path

import pandas as pd


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ml/models/build_outputs.py"


def _load_module():
    spec = importlib.util.spec_from_file_location("build_outputs", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


BUILD_OUTPUTS = _load_module()


def _run(rows: list, tmp_path: Path) -> dict:
    features = tmp_path / "features"
    output = tmp_path / "ads"
    pd.DataFrame(rows).to_parquet(features)
    subprocess.run([sys.executable, str(SCRIPT), "--features", str(features), "--output", str(output)],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    return json.loads((output / "forecast.json").read_text(encoding="utf-8"))


def _by_horizon(forecast: dict) -> dict:
    return {item["horizon_hours"]: item for item in forecast["items"]}


def test_outputs_accept_ads_feature_schema(tmp_path: Path) -> None:
    forecast = _run([
        {"date": "2026-09-10", "start_hour": 8, "station_id": 1, "load_kwh": 20.0,
         "session_count": 2, "user_count": 2, "utilization": 0.4, "idle_pile_estimate": 6},
        {"date": "2026-09-11", "start_hour": 8, "station_id": 1, "load_kwh": 24.0,
         "session_count": 3, "user_count": 3, "utilization": 0.5, "idle_pile_estimate": 5},
        {"date": "2026-09-12", "start_hour": 8, "station_id": 1, "load_kwh": 22.0,
         "session_count": 2, "user_count": 2, "utilization": 0.45, "idle_pile_estimate": 6},
    ], tmp_path)
    assert len(forecast["items"]) == 3
    assert {item["horizon_hours"] for item in forecast["items"]} == {1, 6, 24}
    recommendations = json.loads((tmp_path / "ads" / "recommendations.json").read_text())
    assert len(recommendations["items"]) == 1
    # last_hour=8 → h=1/6 取各自目标小时（无样本 → 站级回退），h=24 命中同小时样本
    items = _by_horizon(forecast)
    assert items[1]["target_hour"] == 9 and items[1]["fallback_level"] == "station"
    assert items[6]["target_hour"] == 14 and items[6]["fallback_level"] == "station"
    assert items[24]["target_hour"] == 8 and items[24]["fallback_level"] == "same_hour"
    assert items[24]["predicted_value"] == 22.0


def test_forecast_horizons_pick_their_own_target_hour(tmp_path: Path) -> None:
    """小时差异显著的数据：三档各取 (last_hour + h) % 24 的目标小时样本，而非站级均值。"""
    rows = []
    for day in ("2026-09-08", "2026-09-09", "2026-09-10"):
        for hour in range(24):
            rows.append({"date": day, "start_hour": hour, "station_id": 1,
                         "load_kwh": float(hour + 1), "session_count": 1, "user_count": 1,
                         "utilization": 0.1, "idle_pile_estimate": 5})
    forecast = _run(rows, tmp_path)
    items = _by_horizon(forecast)
    # 最后一行 = 09-10 23:00 → 目标小时 0 / 5 / 23，取值 = 目标小时负荷（hour + 1）
    assert (items[1]["target_hour"], items[1]["predicted_value"]) == (0, 1.0)
    assert (items[6]["target_hour"], items[6]["predicted_value"]) == (5, 6.0)
    assert (items[24]["target_hour"], items[24]["predicted_value"]) == (23, 24.0)


def test_forecast_prefers_same_weekday_hour_samples(tmp_path: Path) -> None:
    """基线优先级：同星期同小时（≥3 条）优先于同小时全星期样本（指南 §4.1）。"""
    rows = []
    for day, load in (("2026-09-07", 60.0), ("2026-09-14", 70.0), ("2026-09-21", 80.0)):
        rows.append({"date": day, "start_hour": 9, "station_id": 2, "load_kwh": load,
                     "session_count": 2, "user_count": 2, "utilization": 0.2, "idle_pile_estimate": 4})
    for day, load in (("2026-09-11", 30.0), ("2026-09-18", 40.0), ("2026-09-25", 50.0)):
        rows.append({"date": day, "start_hour": 9, "station_id": 2, "load_kwh": load,
                     "session_count": 2, "user_count": 2, "utilization": 0.2, "idle_pile_estimate": 4})
    rows.append({"date": "2026-09-28", "start_hour": 8, "station_id": 2, "load_kwh": 7.0,
                 "session_count": 1, "user_count": 1, "utilization": 0.1, "idle_pile_estimate": 5})
    forecast = _run(rows, tmp_path)
    item = _by_horizon(forecast)[1]     # 目标 = 09-28 09:00（周一 hour 9）
    assert item["target_hour"] == 9
    assert item["fallback_level"] == "same_weekday_hour"
    assert item["sample_count"] == 3
    # 周一 hour9 均值 = 70.0；若退化为全星期同小时样本会得到 55.0
    assert item["predicted_value"] == 70.0


def test_forecast_flat_data_keeps_target_hours_with_equal_values(tmp_path: Path) -> None:
    """平稳数据三档数值相同不算失败——三档仍须各自选中正确的目标小时。"""
    rows = []
    for day in ("2026-09-08", "2026-09-09", "2026-09-10"):
        for hour in range(24):
            rows.append({"date": day, "start_hour": hour, "station_id": 3, "load_kwh": 5.0,
                         "session_count": 1, "user_count": 1, "utilization": 0.1, "idle_pile_estimate": 5})
    forecast = _run(rows, tmp_path)
    items = _by_horizon(forecast)
    assert [items[h]["predicted_value"] for h in (1, 6, 24)] == [5.0, 5.0, 5.0]
    assert [items[h]["target_hour"] for h in (1, 6, 24)] == [0, 5, 23]


def test_model_inference_rows_follow_target_hours() -> None:
    """模型路径推理行：hour_num 自数据截止推进；状态特征取最近有效活动行（零尾行不退化）。"""
    frame = pd.DataFrame([
        {"date": pd.Timestamp("2026-09-10", tz="UTC"), "start_hour": 12, "station_id": 1,
         "load_kwh": 9.0, "session_count": 3, "user_count": 2, "utilization": 0.25, "idle_pile_estimate": 4},
        {"date": pd.Timestamp("2026-09-10", tz="UTC"), "start_hour": 23, "station_id": 1,
         "load_kwh": 3.0, "session_count": 2, "user_count": 1, "utilization": 0.2, "idle_pile_estimate": 5},
        {"date": pd.Timestamp("2026-09-11", tz="UTC"), "start_hour": 0, "station_id": 1,
         "load_kwh": 0.0, "session_count": 0, "user_count": 0, "utilization": 0.0, "idle_pile_estimate": 8},
    ])
    rows = BUILD_OUTPUTS.horizon_inference_rows(frame)
    # 截止 = 09-11 00:00 → 目标小时 1 / 6 / 0
    assert [(row["horizon_hours"], row["hour_num"]) for row in rows] == [(1.0, 1.0), (6.0, 6.0), (24.0, 0.0)]
    # 状态特征来自最近活动行（hour 23：session 2 / user 1 / util 0.2 / idle 5），不是零尾行
    assert all(row["session_num"] == 2.0 and row["user_num"] == 1.0
               and row["utilization_num"] == 0.2 and row["idle_num"] == 5.0 for row in rows)
    assert BUILD_OUTPUTS.target_hour_for(23, 1) == 0
    assert BUILD_OUTPUTS.target_hour_for(23, 24) == 23


def test_forecast_weekday_follows_cross_day_target(tmp_path: Path) -> None:
    """跨日修正：截止周一 23:00，h=1 的目标是周二 00:00——必须取周二样本（100），不是当天样本。"""
    rows = []
    for day in ("2026-09-01", "2026-09-08", "2026-09-15"):      # 周二 00:00 样本 = 100
        rows.append({"date": day, "start_hour": 0, "station_id": 9, "load_kwh": 100.0,
                     "session_count": 2, "user_count": 2, "utilization": 0.3, "idle_pile_estimate": 4})
    for day in ("2026-09-07", "2026-09-14", "2026-09-21"):      # 周一 23:00 样本 = 10（含截止行）
        rows.append({"date": day, "start_hour": 23, "station_id": 9, "load_kwh": 10.0,
                     "session_count": 1, "user_count": 1, "utilization": 0.1, "idle_pile_estimate": 6})
    forecast = _run(rows, tmp_path)
    item = _by_horizon(forecast)[1]
    assert item["target_hour"] == 0
    assert item["fallback_level"] == "same_weekday_hour"
    assert item["predicted_value"] == 100.0      # 旧实现漏加 last_hour 会落到站级均值 55


def test_forecast_weekday_follows_cross_week_target(tmp_path: Path) -> None:
    """跨周修正：截止周日 23:00，h=1 跨入新一周的周一 00:00——取周一 00:00 样本（100）。"""
    rows = []
    for day in ("2026-08-31", "2026-09-07", "2026-09-14"):      # 周一 00:00 样本 = 100
        rows.append({"date": day, "start_hour": 0, "station_id": 9, "load_kwh": 100.0,
                     "session_count": 2, "user_count": 2, "utilization": 0.3, "idle_pile_estimate": 4})
    for day in ("2026-09-06", "2026-09-13", "2026-09-20"):      # 周日 23:00 样本 = 10（含截止行）
        rows.append({"date": day, "start_hour": 23, "station_id": 9, "load_kwh": 10.0,
                     "session_count": 1, "user_count": 1, "utilization": 0.1, "idle_pile_estimate": 6})
    forecast = _run(rows, tmp_path)
    item = _by_horizon(forecast)[1]
    assert item["target_hour"] == 0
    assert item["fallback_level"] == "same_weekday_hour"
    assert item["predicted_value"] == 100.0      # 旧实现会落到站级均值 55
