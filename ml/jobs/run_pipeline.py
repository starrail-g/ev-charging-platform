#!/usr/bin/env python3
"""Build DWD, DWS and ADS datasets from the Schema v0.4 ODS export.

产出：<output>/dwd/charging_orders、<output>/dwd/quarantine_charging_orders（无效行独立落盘）、
<output>/dws/station_hourly、<output>/dws/station_day、<output>/ads/revenue_daily、
<output>/ads/load_features、<output>/reports/quality_report.json。

利用率口径：Σ 区间∩桶分摊充电秒数 /（桩数 × 3600），小时/日网格全量展开——分母含零订单小时；
秒数、电量、金额按重叠占比分别分摊（表达式与 analytics/jobs/build_warehouse.py 同源）。
"""

from __future__ import annotations

import argparse
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

from pyspark.sql import DataFrame, SparkSession, Window, functions as F, types as T


UTC = timezone.utc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ods-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--master", default="local[*]")
    return parser.parse_args()


def read_ods(spark: SparkSession, path: Path, schema: T.StructType) -> DataFrame:
    return (
        spark.read.option("header", True)
        .option("mode", "PERMISSIVE")
        .schema(T.StructType([
            T.StructField("source_table", T.StringType(), False),
            T.StructField("source_database_sha256", T.StringType(), False),
            T.StructField("generation_seed", T.IntegerType(), False),
            T.StructField("source_row_id", T.StringType(), False),
            *schema.fields,
        ]))
        .csv(str(path))
    )


