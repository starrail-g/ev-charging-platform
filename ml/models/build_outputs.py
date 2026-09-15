#!/usr/bin/env python3
"""Build forecast, recommendation and alert JSON from DWS hourly features.

预测语义：predicted_value = 未来第 horizon_hours 小时（UTC 小时 = (截止小时 + h) % 24）
的站级负荷估计（load_kwh），不是 h 小时累计电量；1/6/24 三档各自取目标时刻的历史样本。
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pandas as pd


UTC = timezone.utc

HORIZONS = (1, 6, 24)


def _num(value: object, default: float = 0.0) -> float:
    parsed = pd.to_numeric(value, errors="coerce")
    return default if pd.isna(parsed) else float(parsed)


def target_hour_for(last_hour: int, horizon: int) -> int:
    """未来第 horizon 小时对应的目标时刻（UTC 小时 0–23）。"""
    return (int(last_hour) + int(horizon)) % 24


def horizon_sample(group: pd.DataFrame, target_hour: int, target_weekday: int) -> tuple[pd.DataFrame, str]:
    """指南 §4.1 的基线下钻链：同站点同星期同小时 → 同站点同小时 → 同站点全样本。

    最长回退（全局小时均值）由调用方在样本仍不足 3 条时接管。
    """
    same_weekday_hour = group[
        (group["start_hour"] == int(target_hour))
        & (group["date"].dt.dayofweek == int(target_weekday))
    ]
    if len(same_weekday_hour) >= 3:
        return same_weekday_hour, "same_weekday_hour"
    same_hour = group[group["start_hour"] == int(target_hour)]
    if len(same_hour) >= 3:
        return same_hour, "same_hour"
    return group, "station"


def horizon_inference_rows(frame: pd.DataFrame) -> list[dict[str, float]]:
    """模型推理行：每个 station × horizon 一行。

    hour_num = 自数据截止时刻（全表最后一行的小时）推进 h 小时；其余状态特征取该站
    最近一条有效活动行（session_count > 0）——网格全量展开后尾行常为零订单行，直接用会退化成全零特征。
    """
    rows: list[dict[str, float]] = []
    working = frame.sort_values(["date", "start_hour"])
    cutoff_hour = int(pd.to_numeric(working.iloc[-1]["start_hour"], errors="coerce"))
    for station_id, group in working.groupby("station_id"):
        active = group[pd.to_numeric(group["session_count"], errors="coerce").fillna(0) > 0]
        state_row = active.iloc[-1] if len(active) else group.iloc[-1]
        for horizon in HORIZONS:
            rows.append({
                "station_id": float(int(station_id)),
                "horizon_hours": float(horizon),
                "hour_num": float(target_hour_for(cutoff_hour, horizon)),
                "station_num": float(int(station_id)),
                "session_num": _num(state_row["session_count"]),
                "user_num": _num(state_row["user_count"]),
                "utilization_num": _num(state_row["utilization"]),
                "idle_num": _num(state_row["idle_pile_estimate"]),
            })
    return rows


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
    # 数据截止 = 全表最后一个观测时刻（date + start_hour）；网格全量展开后须按行拼接
    cutoff = (
        frame["date"] + pd.to_timedelta(pd.to_numeric(frame["start_hour"], errors="coerce"), unit="h")
    ).max()
    generated = datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
    # 模型路径同样按目标时刻构造推理行（每个 station × horizon 一行），
    # 与基线的 target_hour 口径一致；键 = (station_id, 目标小时)。
    model_predictions: dict[tuple[int, int], float] = {}
    prediction_source = "deterministic_baseline"
    if args.model:
        from pyspark.ml import PipelineModel
        from pyspark.sql import Row, SparkSession

        spark = (SparkSession.builder.master("local[2]")
                 .appName("ev-charging-load-output")
                 .config("spark.sql.session.timeZone", "UTC")
                 .config("spark.ui.enabled", "false")
                 # ml 链为本地链：显式 file:// 文件系统，避免宿主 Hadoop 配置把本地路径解析到 HDFS
                 .config("spark.hadoop.fs.defaultFS", "file:///")
                 .getOrCreate())
        spark.sparkContext.setLogLevel("WARN")
        try:
            rows = [Row(station_id=int(row["station_id"]), hour_num=row["hour_num"],
                        station_num=row["station_num"], session_num=row["session_num"],
                        user_num=row["user_num"], utilization_num=row["utilization_num"],
                        idle_num=row["idle_num"]) for row in horizon_inference_rows(frame)]
            predictions = PipelineModel.load(str(args.model)).transform(
                spark.createDataFrame(rows)
            ).select("station_id", "hour_num", "prediction").collect()
            for row in predictions:
                model_predictions[(int(row["station_id"]), int(round(float(row["hour_num"]))))] = \
                    max(0.0, float(row["prediction"]))
            prediction_source = "spark_mllib_random_forest"
        finally:
            spark.stop()
    items: list[dict[str, object]] = []
    station_rows: list[dict[str, object]] = []
    global_hour_mean = frame.groupby("start_hour")["load_kwh"].mean()
    for station_id, group in frame.groupby("station_id"):
        group = group.sort_values(["date", "start_hour"])
        last = group.iloc[-1]
        last_hour = int(last["start_hour"])
        station_devices = max(1.0, float(last["device_count"]))
        for horizon in HORIZONS:
            # 预测 = 未来第 horizon 小时（UTC）的站级负荷：目标时刻 = 完整截止时刻
            # （date + last_hour）再推进 h 小时——跨日/跨周时星期必须随目标时刻走
            # （漏加 last_hour 会取错星期样本：23 时截止时 +1h 的星期仍是当天）。
            target_ts = last["date"] + pd.Timedelta(hours=last_hour + int(horizon))
            target_hour = target_hour_for(last_hour, horizon)
            sample, fallback_level = horizon_sample(group, target_hour, int(target_ts.dayofweek))
            if len(sample) >= 3:
                predicted_load = _num(sample["load_kwh"].tail(8).mean())
            else:
                # 指南 §4.1 最后一级回退：全局（跨站点）目标小时均值
                predicted_load = _num(global_hour_mean.get(target_hour, 0.0))
                fallback_level = "global_hour"
            model_prediction = model_predictions.get((int(station_id), target_hour))
            if model_prediction is not None:
                predicted_load = model_prediction
            predicted_sessions = _num(sample["session_count"].tail(8).mean())
            idle = max(0.0, float(last["device_count"]) - predicted_sessions)
            idle_rate = min(1.0, idle / station_devices)
            items.append({
                "station_id": int(station_id), "horizon_hours": horizon, "target_hour": target_hour,
                "target": "load_kwh",
                "predicted_value": round(max(0.0, predicted_load), 3), "unit": "kWh",
                "sample_count": int(len(sample)), "fallback_level": fallback_level,
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
