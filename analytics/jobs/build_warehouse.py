#!/usr/bin/env python3
"""DWD → DWS/ADS 层（PySpark SQL）。

用法（VM）:
  spark-submit --master 'local[2]' \
    --conf spark.sql.session.timeZone=UTC --conf spark.sql.shuffle.partitions=4 \
    analytics/jobs/build_warehouse.py \
    --dwd <DWD 目录> --output <dws+ads 父目录> \
    --manifest <batches/<batch>/manifest.json> \
    --clean-report <clean_report.json> --batch-id <batch>

产出:
  <output>/dws/dws_station_day, dws_station_hour
  <output>/ads/ads_overview, ads_revenue_trend, ads_load_hour,
             ads_station_rank, ads_quality_summary, ads_pile_snapshot

口径（docs/architecture/analytics.md §4）:
- 时间全链路 UTC; 日/小时网格全量展开（缺测时段除外，manifest.collection_gaps）。
- 利用率 = Σ charge_seconds / Σ capacity_pile_seconds（固定维度假设：站桩数×时长，只计一次）。
- 小时分摊: 订单能量按 [started, ended] ∩ 小时桶 比例分摊; 每单 Σ分配 = 源电量。
- 结算跨窗口边界（settled_at ≥ 窗口末端）的行并入网格，不静默丢收入。
"""
from __future__ import annotations

import argparse
import json
import sys

from pyspark.sql import SparkSession
from pyspark.sql import functions as F


def load_manifest(path):
    with open(path, encoding="utf-8") as fh:
        m = json.load(fh)
    return {
        "batch_id": m["batch_id"],
        "source_type": m["source_type"],
        "data_start": m["data_start"].replace("T", " ").replace("Z", ""),
        "data_end": m["data_end_exclusive"].replace("T", " ").replace("Z", ""),
        "data_end_iso": m["data_end_exclusive"],
        "gaps": m.get("collection_gaps", []),
    }


def build_gap_filter(manifest, unit):
    conds = []
    for gap in manifest.get("gaps", []):
        sid = gap["station_id"]
        g0 = gap["gap_start"].replace("T", " ").replace("Z", "")
        g1 = gap["gap_end_exclusive"].replace("T", " ").replace("Z", "")
        if unit == "day":
            conds.append(f"NOT (g.station_id = {sid} AND g.stat_date >= DATE '{g0[:10]}' "
                         f"AND g.stat_date < DATE '{g1[:10]}')")
        else:
            conds.append(f"NOT (g.station_id = {sid} AND g.hour_start >= TIMESTAMP '{g0}' "
                         f"AND g.hour_start < TIMESTAMP '{g1}')")
    return ("WHERE " + " AND ".join(conds)) if conds else ""