def order_dwd(spark: SparkSession, ods: Path) -> DataFrame:
    order_schema = T.StructType([
        T.StructField("id", T.LongType()), T.StructField("order_no", T.StringType()),
        T.StructField("user_id", T.LongType()), T.StructField("pile_id", T.LongType()),
        T.StructField("status", T.StringType()), T.StructField("reserved_at", T.StringType()),
        T.StructField("started_at", T.StringType()), T.StructField("ended_at", T.StringType()),
        T.StructField("energy_wh", T.LongType()), T.StructField("unit_price_cents_per_kwh", T.LongType()),
        T.StructField("service_fee_cents", T.LongType()), T.StructField("total_amount_cents", T.LongType()),
        T.StructField("settled_at", T.StringType()), T.StructField("created_at", T.StringType()),
        T.StructField("updated_at", T.StringType()),
    ])
    station_schema = T.StructType([
        T.StructField("id", T.LongType()), T.StructField("name", T.StringType()),
        T.StructField("address", T.StringType()), T.StructField("latitude", T.DoubleType()),
        T.StructField("longitude", T.DoubleType()), T.StructField("status", T.StringType()),
        T.StructField("created_at", T.StringType()), T.StructField("updated_at", T.StringType()),
        T.StructField("provider", T.StringType()),
    ])
    pile_schema = T.StructType([
        T.StructField("id", T.LongType()), T.StructField("station_id", T.LongType()),
        T.StructField("pile_code", T.StringType()), T.StructField("pile_type", T.StringType()),
        T.StructField("power_kw", T.DoubleType()), T.StructField("unit_price_cents_per_kwh", T.LongType()),
        T.StructField("status", T.StringType()), T.StructField("total_charge_count", T.LongType()),
        T.StructField("total_charge_seconds", T.LongType()), T.StructField("created_at", T.StringType()),
        T.StructField("updated_at", T.StringType()), T.StructField("simulated", T.IntegerType()),
        T.StructField("status_source", T.StringType()), T.StructField("status_updated_at", T.StringType()),
    ])
    user_schema = T.StructType([
        T.StructField("id", T.LongType()), T.StructField("phone", T.StringType()),
        T.StructField("nickname", T.StringType()), T.StructField("balance_cents", T.LongType()),
        T.StructField("status", T.StringType()), T.StructField("created_at", T.StringType()),
        T.StructField("updated_at", T.StringType()),
    ])

    orders_path = ods / "charging_orders_raw.csv"
    if not orders_path.exists():
        orders_path = ods / "charging_orders.csv"
    orders = read_ods(spark, orders_path, order_schema).alias("o")
    stations = read_ods(spark, ods / "stations.csv", station_schema).select(
        F.col("id").alias("station_id_lookup"), F.col("name").alias("station_name_lookup"),
        F.col("status").alias("station_status_lookup"),
    )
    piles = read_ods(spark, ods / "charging_piles.csv", pile_schema).select(
        F.col("id").alias("pile_id_lookup"), F.col("station_id").alias("pile_station_id"),
        F.col("power_kw").alias("pile_power_kw"),
    )
    users = read_ods(spark, ods / "users.csv", user_schema).select(
        F.col("id").alias("user_id_lookup"), F.col("status").alias("user_status_lookup"),
    )

    typed = (
        orders.withColumn("order_id", F.col("o.id").cast("long"))
        .withColumn("user_id_typed", F.col("o.user_id").cast("long"))
        .withColumn("pile_id_typed", F.col("o.pile_id").cast("long"))
        .withColumn("created_ts", F.to_timestamp("o.created_at"))
        .withColumn("started_ts", F.to_timestamp("o.started_at"))
        .withColumn("ended_ts", F.to_timestamp("o.ended_at"))
        .withColumn("settled_ts", F.to_timestamp("o.settled_at"))
        .withColumn("duration_minutes", (F.col("ended_ts").cast("long") - F.col("started_ts").cast("long")) / F.lit(60.0))
    )
    joined = (
        typed.join(piles, F.col("pile_id_typed") == F.col("pile_id_lookup"), "left")
        .join(stations, F.col("pile_station_id") == F.col("station_id_lookup"), "left")
        .join(users, F.col("user_id_typed") == F.col("user_id_lookup"), "left")
    )
    result = (
        joined
        .withColumn("order_id_duplicate_count", F.count("order_id").over(Window.partitionBy("order_id")))
        .withColumn("station_id", F.col("pile_station_id"))
        .withColumn("station_name", F.col("station_name_lookup"))
        .withColumn("station_status", F.col("station_status_lookup"))
        .withColumn("user_status", F.col("user_status_lookup"))
        .withColumn("event_date", F.to_date("settled_ts"))
        .withColumn("start_hour", F.hour("started_ts"))
        .withColumn("energy_kwh", F.col("o.energy_wh") / F.lit(1000.0))
        .withColumn("quality_valid", (
            (F.col("o.status") == F.lit("completed")) & F.col("order_id").isNotNull()
            & F.col("o.order_no").isNotNull() & F.col("user_id_typed").isNotNull()
            & F.col("pile_id_typed").isNotNull() & F.col("pile_station_id").isNotNull()
            & F.col("station_id_lookup").isNotNull() & F.col("user_id_lookup").isNotNull()
            & F.col("created_ts").isNotNull() & F.col("started_ts").isNotNull()
            & F.col("ended_ts").isNotNull() & F.col("settled_ts").isNotNull()
            & (F.col("o.energy_wh") >= 0) & (F.col("o.total_amount_cents") > 0)
            & (F.col("duration_minutes") >= 0) & (F.col("duration_minutes") <= 24 * 60)
            & (F.col("order_id_duplicate_count") == 1)
        ))
        .withColumn("quality_code", F.when(F.col("order_id_duplicate_count") > 1, "DUPLICATE_ORDER")
                    .when(F.col("order_id").isNull(), "NULL_ORDER_ID")
                    .when(F.col("pile_station_id").isNull(), "ORPHAN_PILE")
                    .when(F.col("station_id_lookup").isNull(), "ORPHAN_STATION")
                    .when(F.col("user_id_lookup").isNull(), "ORPHAN_USER")
                    .when(F.col("created_ts").isNull() | F.col("started_ts").isNull()
                          | F.col("ended_ts").isNull() | F.col("settled_ts").isNull(), "BAD_TIMESTAMP")
                    .when(F.col("duration_minutes") < 0, "END_BEFORE_START")
                    .when(F.col("duration_minutes") > 24 * 60, "LONG_SESSION")
                    .when(F.col("o.energy_wh") < 0, "NEGATIVE_ENERGY")
                    .when(F.col("o.total_amount_cents") <= 0, "INVALID_TOTAL")
                    .when(F.col("o.status") != "completed", "NON_COMPLETED_ORDER")
                    .otherwise(F.lit(None).cast("string")))
        .select(
            "source_table", "source_database_sha256", "generation_seed", "source_row_id",
            F.col("order_id").alias("order_id"), F.col("o.order_no").alias("order_no"),
            F.col("user_id_typed").alias("user_id"), F.col("pile_id_typed").alias("pile_id"),
            "station_id", F.col("o.status").alias("order_status"), "created_ts", "started_ts", "ended_ts", "settled_ts",
            "event_date", "start_hour", "energy_kwh", F.col("o.energy_wh").alias("energy_wh"),
            F.col("o.unit_price_cents_per_kwh").alias("unit_price_cents_per_kwh"),
            F.col("o.service_fee_cents").alias("service_fee_cents"), F.col("o.total_amount_cents").alias("total_amount_cents"),
            "duration_minutes", "pile_power_kw", "station_name", "station_status", "quality_valid", "quality_code",
        )
    )
    return result


