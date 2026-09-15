#!/usr/bin/env python3
"""六维质量统计（清洗前 ODS / 清洗后 DWD）。

用法（VM）:
  spark-submit analytics/jobs/quality_profile.py --stage before \
      --input <ODS 目录> --out <profile_before.json> --batch-id <batch>
  spark-submit analytics/jobs/quality_profile.py --stage after \
      --dwd <DWD 目录> --out <profile_after.json> --batch-id <batch>

（文件名为 quality_profile.py 而非 profile.py：spark-submit 将脚本目录置于 sys.path[0]，
名为 profile.py 会遮蔽 stdlib profile 模块，pyspark 内部 import cProfile→profile 时
反向导入本文件造成循环导入崩溃——实测踩坑，勿改回。）

六维: 完整性(缺失) / 唯一性(重复) / 类型格式(可解析性) / 值域(异常越界) /
      逻辑一致性(时间/计费) / 参照完整性(外键与流水关联)。
before 用于清洗前探查基线, after 用于洗后校验（应清零）。
"""
from __future__ import annotations

import argparse
import json
import sys

from pyspark.sql import SparkSession
from pyspark.sql import functions as F

from clean import (TABLE_FILES, TABLE_SPECS, build_canonical, content_key_expr)  # noqa: E402


def _blank(col):
    return F.trim(F.col(col).cast("string")).isNull() | (F.trim(F.col(col).cast("string")) == "")


def _ids(df, col):
    return [r[0] for r in df.filter(F.col(col).isNotNull()).select(col).distinct().collect()]


def profile_before(spark, table, path, refs):
    spec = TABLE_SPECS[table]()
    raw = spark.read.option("header", True).option("inferSchema", False).csv(path)
    df = build_canonical(raw, spec).cache()
    n = raw.count()
    key = spec["key"]

    by_column = {c: df.filter(_blank(c)).count() for c in spec["cols"]}
    reasons = {r["code"]: r["count"] for r in
               df.select(F.explode("parse_reasons").alias("code")).groupBy("code").count().collect()}

    df2 = df.filter(F.size("parse_reasons") == 0).withColumn("_ck", content_key_expr(spec))
    grp = (df2.groupBy(F.col(f"{key}_canon").alias("_k"))
           .agg(F.countDistinct("_ck").alias("nc"), F.count("*").alias("nr")))
    conflict_rows = grp.filter(F.col("nc") > 1).agg(F.sum("nr")).collect()[0][0] or 0
    conflict_keys = grp.filter(F.col("nc") > 1).count()
    dup_rows = grp.filter((F.col("nc") == 1) & (F.col("nr") > 1)) \
        .agg(F.sum(F.col("nr") - 1)).collect()[0][0] or 0

    referential = {}
    if table == "charging_piles" and refs.get("stations"):
        referential["station_id_missing"] = df.filter(
            F.col("station_id_ok") & ~F.col("station_id_canon").isin(refs["stations"])).count()
    if table == "charging_orders":
        if refs.get("piles"):
            referential["pile_id_missing"] = df.filter(
                F.col("pile_id_ok") & ~F.col("pile_id_canon").isin(refs["piles"])).count()
        if refs.get("users"):
            referential["user_id_missing"] = df.filter(
                F.col("user_id_ok") & ~F.col("user_id_canon").isin(refs["users"])).count()
    if table == "wallet_transactions" and refs.get("orders"):
        referential["order_id_missing"] = df.filter(
            F.col("order_id_ok") & F.col("order_id_canon").isNotNull()
            & ~F.col("order_id_canon").isin(refs["orders"])).count()

    consistency = {}
    if table == "charging_orders":
        consistency["time_order_violations"] = df.filter(
            (F.col("ended_at_canon").isNotNull() & F.col("started_at_canon").isNotNull()
             & (F.col("ended_at_canon") <= F.col("started_at_canon")))
            | (F.col("settled_at_canon").isNotNull() & F.col("ended_at_canon").isNotNull()
               & (F.col("settled_at_canon") < F.col("ended_at_canon")))).count()
        e = F.col("energy_wh_canon").cast("long")
        p = F.col("unit_price_cents_per_kwh_canon").cast("long")
        f_ = F.col("service_fee_cents_canon").cast("long")
        consistency["billing_mismatch"] = df.filter(
            (F.col("status_canon") == "completed") & F.col("energy_wh_ok")
            & F.col("total_amount_cents_ok")
            & (F.col("total_amount_cents_canon").cast("long")
               != F.floor((e * p + 999) / 1000) + f_)).count()

    new_refs = {}
    if key == "id":
        new_refs[table] = _ids(df2, f"{key}_canon")
    elif table == "charging_orders":
        new_refs["orders"] = _ids(df2, "id_canon")

    out = {
        "rows": n,
        "completeness": {"blank_cells": sum(by_column.values()), "by_column": by_column},
        "uniqueness": {"exact_duplicate_rows": int(dup_rows),
                       "conflict_rows": int(conflict_rows), "conflict_keys": int(conflict_keys)},
        "validity": {"parse_failures": reasons},
        "domain": {"out_of_range": reasons.get("DQ08_value_domain", 0)},
        "consistency": consistency,
        "referential": referential,
    }
    df.unpersist()
    return out, new_refs