def run(spark, dwd, output, batch_id, manifest):
    t0, t1 = manifest["data_start"], manifest["data_end"]
    t0d, t1d = t0[:10], t1[:10]

    order = spark.read.parquet(f"{dwd}/fact_order")
    pile = spark.read.parquet(f"{dwd}/dim_pile")
    station = spark.read.parquet(f"{dwd}/dim_station")
    order.createOrReplaceTempView("dwd_order")
    pile.createOrReplaceTempView("dwd_pile")
    station.createOrReplaceTempView("dwd_station")

    # ---------- dws_station_day ----------
    spark.sql("""
        CREATE OR REPLACE TEMP VIEW created_day AS
        SELECT p.station_id, TO_DATE(o.created_at) AS stat_date, COUNT(*) AS created_order_count
        FROM dwd_order o JOIN dwd_pile p ON o.pile_id = p.id
        GROUP BY p.station_id, TO_DATE(o.created_at)
    """)
    spark.sql("""
        CREATE OR REPLACE TEMP VIEW settled_day AS
        SELECT p.station_id, TO_DATE(o.settled_at) AS stat_date,
               COUNT(*) AS settled_order_count, SUM(o.total_amount_cents) AS revenue_cents,
               SUM(o.energy_wh) AS settled_energy_wh,
               SUM(unix_timestamp(o.ended_at) - unix_timestamp(o.started_at)) AS settled_charge_seconds
        FROM dwd_order o JOIN dwd_pile p ON o.pile_id = p.id
        WHERE o.status = 'completed' AND o.settled_at IS NOT NULL
          AND o.started_at IS NOT NULL AND o.ended_at IS NOT NULL
        GROUP BY p.station_id, TO_DATE(o.settled_at)
    """)
    spark.sql(f"""
        CREATE OR REPLACE TEMP VIEW grid_day AS
        SELECT s.id AS station_id, d AS stat_date
        FROM dwd_station s
        CROSS JOIN (SELECT explode(sequence(DATE '{t0d}',
                        DATE_ADD(DATE '{t1d}', -1), INTERVAL 1 DAY)) AS d)
        UNION
        SELECT DISTINCT station_id, stat_date FROM settled_day WHERE stat_date >= DATE '{t1d}'
    """)
    gap_day = build_gap_filter(manifest, unit="day")
    dws_day = spark.sql(f"""
        SELECT g.station_id, g.stat_date,
               COALESCE(c.created_order_count, 0) AS created_order_count,
               COALESCE(s.settled_order_count, 0) AS settled_order_count,
               COALESCE(s.revenue_cents, 0) AS revenue_cents,
               COALESCE(s.settled_energy_wh, 0) AS settled_energy_wh,
               COALESCE(s.settled_charge_seconds, 0) AS settled_charge_seconds,
               '{batch_id}' AS batch_id
        FROM grid_day g
        LEFT JOIN created_day c ON c.station_id = g.station_id AND c.stat_date = g.stat_date
        LEFT JOIN settled_day s ON s.station_id = g.station_id AND s.stat_date = g.stat_date
        {gap_day}
    """)

    # ---------- dws_station_hour ----------
    spark.sql(f"""
        CREATE OR REPLACE TEMP VIEW alloc_hour AS
        WITH base AS (
          SELECT p.station_id, o.energy_wh, o.started_at, o.ended_at,
                 unix_timestamp(o.ended_at) - unix_timestamp(o.started_at) AS dur_s
          FROM dwd_order o JOIN dwd_pile p ON o.pile_id = p.id
          WHERE o.status = 'completed' AND o.started_at IS NOT NULL AND o.ended_at IS NOT NULL
            AND o.ended_at > o.started_at
            AND o.started_at >= TIMESTAMP '{t0}' AND o.started_at < TIMESTAMP '{t1}'
        ),
        bh AS (
          SELECT station_id, energy_wh, dur_s, started_at, ended_at,
                 explode(sequence(date_trunc('HOUR', started_at),
                                  date_trunc('HOUR', ended_at - INTERVAL 1 SECOND),
                                  INTERVAL 1 HOUR)) AS h
          FROM base
        )
        SELECT station_id, h AS hour_start,
               SUM(GREATEST(0, unix_timestamp(LEAST(ended_at, h + INTERVAL 1 HOUR))
                              - unix_timestamp(GREATEST(started_at, h)))) AS charge_seconds,
               SUM(energy_wh * GREATEST(0, unix_timestamp(LEAST(ended_at, h + INTERVAL 1 HOUR))
                              - unix_timestamp(GREATEST(started_at, h))) / dur_s) AS allocated_wh
        FROM bh
        GROUP BY station_id, h
    """)
    spark.sql(f"""
        CREATE OR REPLACE TEMP VIEW grid_hour AS
        SELECT s.id AS station_id, h AS hour_start
        FROM dwd_station s
        CROSS JOIN (SELECT explode(sequence(TIMESTAMP '{t0}',
                        TIMESTAMP '{t1}' - INTERVAL 1 SECOND, INTERVAL 1 HOUR)) AS h)
        UNION
        SELECT DISTINCT station_id, hour_start FROM alloc_hour WHERE hour_start >= TIMESTAMP '{t1}'
    """)
    spark.sql("""
        CREATE OR REPLACE TEMP VIEW pile_count AS
        SELECT station_id, COUNT(*) AS pile_count FROM dwd_pile GROUP BY station_id
    """)
    gap_hour = build_gap_filter(manifest, unit="hour")
    dws_hour = spark.sql(f"""
        SELECT g.station_id, g.hour_start,
               CAST(COALESCE(a.allocated_wh, 0.0) AS DOUBLE) AS allocated_wh,
               CAST(COALESCE(a.charge_seconds, 0) AS BIGINT) AS charge_seconds,
               CAST(COALESCE(pc.pile_count, 0) * 3600 AS BIGINT) AS capacity_pile_seconds,
               CAST(COALESCE(a.allocated_wh, 0.0) / 1000.0 AS DOUBLE) AS avg_load_kw,
               '{batch_id}' AS batch_id
        FROM grid_hour g
        LEFT JOIN alloc_hour a ON a.station_id = g.station_id AND a.hour_start = g.hour_start
        LEFT JOIN pile_count pc ON pc.station_id = g.station_id
        {gap_hour}
    """)

    dws_day.write.mode("error").parquet(f"{output}/dws/dws_station_day")
    dws_hour.write.mode("error").parquet(f"{output}/dws/dws_station_hour")
    dws_day.createOrReplaceTempView("dws_day")
    dws_hour.createOrReplaceTempView("dws_hour")

    # ---------- ADS ----------
    ads_revenue_trend = spark.sql(f"""
        SELECT '{batch_id}' AS batch_id, stat_date, station_id,
               revenue_cents, settled_order_count AS completed_orders,
               settled_energy_wh AS energy_wh, created_order_count
        FROM dws_day
    """)

    ads_load_hour = spark.sql(f"""
        SELECT '{batch_id}' AS batch_id, hour_start, station_id,
               allocated_wh, charge_seconds, capacity_pile_seconds, avg_load_kw AS load_kw
        FROM dws_hour
    """)

    spark.sql(f"""
        CREATE OR REPLACE TEMP VIEW rev_station AS
        SELECT station_id, SUM(revenue_cents) AS revenue_cents,
               SUM(settled_order_count) AS completed_orders,
               SUM(settled_energy_wh) AS energy_wh
        FROM dws_day GROUP BY station_id
    """)
    spark.sql("""
        CREATE OR REPLACE TEMP VIEW util_station AS
        SELECT station_id,
               CASE WHEN SUM(capacity_pile_seconds) > 0
                    THEN SUM(charge_seconds) / SUM(capacity_pile_seconds)
                    ELSE 0.0 END AS utilization
        FROM dws_hour GROUP BY station_id
    """)
    ads_station_rank = spark.sql(f"""
        SELECT '{batch_id}' AS batch_id, r.station_id, r.revenue_cents, r.completed_orders,
               r.energy_wh, COALESCE(u.utilization, 0.0) AS utilization,
               ROW_NUMBER() OVER (ORDER BY r.revenue_cents DESC, r.station_id ASC) AS rank_no
        FROM rev_station r
        LEFT JOIN util_station u ON u.station_id = r.station_id
    """)
    ads_station_rank.createOrReplaceTempView("ads_station_rank")

    dim_pile = spark.read.parquet(f"{dwd}/dim_pile")
    dim_station = spark.read.parquet(f"{dwd}/dim_station")
    # 桩快照附带站点名/坐标：大屏站点节点（地图/列表）需要 name+经纬度，
    # 而 ADS 层是发布的唯一来源（export 不回读 DWD）——缺坐标曾致前端 assertFinite 崩（实测）。
    ads_pile_snapshot = (
        dim_pile.join(dim_station.select(
            F.col("id").alias("_st_id"), "name", "latitude", "longitude"),
            dim_pile.station_id == F.col("_st_id"), "left")
        .select(
            F.lit(batch_id).alias("batch_id"),
            F.lit(manifest["data_end_iso"]).alias("snapshot_at"),
            "id", "station_id", "pile_code", "pile_type", "power_kw",
            "unit_price_cents_per_kwh", "status", "total_charge_count", "total_charge_seconds",
            F.col("name").alias("station_name"), "latitude", "longitude"))

    qrows = []
    if manifest.get("clean_report"):
        with open(manifest["clean_report"], encoding="utf-8") as fh:
            cr = json.load(fh)
        for tname, rep in sorted(cr.get("tables", {}).items()):
            qrows.append((batch_id, tname, rep["input"], rep["kept"], rep["duplicate"],
                          rep["quarantine"], rep["repaired"]))
    qschema = "batch_id string, table_name string, input_rows long, kept_rows long, " \
              "duplicate_rows long, quarantine_rows long, repaired_rows long"
    ads_quality = spark.createDataFrame(qrows, schema=qschema)

    totals = spark.sql(f"""
        SELECT
          (SELECT COALESCE(SUM(revenue_cents),0) FROM dws_day) AS revenue_cents,
          (SELECT COALESCE(SUM(settled_order_count),0) FROM dws_day) AS completed_orders,
          (SELECT COALESCE(SUM(settled_energy_wh),0) FROM dws_day) AS energy_wh,
          (SELECT COUNT(*) FROM dwd_order) AS total_order_count,
          (SELECT COUNT(*) FROM dwd_order WHERE status='cancelled') AS cancelled_orders,
          (SELECT COUNT(*) FROM dwd_order WHERE status='exception') AS exception_orders,
          (SELECT COUNT(*) FROM dwd_station) AS station_count,
          (SELECT COUNT(*) FROM dwd_pile) AS pile_count,
          (SELECT COALESCE(AVG(utilization),0) FROM ads_station_rank) AS avg_station_utilization
    """).collect()[0]
    ads_overview = spark.createDataFrame([(
        batch_id, manifest["source_type"], t0.replace(" ", "T") + "Z", manifest["data_end_iso"],
        int(totals["revenue_cents"]), int(totals["completed_orders"]), int(totals["energy_wh"]),
        int(totals["total_order_count"]), int(totals["cancelled_orders"]),
        int(totals["exception_orders"]), int(totals["station_count"]), int(totals["pile_count"]),
        float(totals["avg_station_utilization"]),
    )], schema="batch_id string, source_type string, window_start string, window_end_exclusive string, "
              "revenue_cents long, completed_orders long, energy_wh long, total_order_count long, "
              "cancelled_orders long, exception_orders long, station_count long, pile_count long, "
              "avg_station_utilization double")

    # 工作台只发布去标识的分析必要字段，不回读污染 ODS，也不导出姓名/手机号。
    # 按站点/日期保留粒度，API 可以在任意合法筛选窗口重新计算复购和 RFM。
    ads_user_activity = spark.sql(f"""
        SELECT '{batch_id}' AS batch_id, o.user_id, p.station_id,
               TO_DATE(o.settled_at) AS stat_date, COUNT(*) AS frequency,
               SUM(o.total_amount_cents) AS monetary,
               MAX(o.settled_at) AS last_settled_at
        FROM dwd_order o JOIN dwd_pile p ON o.pile_id = p.id
        WHERE o.status = 'completed' AND o.settled_at IS NOT NULL
        GROUP BY o.user_id, p.station_id, TO_DATE(o.settled_at)
    """)
    ads_user_summary = spark.read.parquet(f"{dwd}/dim_user").agg(
        F.count("id").alias("total_users")).withColumn("batch_id", F.lit(batch_id))
    ads_order_activity = spark.sql(f"""
        SELECT '{batch_id}' AS batch_id, p.station_id,
               TO_DATE(o.created_at) AS stat_date, o.status,
               HOUR(o.started_at) AS start_hour, COUNT(*) AS order_count,
               SUM(CASE WHEN o.status = 'completed'
                   THEN unix_timestamp(o.ended_at) - unix_timestamp(o.started_at) ELSE 0 END) AS duration_seconds
        FROM dwd_order o JOIN dwd_pile p ON o.pile_id = p.id
        GROUP BY p.station_id, TO_DATE(o.created_at), o.status, HOUR(o.started_at)
    """)

    for name, df in (("ads_overview", ads_overview),
                     ("ads_revenue_trend", ads_revenue_trend),
                     ("ads_load_hour", ads_load_hour),
                     ("ads_station_rank", ads_station_rank),
                     ("ads_quality_summary", ads_quality),
                     ("ads_pile_snapshot", ads_pile_snapshot),
                     ("ads_user_activity", ads_user_activity),
                     ("ads_user_summary", ads_user_summary),
                     ("ads_order_activity", ads_order_activity)):
        df.write.mode("error").parquet(f"{output}/ads/{name}")
        print(f"ADS {name}: rows={df.count()}", flush=True)

    return ads_overview


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dwd", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--clean-report", default=None)
    ap.add_argument("--batch-id", required=True)
    args = ap.parse_args(argv)

    manifest = load_manifest(args.manifest)
    manifest["clean_report"] = args.clean_report
    spark = (SparkSession.builder.appName(f"stage2-warehouse-{args.batch_id}")
             .config("spark.sql.session.timeZone", "UTC")
             .config("spark.sql.shuffle.partitions", "4")
             # 本地模式延迟调度防线（同 clean.py 注释；SPARK-42923 征兆）
             .config("spark.locality.wait", "0")
             .config("spark.locality.wait.node", "0")
             .config("spark.locality.wait.rack", "0")
             .config("spark.locality.wait.process", "0")
             .getOrCreate())
    spark.sparkContext.setLogLevel("WARN")
    run(spark, args.dwd, args.output, args.batch_id, manifest)
    spark.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