def write_dataset(frame: DataFrame, path: Path, mode: str = "overwrite") -> None:
    frame.write.mode(mode).parquet(str(path))


def hour_window(dwd: DataFrame) -> tuple[str, str]:
    """有效订单的小时窗口 [floor(最早开始, 小时), floor(最晚结束-1s, 小时)]（UTC 字符串）。"""
    bounds = (
        dwd.filter(
            F.col("quality_valid") & F.col("started_ts").isNotNull() & F.col("ended_ts").isNotNull()
            & (F.col("ended_ts") > F.col("started_ts"))
        ).agg(
            F.date_format(F.date_trunc("hour", F.min("started_ts")), "yyyy-MM-dd HH:mm:ss").alias("lo"),
            F.date_format(F.date_trunc("hour", F.max("ended_ts") - F.expr("INTERVAL 1 SECOND")),
                          "yyyy-MM-dd HH:mm:ss").alias("hi"),
        ).collect()[0]
    )
    if bounds["lo"] is None or bounds["hi"] is None:
        raise ValueError("no valid completed orders available for DWS")
    return bounds["lo"], bounds["hi"]


def allocation_facts(dwd: DataFrame) -> tuple[DataFrame, DataFrame]:
    """有效完成订单按 [started, ended) ∩ 桶 分摊（小时级 / 日级两级）。

    与 analytics/jobs/build_warehouse.py 同源：重叠秒数、电量、金额分别按 overlap/duration
    占比分配——每单 Σ分摊电量 == 源电量（守恒），不再按起始小时整桶计入。
    """
    # 用纪元秒整数运算表达「[started, ended) ∩ 小时桶」：sequence 走 long 类型，
    # 避免依赖不同 Spark 版本对 timestamp 序列的支持差异（pyspark 3.3.4 为 ml 链验证环境）。
    base = (
        dwd.filter(
            F.col("quality_valid") & F.col("started_ts").isNotNull() & F.col("ended_ts").isNotNull()
            & (F.col("ended_ts") > F.col("started_ts"))
        )
        .withColumn("start_epoch", F.unix_timestamp("started_ts"))
        .withColumn("end_epoch", F.unix_timestamp("ended_ts"))
        .withColumn("dur_s", F.col("end_epoch") - F.col("start_epoch"))
    )
    bucket_lo = F.floor(F.col("start_epoch") / F.lit(3600)).cast("long") * F.lit(3600)
    bucket_hi = F.floor((F.col("end_epoch") - F.lit(1)) / F.lit(3600)).cast("long") * F.lit(3600)
    buckets = base.withColumn(
        "bucket_epoch",
        F.explode(F.sequence(bucket_lo, bucket_hi, F.lit(3600))),
    ).withColumn(
        "overlap_s",
        F.least(F.col("end_epoch"), F.col("bucket_epoch") + F.lit(3600))
        - F.greatest(F.col("start_epoch"), F.col("bucket_epoch")),
    )
    share = F.col("overlap_s") / F.col("dur_s")

    def aggregate(keys: list) -> DataFrame:
        return buckets.groupBy(*keys).agg(
            F.sum("overlap_s").alias("charge_seconds"),
            F.sum(F.col("energy_wh") * share).alias("allocated_wh"),
            F.sum(F.col("total_amount_cents") * share).alias("allocated_cents"),
            F.countDistinct("order_id").alias("session_count"),
            F.countDistinct("user_id").alias("user_count"),
        )

    hourly = aggregate(["station_id", F.col("bucket_epoch").cast("timestamp").alias("hour_start")])
    daily = aggregate(["station_id", F.to_date(F.col("bucket_epoch").cast("timestamp")).alias("stat_date")])
    return hourly, daily