def profile_after(spark, table, path, refs):
    spec = TABLE_SPECS[table]()
    df = spark.read.parquet(path)
    n = df.count()
    key = spec["key"]
    by_column = {c: df.filter(_blank(c)).count() for c in spec["cols"]}
    dup_keys = n - df.select(F.col(key)).distinct().count()

    referential = {}
    if table == "charging_piles" and refs.get("stations"):
        referential["station_id_missing"] = df.filter(~F.col("station_id").isin(refs["stations"])).count()
    if table == "charging_orders":
        if refs.get("piles"):
            referential["pile_id_missing"] = df.filter(~F.col("pile_id").isin(refs["piles"])).count()
        if refs.get("users"):
            referential["user_id_missing"] = df.filter(~F.col("user_id").isin(refs["users"])).count()
    if table == "wallet_transactions" and refs.get("orders_completed") is not None:
        completed = refs["orders_completed"]
        cdf = spark.createDataFrame(completed, "oid long, uid long, amt long, settled string")
        j = df.filter(F.col("transaction_type") == "charge").join(
            cdf, F.col("order_id") == F.col("oid"), "left")
        referential["charge_receipt_missing"] = j.filter(
            F.col("oid").isNull()
            | (F.col("user_id") != F.col("uid"))
            | (F.col("amount_cents") != F.col("amt") * -1)
            | (F.col("created_at") != F.to_timestamp(F.col("settled"),
                                                     "yyyy-MM-dd'T'HH:mm:ss'Z'"))).count()

    consistency = {}
    if table == "charging_orders":
        consistency["time_order_violations"] = df.filter(
            (F.col("ended_at").isNotNull() & F.col("started_at").isNotNull()
             & (F.col("ended_at") <= F.col("started_at")))
            | (F.col("settled_at").isNotNull() & F.col("ended_at").isNotNull()
               & (F.col("settled_at") < F.col("ended_at")))).count()
        e = F.col("energy_wh").cast("long")
        p = F.col("unit_price_cents_per_kwh").cast("long")
        f_ = F.col("service_fee_cents").cast("long")
        consistency["billing_mismatch"] = df.filter(
            (F.col("status") == "completed")
            & (F.col("total_amount_cents").cast("long")
               != F.floor((e * p + 999) / 1000) + f_)).count()

    new_refs = {}
    if key == "id":
        new_refs[table] = _ids(df, key)
    if table == "charging_orders":
        rows = df.filter(F.col("status") == "completed").select(
            F.col("id").alias("oid"), F.col("user_id").alias("uid"),
            F.col("total_amount_cents").alias("amt"),
            F.date_format("settled_at", "yyyy-MM-dd'T'HH:mm:ss'Z'").alias("settled")).collect()
        new_refs["orders_completed"] = [tuple(r) for r in rows]
        new_refs["orders"] = _ids(df, "id")

    return {
        "rows": n,
        "completeness": {"blank_cells": sum(by_column.values()), "by_column": by_column},
        "uniqueness": {"duplicate_keys": dup_keys},
        "validity": {},
        "domain": {},
        "consistency": consistency,
        "referential": referential,
    }, new_refs


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", choices=["before", "after"], required=True)
    ap.add_argument("--input", default=None, help="ODS 目录（before）")
    ap.add_argument("--dwd", default=None, help="DWD 目录（after）")
    ap.add_argument("--out", required=True)
    ap.add_argument("--batch-id", required=True)
    args = ap.parse_args(argv)

    spark = (SparkSession.builder.appName(f"stage2-profile-{args.stage}-{args.batch_id}")
             .config("spark.sql.session.timeZone", "UTC")
             .config("spark.sql.shuffle.partitions", "4")
             # 本地模式延迟调度防线（同 clean.py 注释；SPARK-42923 征兆）
             .config("spark.locality.wait", "0")
             .config("spark.locality.wait.node", "0")
             .config("spark.locality.wait.rack", "0")
             .config("spark.locality.wait.process", "0")
             .getOrCreate())
    spark.sparkContext.setLogLevel("WARN")

    order = ["users", "stations", "charging_piles", "charging_orders", "wallet_transactions"]
    dwd_names = {"users": "dim_user", "stations": "dim_station", "charging_piles": "dim_pile",
                 "charging_orders": "fact_order", "wallet_transactions": "fact_wallet"}
    refs = {}
    tables = {}
    for t in order:
        if args.stage == "before":
            res, new_refs = profile_before(spark, t, f"{args.input}/{TABLE_FILES[t]}", refs)
        else:
            res, new_refs = profile_after(spark, t, f"{args.dwd}/{dwd_names[t]}", refs)
        refs.update(new_refs)
        tables[t] = res
        print(f"[profile-{args.stage}] {t}: rows={res['rows']}", flush=True)

    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump({"batch_id": args.batch_id, "stage": args.stage, "tables": tables},
                  fh, ensure_ascii=False, indent=2, sort_keys=True)
    print(f"PROFILE_WRITTEN {args.out}", flush=True)
    spark.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
