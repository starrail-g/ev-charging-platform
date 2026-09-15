#!/usr/bin/env python3
"""Build DWD, DWS and ADS datasets from the Schema v0.4 ODS export."""

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


def build_pipeline(ods_dir: Path, output_dir: Path, master: str) -> dict[str, object]:
    spark = (
        SparkSession.builder.master(master).appName("ev-charging-stage2-pipeline")
        .config("spark.sql.session.timeZone", "UTC")
        .config("spark.ui.enabled", "false")
        .getOrCreate()
    )
    spark.sparkContext.setLogLevel("WARN")
    try:
        dwd = order_dwd(spark, ods_dir).cache()
        dwd_path = output_dir / "dwd" / "charging_orders"
        dws_path = output_dir / "dws" / "station_hourly"
        ads_path = output_dir / "ads" / "revenue_daily"
        feature_path = output_dir / "ads" / "load_features"
        for path in [dwd_path, dws_path, ads_path, feature_path]:
            if path.exists():
                shutil.rmtree(path)
        write_dataset(dwd, dwd_path)

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
        pile_counts = piles.groupBy("station_id").agg(
            F.count("id").alias("device_count"),
            F.sum(F.when(F.col("status") == "idle", 1).otherwise(0)).alias("idle_pile_count"),
        )
        hourly = (
            dwd.filter(F.col("quality_valid"))
            .groupBy("event_date", "start_hour", "station_id")
            .agg(
                F.count("order_id").alias("session_count"),
                F.sum("energy_kwh").alias("load_kwh"),
                F.sum("total_amount_cents").alias("revenue_cents"),
                F.avg("duration_minutes").alias("avg_duration_minutes"),
                F.countDistinct("user_id").alias("user_count"),
            )
            .join(pile_counts, "station_id", "left")
            .withColumn("occupancy_minutes", F.col("avg_duration_minutes") * F.col("session_count"))
            .withColumn("utilization", F.least(F.lit(1.0), F.col("occupancy_minutes") / (F.col("device_count") * 60.0)))
            .withColumn("idle_pile_estimate", F.greatest(F.lit(0), F.col("device_count") - F.col("session_count")))
        )
        write_dataset(hourly, dws_path)

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
            hourly.select("event_date", "start_hour", "station_id", "load_kwh", "session_count", "user_count", "utilization", "idle_pile_estimate")
            .withColumnRenamed("event_date", "date")
            .withColumn("feature_source", F.lit("schema_v0.4_generated"))
        )
        write_dataset(features, feature_path)

        quality_rows = dwd.groupBy("quality_valid", "quality_code").count().orderBy("quality_valid", "quality_code").collect()
        report = {
            "generated_at": datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "source": str(ods_dir),
            "dwd_rows": dwd.count(),
            "dwd_valid_rows": dwd.filter(F.col("quality_valid")).count(),
            "dwd_invalid_rows": dwd.filter(~F.col("quality_valid")).count(),
            "quality_counts": [row.asDict(recursive=True) for row in quality_rows],
            "dws_rows": hourly.count(),
            "ads_revenue_daily_rows": ads.count(),
            "feature_rows": features.count(),
            "timezone": "UTC",
            "source_contract": "SQLite Schema v0.4 generated analysis database",
        }
        (output_dir / "reports").mkdir(parents=True, exist_ok=True)
        (output_dir / "reports" / "quality_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        return report
    finally:
        spark.stop()


def main() -> int:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = build_pipeline(args.ods_dir, args.output, args.master)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
