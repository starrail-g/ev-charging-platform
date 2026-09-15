#!/usr/bin/env python3
"""Build forecast, recommendation and alert JSON from DWS hourly features."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pandas as pd


UTC = timezone.utc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--features", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--model-version", default="baseline-20260914")
    parser.add_argument("--model", type=Path, help="optional Spark MLlib PipelineModel directory")
    args = parser.parse_args()
    frame = pd.read_parquet(args.features)
    if frame.empty:
        raise ValueError("feature dataset is empty")
    # ADS load_features intentionally omits device_count; DWS station_hourly
    # includes it. Preserve the same idle-rate semantics across both inputs.
    if "device_count" not in frame.columns:
        if "idle_pile_estimate" not in frame.columns:
            raise ValueError("features require device_count or idle_pile_estimate")
        session_counts = frame["session_count"] if "session_count" in frame.columns else pd.Series(0, index=frame.index)
        frame["device_count"] = (
            pd.to_numeric(frame["idle_pile_estimate"], errors="coerce").fillna(0)
            + pd.to_numeric(session_counts, errors="coerce").fillna(0)
        ).clip(lower=1)
    if "event_date" in frame.columns and "date" not in frame.columns:
        frame["date"] = frame["event_date"]
    frame["date"] = pd.to_datetime(frame["date"], utc=True)
    cutoff = frame["date"].max() + pd.Timedelta(hours=int(frame.loc[frame["date"].idxmax(), "start_hour"]))
    generated = datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
    model_predictions: dict[int, float] = {}
    prediction_source = "deterministic_baseline"
    if args.model:
        from pyspark.ml import PipelineModel
        from pyspark.sql import Row, SparkSession

        spark = (SparkSession.builder.master("local[2]")
                 .appName("ev-charging-load-output")
                 .config("spark.sql.session.timeZone", "UTC")
                 .config("spark.ui.enabled", "false").getOrCreate())
        spark.sparkContext.setLogLevel("WARN")
        try:
            inference = frame[["station_id", "start_hour", "session_count", "user_count",
                               "utilization", "idle_pile_estimate"]].copy()
            inference["hour_num"] = pd.to_numeric(inference["start_hour"], errors="coerce")
            inference["station_num"] = pd.to_numeric(inference["station_id"], errors="coerce")
            inference["session_num"] = pd.to_numeric(inference["session_count"], errors="coerce")
            inference["user_num"] = pd.to_numeric(inference["user_count"], errors="coerce")
            inference["utilization_num"] = pd.to_numeric(inference["utilization"], errors="coerce").fillna(0.0)
            inference["idle_num"] = pd.to_numeric(inference["idle_pile_estimate"], errors="coerce").fillna(0.0)
            rows = [Row(station_id=int(row.station_id), hour_num=float(row.hour_num),
                        station_num=float(row.station_num), session_num=float(row.session_num),
                        user_num=float(row.user_num), utilization_num=float(row.utilization_num),
                        idle_num=float(row.idle_num)) for row in inference.itertuples(index=False)]
            predictions = PipelineModel.load(str(args.model)).transform(
                spark.createDataFrame(rows)
            ).select("station_id", "prediction").groupBy("station_id").avg("prediction").collect()
            model_predictions = {int(row["station_id"]): max(0.0, float(row["avg(prediction)"])) for row in predictions}
            prediction_source = "spark_mllib_random_forest"
        finally:
            spark.stop()
    items: list[dict[str, object]] = []
    station_rows: list[dict[str, object]] = []
    for station_id, group in frame.groupby("station_id"):
        group = group.sort_values(["date", "start_hour"])
        last = group.iloc[-1]
        for horizon in (1, 6, 24):
            same_hour = group[group["start_hour"] == int(last["start_hour"])]
            sample = same_hour if len(same_hour) >= 3 else group
            predicted_load = model_predictions.get(int(station_id), float(sample["load_kwh"].tail(8).mean()))
            predicted_sessions = float(sample["session_count"].tail(8).mean())
            idle = max(0.0, float(last["device_count"]) - predicted_sessions)
            idle_rate = min(1.0, idle / max(1.0, float(last["device_count"])))
            items.append({
                "station_id": int(station_id), "horizon_hours": horizon, "target": "load_kwh",
                "predicted_value": round(max(0.0, predicted_load), 3), "unit": "kWh",
                "sample_count": int(len(sample)), "fallback_level": "same_hour" if len(same_hour) >= 3 else "station",
                "generated_at": generated, "data_cutoff_at": cutoff.strftime("%Y-%m-%dT%H:%M:%SZ"),
                "model_version": args.model_version, "prediction_source": prediction_source, "status": "ok",
            })
            if horizon == 1:
                station_rows.append({"station_id": int(station_id), "predicted_load": predicted_load,
                                     "predicted_idle_rate": idle_rate, "device_count": int(last["device_count"])})

    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    (output / "forecast.json").write_text(json.dumps({"status": "ok", "generated_at": generated,
        "data_cutoff_at": cutoff.strftime("%Y-%m-%dT%H:%M:%SZ"), "timezone": "UTC",
        "model_version": args.model_version, "items": items}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    loads = [row["predicted_load"] for row in station_rows]
    max_load = max(loads) if loads else 1.0
    recommendations = []
    for row in sorted(station_rows, key=lambda value: (-(value["predicted_idle_rate"]), value["predicted_load"], value["station_id"]))[:3]:
        recommendations.append({"station_id": row["station_id"], "predicted_load": round(row["predicted_load"], 3),
            "predicted_idle_rate": round(row["predicted_idle_rate"], 3),
            "score": round(0.5 * row["predicted_idle_rate"] - 0.3 * row["predicted_load"] / max_load, 4),
            "reasons": ["预测空闲率高", "未来 1 小时负荷可观测"], "generated_at": generated,
            "model_version": args.model_version})
    (output / "recommendations.json").write_text(json.dumps({"status": "ok", "generated_at": generated,
        "data_cutoff_at": cutoff.strftime("%Y-%m-%dT%H:%M:%SZ"), "model_version": args.model_version,
        "items": recommendations}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    alerts = []
    p95 = float(pd.Series(loads).quantile(0.95)) if loads else 0.0
    for row in station_rows:
        if row["predicted_load"] >= p95 and p95 > 0:
            alerts.append({"station_id": row["station_id"], "horizon_hours": 1, "level": "high",
                "threshold": round(p95, 3), "predicted_value": round(row["predicted_load"], 3),
                "generated_at": generated, "data_cutoff_at": cutoff.strftime("%Y-%m-%dT%H:%M:%SZ"),
                "dedupe_key": f"{row['station_id']}:1:{cutoff.isoformat()}:high", "status": "open",
                "model_version": args.model_version, "reason": "历史 P95 基线预警"})
    (output / "alerts.json").write_text(json.dumps({"status": "ok", "generated_at": generated,
        "data_cutoff_at": cutoff.strftime("%Y-%m-%dT%H:%M:%SZ"), "model_version": args.model_version,
        "threshold_type": "historical_p95", "items": alerts}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    training_metadata = {}
    if args.model:
        metadata_path = args.model.parent / "metadata.json"
        if metadata_path.exists():
            try:
                training_metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                training_metadata = {}
    model_metadata = {"model_version": args.model_version,
        "algorithm": "Spark MLlib RandomForestRegressor" if args.model else "deterministic station-hour baseline",
        "prediction_source": prediction_source, "generated_at": generated,
        "data_cutoff_at": cutoff.strftime("%Y-%m-%dT%H:%M:%SZ"), "targets": ["load_kwh", "idle_pile_estimate"]}
    # Preserve the time-ordered train/validation evidence for the dashboard;
    # the output remains valid for the deterministic fallback with no model.
    for key in ("metrics", "training_rows", "validation_rows", "feature_columns", "training_start", "training_end"):
        if key in training_metadata:
            model_metadata[key] = training_metadata[key]
    (output / "model_metadata.json").write_text(json.dumps(model_metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"forecast_items": len(items), "recommendations": len(recommendations), "alerts": len(alerts), "output": str(output)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