def pile_counts(piles: DataFrame) -> DataFrame:
    return piles.groupBy("station_id").agg(F.count("id").alias("device_count"))


def hour_sequence(spark: SparkSession, lo: str, hi: str) -> DataFrame:
    """全窗口小时序列（UTC）——小时网格、日网格与各级容量共用的唯一窗口基准。"""
    lo_epoch = int(datetime.strptime(lo, "%Y-%m-%d %H:%M:%S").replace(tzinfo=UTC).timestamp())
    hi_epoch = int(datetime.strptime(hi, "%Y-%m-%d %H:%M:%S").replace(tzinfo=UTC).timestamp())
    return (spark.range(lo_epoch, hi_epoch + 3600, 3600)
            .select(F.col("id").cast("timestamp").alias("hour_start")))


def grid_hourly(allocated: DataFrame, piles: DataFrame, hours: DataFrame) -> DataFrame:
    """站点 × 全窗口小时网格（零订单小时保留行），利用率分母 = 桩数 × 3600。"""
    grid = piles.select("station_id").distinct().crossJoin(hours)
    return (
        grid.join(allocated, ["station_id", "hour_start"], "left")
        .join(pile_counts(piles), "station_id", "left")
        .fillna({"charge_seconds": 0, "allocated_wh": 0.0, "allocated_cents": 0.0,
                 "session_count": 0, "user_count": 0})
        .withColumn("capacity_pile_seconds", F.col("device_count") * F.lit(3600))
        .withColumn("utilization", F.least(F.lit(1.0), F.col("charge_seconds") / F.col("capacity_pile_seconds")))
        .withColumn("load_kwh", F.col("allocated_wh") / F.lit(1000.0))
        .withColumn("idle_pile_estimate", F.greatest(F.lit(0), F.col("device_count") - F.col("session_count")))
        .select(
            F.to_date("hour_start").alias("event_date"),
            F.hour("hour_start").alias("start_hour"),
            "hour_start", "station_id",
            F.col("charge_seconds").cast("long").alias("charge_seconds"),
            F.col("allocated_wh").cast("double").alias("allocated_wh"),
            F.col("allocated_cents").cast("double").alias("revenue_cents"),
            "session_count", "user_count", "device_count", "capacity_pile_seconds",
            F.col("utilization").cast("double").alias("utilization"),
            F.col("load_kwh").cast("double").alias("load_kwh"),
            "idle_pile_estimate",
        )
    )


