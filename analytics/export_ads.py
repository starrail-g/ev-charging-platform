#!/usr/bin/env python3
"""ADS → 大屏发布 JSON（只读快照 + 原子发布）。

用法（VM）:
  python3 analytics/export_ads.py \
    --analytics-root ~/ev-stage2-artifacts --batch-id s2-smoke-20260915 \
    [--verify-only]

读取 batches/<batch>/{manifest.json, quality/clean_report.json} 与 HDFS/本地 ADS 目录
（--ads-uri 指定, 默认 hdfs:///ev-stage2/batches/<batch>/ads），产出:
  published/<batch>/dashboard.json   大屏全量投影（全窗口, 不分站过滤; API 在此之上过滤）
  published/<batch>/quality.json     质量报告（清洗守恒 + 规则统计 + 污染注入统计）
  published/<batch>/manifest.json    批次清单副本
  published/latest.json              指针（校验通过后原子切换; --verify-only 不切换）

dashboard.json 的 meta 同时发布 available_start/available_end_exclusive = 发布覆盖窗口
（生成窗口 ∪ 末端结算追加日）：API 默认查询窗口、覆盖校验与前端筛选范围都以此为准，
保证“发布 overview 里有的收入，对应结算日一定可查询”（PR #24 评审 P2）。

安全: 全部严格 JSON; 发布采用 写临时文件 + os.replace 原子替换。
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path

from pyspark.sql import SparkSession
from pyspark.sql import functions as F

try:  # 包导入（测试/被 import）
    from .publish_coverage import coverage_end_exclusive
except ImportError:  # pragma: no cover - 脚本式直跑（python3 analytics/export_ads.py）
    from publish_coverage import coverage_end_exclusive

UTC = timezone.utc


def dump_json(path: Path, obj) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(obj, fh, ensure_ascii=False, indent=2, allow_nan=False)
    os.replace(tmp, path)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--analytics-root", required=True, help="EV_ANALYTICS_ROOT")
    ap.add_argument("--batch-id", required=True)
    ap.add_argument("--ads-uri", default=None, help="ADS parquet 根（默认 hdfs:///ev-stage2/batches/<batch>/ads）")
    ap.add_argument("--verify-only", action="store_true", help="只生成到 batches 下, 不更新 latest")
    args = ap.parse_args(argv)

    root = Path(args.analytics_root).expanduser()
    batch_dir = root / "batches" / args.batch_id
    manifest = json.loads((batch_dir / "manifest.json").read_text(encoding="utf-8"))
    clean_report_path = batch_dir / "quality" / "clean_report.json"
    clean_report = json.loads(clean_report_path.read_text(encoding="utf-8")) \
        if clean_report_path.exists() else None
    ads_uri = args.ads_uri or f"hdfs:///ev-stage2/batches/{args.batch_id}/ads"

    spark = (SparkSession.builder.appName(f"stage2-export-{args.batch_id}")
             .config("spark.sql.session.timeZone", "UTC")
             .getOrCreate())
    spark.sparkContext.setLogLevel("WARN")

    def read(name):
        return spark.read.parquet(f"{ads_uri}/{name}")

    overview = read("ads_overview").collect()[0].asDict()
    # 时间列在 Spark 侧格式化（session tz=UTC）：PySpark 收集到驱动后为「驱动本地时区」的
    # naive datetime，直接 str() 会在非 UTC 驱动机上产出错误字符串（实测踩坑）。
    trend = [r.asDict() for r in read("ads_revenue_trend").select(
        "batch_id", F.date_format("stat_date", "yyyy-MM-dd").alias("stat_date"),
        "station_id", "revenue_cents", "completed_orders", "energy_wh").collect()]
    load = [r.asDict() for r in read("ads_load_hour").select(
        "batch_id",
        F.date_format("hour_start", "yyyy-MM-dd'T'HH:mm:ss'Z'").alias("hour_start"),
        "station_id", "allocated_wh", "load_kw", "charge_seconds",
        "capacity_pile_seconds").collect()]
    rank = [r.asDict() for r in read("ads_station_rank").collect()]
    snapshot = [r.asDict() for r in read("ads_pile_snapshot").collect()]
    user_activity = [r.asDict() for r in read("ads_user_activity").select(
        "user_id", "station_id", "frequency", "monetary",
        F.date_format("stat_date", "yyyy-MM-dd").alias("stat_date"),
        F.date_format("last_settled_at", "yyyy-MM-dd'T'HH:mm:ss'Z'").alias("last_settled_at")
    ).orderBy("stat_date", "station_id", "user_id").collect()]
    order_activity = [r.asDict() for r in read("ads_order_activity").select(
        "station_id", "status", "start_hour", "order_count", "duration_seconds",
        "duration_le15", "duration_15_30", "duration_30_60", "duration_gt60",
        F.date_format("stat_date", "yyyy-MM-dd").alias("stat_date")
    ).orderBy("stat_date", "station_id", "status", "start_hour").collect()]
    total_users = int(read("ads_user_summary").collect()[0]["total_users"])

    # 站点维度来自快照聚合（ADS 内部派生, 不再回读 DWD）
    stations, pile_rows = {}, []
    for p in sorted(snapshot, key=lambda x: x["id"]):
        sid = p["station_id"]
        st = stations.setdefault(sid, {
            "id": sid,
            "name": p.get("station_name") or f"站点 {sid}",
            "latitude": p.get("latitude"),
            "longitude": p.get("longitude"),
            "pileCount": 0,
            "pileCounts": {"idle": 0, "reserved": 0, "charging": 0,
                           "fault": 0, "offline": 0}})
        st["pileCount"] += 1
        st["pileCounts"][p["status"]] = st["pileCounts"].get(p["status"], 0) + 1
        pile_rows.append({
            "id": p["id"], "stationId": p["station_id"], "code": p["pile_code"],
            "type": p["pile_type"], "powerKw": p["power_kw"],
            "unitPriceCentsPerKwh": p["unit_price_cents_per_kwh"], "status": p["status"],
            "totalChargeCount": p["total_charge_count"],
            "totalChargeSeconds": p["total_charge_seconds"]})

    util = {int(r["station_id"]): round(float(r["utilization"]), 6) for r in rank}

    # 发布覆盖窗口（评审 P2）：生成窗口 ∪ 末端结算追加日。trend/load 已按 Spark 侧格式化，
    # 这里只用日期前缀，因此不受驱动时区影响。
    available_end_date = coverage_end_exclusive(
        manifest["data_end_exclusive"],
        [r["stat_date"] for r in trend] + [r["hour_start"] for r in load])

    dashboard = {
        "status": "ok",
        "meta": {
            "batch_id": args.batch_id,
            "source_type": manifest["source_type"],
            "timezone": "UTC",
            "coverage": "complete",
            "data_start": manifest["data_start"],
            "data_end_exclusive": manifest["data_end_exclusive"],
            # 实际可查询范围（含追加日）：API 默认窗口/覆盖校验与前端筛选都以此为准，
            # 避免“收入在发布 overview 里、却查不到那一天”（PR #24 评审 P2）。
            "available_start": manifest["data_start"],
            "available_end_exclusive": f"{available_end_date}T00:00:00Z",
            "generated_at": datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "batch_generated_at": manifest["generated_at"],
            "is_realtime": False,
            "stale": False,
            "snapshot_at": overview.get("window_end_exclusive"),
            "collection_gaps": manifest.get("collection_gaps", []),
        },
        "data": {
            # 内部聚合事实只供 API 筛选；响应只返回计算后的工作台指标。
            "workbenchFacts": {"version": 2, "totalUsers": total_users,
                               "users": user_activity, "orders": order_activity},
            "overview": {
                "revenueCents": int(overview["revenue_cents"]),
                "completedOrders": int(overview["completed_orders"]),
                "energyWh": int(overview["energy_wh"]),
                "energyKwh": round(int(overview["energy_wh"]) / 1000.0, 3),
                "totalOrderCount": int(overview["total_order_count"]),
                "cancelledOrders": int(overview["cancelled_orders"]),
                "exceptionOrders": int(overview["exception_orders"]),
                "stationCount": int(overview["station_count"]),
                "pileCount": int(overview["pile_count"]),
                "avgStationUtilization": float(overview["avg_station_utilization"]),
            },
            "stations": [stations[k] for k in sorted(stations)],
            "piles": pile_rows,
            "stationUtilization": [{"stationId": k, "utilization": util.get(k, 0.0)}
                                   for k in sorted(stations)],
            "stationRank": [{
                "stationId": int(r["station_id"]), "rank": int(r["rank_no"]),
                "revenueCents": int(r["revenue_cents"]),
                "completedOrders": int(r["completed_orders"]),
                "energyWh": int(r["energy_wh"]),
                "utilization": round(float(r["utilization"]), 6),
            } for r in sorted(rank, key=lambda x: x["rank_no"])],
            "revenueDaily": [{
                "date": str(r["stat_date"]), "stationId": int(r["station_id"]),
                "revenueCents": int(r["revenue_cents"]),
                "completedOrders": int(r["completed_orders"]),
                "energyWh": int(r["energy_wh"]),
            } for r in sorted(trend, key=lambda x: (str(x["stat_date"]), x["station_id"]))],
            "loadHourly": [{
                "hourStart": str(r["hour_start"]), "stationId": int(r["station_id"]),
                "allocatedWh": round(float(r["allocated_wh"]), 6),
                "loadKw": round(float(r["load_kw"]), 6),
                "chargeSeconds": int(r["charge_seconds"]),
                "capacityPileSeconds": int(r["capacity_pile_seconds"]),
            } for r in sorted(load, key=lambda x: (str(x["hour_start"]), x["station_id"]))],
        },
    }
    if clean_report:
        tables = clean_report["tables"]
        by_table = {t: {"input": r["input"], "kept": r["kept"], "duplicate": r["duplicate"],
                        "quarantine": r["quarantine"], "repaired": r["repaired"],
                        "conservation_ok": r["conservation_ok"]}
                    for t, r in sorted(tables.items())}
        totals = {k: sum(r[k] for r in tables.values())
                  for k in ("input", "kept", "duplicate", "quarantine", "repaired")}
        dashboard["data"]["quality"] = {**totals, "byTable": by_table}

    pub = root / "published" / args.batch_id
    pub.mkdir(parents=True, exist_ok=True)
    dump_json(pub / "dashboard.json", dashboard)
    dump_json(pub / "quality.json", {
        "batch_id": args.batch_id,
        "generated_at": dashboard["meta"]["generated_at"],
        "tables": clean_report["tables"] if clean_report else {},
        "pollution": manifest.get("pollution", {}),
        "collection_gaps": manifest.get("collection_gaps", []),
    })
    shutil.copyfile(batch_dir / "manifest.json", pub / "manifest.json")

    # 自校验：重新读回 + 关键字段
    check = json.loads((pub / "dashboard.json").read_text(encoding="utf-8"))
    assert check["data"]["overview"]["revenueCents"] == dashboard["data"]["overview"]["revenueCents"]
    assert len(check["data"]["piles"]) == len(pile_rows)
    # 覆盖窗口自检（评审 P2）：全部已发布行必须落在可查询窗口内——否则又会出现
    # “收入进了 overview、对应日期却查不到”的边界缺口。
    window_end = check["meta"]["available_end_exclusive"][:10]
    assert window_end >= check["meta"]["data_end_exclusive"][:10]
    assert all(row["date"] < window_end for row in check["data"]["revenueDaily"])
    assert all(row["hourStart"][:10] < window_end for row in check["data"]["loadHourly"])

    if not args.verify_only:
        dump_json(root / "published" / "latest.json", {
            "batch_id": args.batch_id,
            "published_at": dashboard["meta"]["generated_at"],
            "path": f"published/{args.batch_id}",
        })
        print(f"PUBLISHED {args.batch_id} -> published/latest.json", flush=True)
    else:
        print(f"VERIFY-ONLY wrote {pub} (latest unchanged)", flush=True)

    print("EXPORT_SUMMARY " + json.dumps({
        "stations": len(stations), "piles": len(pile_rows),
        "revenue_daily_rows": len(dashboard["data"]["revenueDaily"]),
        "load_hourly_rows": len(dashboard["data"]["loadHourly"]),
        "overview_revenue_cents": dashboard["data"]["overview"]["revenueCents"],
        "available_start": dashboard["meta"]["available_start"],
        "available_end_exclusive": dashboard["meta"]["available_end_exclusive"],
    }, ensure_ascii=False))
    spark.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