def grid_daily(allocated_day: DataFrame, piles: DataFrame, hours: DataFrame) -> DataFrame:
    """站点 × 全窗口日网格（零订单日保留行）。

    窗口与小时网格严格统一：每日容量 = 桩数 × 该日在小时网格中的桶数 × 3600——
    即"每日小时容量之和 = 日容量"，小时/日两级汇总利用率天然一致（分母含零订单小时）。
    """
    day_hours = (hours.groupBy(F.to_date("hour_start").alias("stat_date"))
                 .agg(F.count("*").cast("long").alias("hours_in_window")))
    grid = piles.select("station_id").distinct().crossJoin(day_hours.select("stat_date"))
    return (
        grid.join(day_hours, "stat_date", "left")
        .join(allocated_day, ["station_id", "stat_date"], "left")
        .join(pile_counts(piles), "station_id", "left")
        .fillna({"charge_seconds": 0, "allocated_wh": 0.0, "allocated_cents": 0.0,
                 "session_count": 0, "user_count": 0})
        .withColumn("capacity_pile_seconds",
                    F.col("device_count") * F.col("hours_in_window") * F.lit(3600))
        .withColumn("utilization", F.least(F.lit(1.0), F.col("charge_seconds") / F.col("capacity_pile_seconds")))
        .withColumn("load_kwh", F.col("allocated_wh") / F.lit(1000.0))
        .select(
            "station_id", "stat_date", "hours_in_window",
            F.col("charge_seconds").cast("long").alias("charge_seconds"),
            F.col("allocated_wh").cast("double").alias("allocated_wh"),
            F.col("allocated_cents").cast("double").alias("revenue_cents"),
            "session_count", "user_count", "device_count", "capacity_pile_seconds",
            F.col("utilization").cast("double").alias("utilization"),
            F.col("load_kwh").cast("double").alias("load_kwh"),
        )
    )


def build_pipeline(ods_dir: Path, output_dir: Path, master: str) -> dict[str, object]:
    spark = (
        SparkSession.builder.master(master).appName("ev-charging-stage2-pipeline")
        .config("spark.sql.session.timeZone", "UTC")
        .config("spark.ui.enabled", "false")
        # ml 链为本地链：显式 file:// 文件系统，避免宿主 Hadoop 配置把 /tmp 等本地路径解析到 HDFS
        .config("spark.hadoop.fs.defaultFS", "file:///")
        # 本地模式延迟调度防线（SPARK-42923 征兆；与 clean.py / build_warehouse.py 一致）
        .config("spark.locality.wait", "0")
        .config("spark.locality.wait.node", "0")
        .config("spark.locality.wait.rack", "0")
        .config("spark.locality.wait.process", "0")
        .getOrCreate()
    )
    spark.sparkContext.setLogLevel("WARN")
    try:
        return run_build(spark, ods_dir, output_dir)
    finally:
        spark.stop()


def run_build(spark: SparkSession, ods_dir: Path, output_dir: Path) -> dict[str, object]:
    dwd = order_dwd(spark, ods_dir).cache()
    dwd_path = output_dir / "dwd" / "charging_orders"
    quarantine_path = output_dir / "dwd" / "quarantine_charging_orders"
    dws_path = output_dir / "dws" / "station_hourly"
    dws_day_path = output_dir / "dws" / "station_day"
    ads_path = output_dir / "ads" / "revenue_daily"
    feature_path = output_dir / "ads" / "load_features"
    for path in [dwd_path, quarantine_path, dws_path, dws_day_path, ads_path, feature_path]:
        if path.exists():
            shutil.rmtree(path)
    # 正式 DWD 只落有效行；无效行独立落盘 quarantine——两者互斥，输入 = DWD + quarantine。
    # 计数与互斥性在 report.conservation 中从实际落盘产物读回核验。
    write_dataset(dwd.filter(F.col("quality_valid")), dwd_path)
    write_dataset(dwd.filter(~F.col("quality_valid")), quarantine_path)

    piles = (
        read_ods(spark, ods_dir / "charging_piles.csv", T.StructType([
            T.StructField("id", T.LongType()), T.StructField("station_id", T.LongType()),
            T.StructField("pile_code", T.StringType()), T.StructField("pile_type", T.StringType()),
            T.StructField("power_kw", T.DoubleType()), T.StructField("unit_price_cents_per_kwh", T.LongType()),
            T.StructField("status", T.StringType()), T.StructField("total_charge_count", T.LongType()),
            T.StructField("total_charge_seconds", T.LongType()), T.StructField("created_at", T.StringType()),
            T.StructField("updated_at", T.StringType()), T.StructField("simulated", T.IntegerType()),
            T.StructField("status_source", T.StringType()), T.StructField("status_updated_at", T.StringType()),
        ])).select("id", "station_id", "power_kw", "status")
    )
    lo, hi = hour_window(dwd)
    hours = hour_sequence(spark, lo, hi)
    hourly_alloc, daily_alloc = allocation_facts(dwd)
    hourly = grid_hourly(hourly_alloc, piles, hours)
    write_dataset(hourly, dws_path)
    station_day = grid_daily(daily_alloc, piles, hours)
    write_dataset(station_day, dws_day_path)

    daily = dwd.filter(F.col("quality_valid")).groupBy("event_date").agg(
        F.sum("total_amount_cents").alias("revenue_cents"),
        F.count("order_id").alias("completed_order_count"),
        F.sum("energy_wh").alias("energy_wh"),
    )
    bounds = daily.agg(F.min("event_date").alias("min_date"), F.max("event_date").alias("max_date")).collect()[0]
    if bounds["min_date"] is None:
        raise ValueError("no valid completed orders available for ADS")
    calendar = spark.range(1).select(
        F.explode(F.sequence(F.lit(bounds["min_date"]), F.lit(bounds["max_date"]), F.expr("interval 1 day"))).alias("date")
    )
    ads = (
        calendar.join(daily.withColumnRenamed("event_date", "date"), "date", "left")
        .fillna({"revenue_cents": 0, "completed_order_count": 0, "energy_wh": 0})
        .withColumn("data_quality_summary", F.lit("schema_v0.4_generated"))
        .orderBy("date")
    )
    write_dataset(ads, ads_path)

    features = (
        hourly.select("event_date", "start_hour", "station_id", "load_kwh", "session_count",
                      "user_count", "utilization", "idle_pile_estimate")
        .withColumnRenamed("event_date", "date")
        .withColumn("feature_source", F.lit("schema_v0.4_generated"))
    )
    write_dataset(features, feature_path)

    input_rows = dwd.count()
    quality_rows = dwd.groupBy("quality_valid", "quality_code").count().orderBy("quality_valid", "quality_code").collect()
    # 计数与互斥性从两个实际落盘产物读回核验（不再只依赖内存分类计数）
    dwd_written = spark.read.parquet(str(dwd_path)).count()
    quarantine_written = spark.read.parquet(str(quarantine_path)).count()
    overlap = (spark.read.parquet(str(dwd_path)).select("source_row_id")
               .join(spark.read.parquet(str(quarantine_path)).select("source_row_id"), "source_row_id")
               .count())
    report = {
        "generated_at": datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "source": str(ods_dir),
        "input_rows": input_rows,
        "dwd_rows": dwd_written,
        "quarantine_rows": quarantine_written,
        "dwd_path": str(dwd_path),
        "quarantine_path": str(quarantine_path),
        "conservation": {
            "rule": "input_rows == dwd_rows + quarantine_rows",
            "input_rows": input_rows,
            "dwd_rows": dwd_written,
            "quarantine_rows": quarantine_written,
            "balanced": input_rows == dwd_written + quarantine_written,
            "exclusive": overlap == 0,
        },
        "quality_counts": [row.asDict(recursive=True) for row in quality_rows],
        "dws_rows": hourly.count(),
        "dws_station_day_rows": station_day.count(),
        "window_hours": hours.count(),
        "window_start": lo,
        "window_end_inclusive": hi,
        "ads_revenue_daily_rows": ads.count(),
        "feature_rows": features.count(),
        "timezone": "UTC",
        "source_contract": "SQLite Schema v0.4 generated analysis database",
    }
    (output_dir / "reports").mkdir(parents=True, exist_ok=True)
    (output_dir / "reports" / "quality_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = build_pipeline(args.ods_dir, args.output, args.master)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
