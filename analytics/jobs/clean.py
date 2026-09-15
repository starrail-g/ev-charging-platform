#!/usr/bin/env python3
"""ODS → DWD 清洗作业（PySpark）。

用法（VM）:
  spark-submit --master 'local[2]' \
  --conf spark.sql.session.timeZone=UTC --conf spark.sql.shuffle.partitions=4 \
  --conf spark.locality.wait=0 --conf spark.locality.wait.node=0 \
  --conf spark.locality.wait.rack=0 --conf spark.locality.wait.process=0 \
  analytics/jobs/clean.py \
    --input  <ODS 目录> --output <DWD 目录> --quarantine <隔离目录> \
    --report <本地 clean_report.json> --batch-id <batch>

实现要点（对应 docs/architecture/analytics.md §7 与手册 §3.3 伪代码）:
- 每张表独立守恒: input = kept + duplicate + quarantine; 修复为保留行子集。
- 维表先清洗（users → stations → piles → orders → wallet），防连接放大。
- 处置优先级: 解析失败/关键缺失 → 隔离; 同主键内容冲突 → 整组隔离;
  同主键内容相同 → 保留最小 source_record_id、其余 duplicate;
  剩余: 外键/时间逻辑/计费/流水/区间冲突（reason_codes 数组）。
- 标准化: 去空格/千分位、显式"元"换算分、时间归一 UTC、状态字典型修复。
  修复判定 = 规范化结果 ≠ 原值; 修复明细只记录保留行（repair ⊆ keep）。
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime, timezone

from pyspark.sql import SparkSession, Window
from pyspark.sql import functions as F

SENTINEL = "\u0001NULL\u0001"

# --------------------------------------------------------------------------- 列规格
# kind: int_r/int_o 整数（_r 必填 / _o 可选）; float_* 浮点; ts_* 时间; money_* 金额（支持"元"）;
#       enum_r 枚举（spec["enum"] 定义）; text_r 必填文本; text_default 可空文本（空→"未知"）

def users_spec():
    return {
        "table": "users", "key": "id",
        "cols": {"id": "int_r", "phone": "text_r", "nickname": "text_default",
                 "balance_cents": "int_r", "status": "enum_r",
                 "created_at": "ts_r", "updated_at": "ts_r"},
        "enum": {"status": ["active", "frozen"]},
        "domain": [("phone", "NOT phone_canon RLIKE '^1[0-9]{10}$'", "DQ08_value_domain"),
                   ("balance_cents", "CAST(balance_cents_canon AS BIGINT) < 0", "DQ08_value_domain")],
    }


def stations_spec():
    return {
        "table": "stations", "key": "id",
        "cols": {"id": "int_r", "name": "text_r", "address": "text_default",
                 "latitude": "float_r", "longitude": "float_r", "status": "enum_r",
                 "created_at": "ts_r", "updated_at": "ts_r"},
        "enum": {"status": ["active", "inactive"]},
        "domain": [("latitude", "CAST(latitude_canon AS DOUBLE) NOT BETWEEN -90.0 AND 90.0", "DQ08_value_domain"),
                   ("longitude", "CAST(longitude_canon AS DOUBLE) NOT BETWEEN -180.0 AND 180.0", "DQ08_value_domain")],
    }


def piles_spec():
    return {
        "table": "charging_piles", "key": "id",
        "cols": {"id": "int_r", "station_id": "int_r", "pile_code": "text_r",
                 "pile_type": "enum_r", "power_kw": "float_r",
                 "unit_price_cents_per_kwh": "int_r", "status": "enum_r",
                 "total_charge_count": "int_r", "total_charge_seconds": "int_r",
                 "created_at": "ts_r", "updated_at": "ts_r"},
        "enum": {"pile_type": ["fast", "slow"],
                 "status": ["idle", "reserved", "charging", "fault", "offline"]},
        "domain": [("power_kw", "CAST(power_kw_canon AS DOUBLE) <= 0.0 OR CAST(power_kw_canon AS DOUBLE) > 1000.0", "DQ08_value_domain"),
                   ("unit_price_cents_per_kwh", "CAST(unit_price_cents_per_kwh_canon AS BIGINT) <= 0", "DQ08_value_domain"),
                   ("total_charge_count", "CAST(total_charge_count_canon AS BIGINT) < 0", "DQ08_value_domain"),
                   ("total_charge_seconds", "CAST(total_charge_seconds_canon AS BIGINT) < 0", "DQ08_value_domain")],
    }


def orders_spec():
    return {
        "table": "charging_orders", "key": "order_no",
        "cols": {"id": "int_r", "order_no": "text_r", "user_id": "int_r", "pile_id": "int_r",
                 "status": "enum_r", "reserved_at": "ts_o", "started_at": "ts_o",
                 "ended_at": "ts_o", "energy_wh": "int_r",
                 "unit_price_cents_per_kwh": "int_r", "service_fee_cents": "int_r",
                 "total_amount_cents": "money_o", "settled_at": "ts_o",
                 "created_at": "ts_r", "updated_at": "ts_r"},
        "enum": {"status": ["pending_reservation", "reserved", "charging",
                            "pending_settlement", "completed", "cancelled", "exception"]},
        "domain": [("energy_wh", "CAST(energy_wh_canon AS BIGINT) < 0", "DQ08_value_domain"),
                   ("unit_price_cents_per_kwh", "CAST(unit_price_cents_per_kwh_canon AS BIGINT) <= 0", "DQ08_value_domain"),
                   ("service_fee_cents", "CAST(service_fee_cents_canon AS BIGINT) < 0", "DQ08_value_domain"),
                   ("total_amount_cents", "CAST(total_amount_cents_canon AS BIGINT) < 0", "DQ08_value_domain")],
        "status_required": {
            "completed": ["started_at", "ended_at", "settled_at", "energy_wh", "total_amount_cents"],
            "pending_settlement": ["started_at", "ended_at", "energy_wh", "total_amount_cents"],
            "charging": ["started_at"],
            "reserved": ["reserved_at"],
            "pending_reservation": ["reserved_at"],
            "exception": [],
            "cancelled": [],
        },
    }


def wallet_spec():
    return {
        "table": "wallet_transactions", "key": "id",
        "cols": {"id": "int_r", "user_id": "int_r", "order_id": "int_o",
                 "transaction_type": "enum_r", "amount_cents": "int_r",
                 "balance_after_cents": "int_r", "created_at": "ts_r"},
        "enum": {"transaction_type": ["recharge", "charge", "refund", "adjustment"]},
        "domain": [("amount_cents", "CAST(amount_cents_canon AS BIGINT) = 0", "DQ08_value_domain"),
                   ("balance_after_cents", "CAST(balance_after_cents_canon AS BIGINT) < 0", "DQ08_value_domain")],
    }


TABLE_SPECS = {"users": users_spec, "stations": stations_spec, "charging_piles": piles_spec,
               "charging_orders": orders_spec, "wallet_transactions": wallet_spec}
TABLE_FILES = {"users": "users.csv", "stations": "stations.csv",
               "charging_piles": "charging_piles.csv", "charging_orders": "charging_orders.csv",
               "wallet_transactions": "wallet_transactions.csv"}
DWD_NAMES = {"users": "dim_user", "stations": "dim_station", "charging_piles": "dim_pile",
             "charging_orders": "fact_order", "wallet_transactions": "fact_wallet"}
MONEY_KINDS = {"money_r", "money_o"}
INT_KINDS = {"int_r", "int_o"}
FLOAT_KINDS = {"float_r", "float_o"}
TS_KINDS = {"ts_r", "ts_o"}

# --------------------------------------------------------------------------- 规范化

INT_RE = r"^-?\d+$"
FLOAT_RE = r"^-?\d+(\.\d+)?$"
TS_ISO_RE = r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$"
TS_SPACE_RE = r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$"
TS_OFFSET_RE = r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}$"
YUAN_RE = r"^-?\d+(\.\d+)?元$"


def _clean_num(col):
    return F.regexp_replace(F.trim(F.col(col)), "[ ,]", "")


def canon_int(col):
    cleaned = _clean_num(col)
    ok = cleaned.rlike(INT_RE)
    return F.when(ok, cleaned).otherwise(F.lit(None).cast("string")), ok


def canon_float(col):
    cleaned = _clean_num(col)
    ok = cleaned.rlike(FLOAT_RE)
    return F.when(ok, cleaned).otherwise(F.lit(None).cast("string")), ok


def canon_ts(col):
    c = F.trim(F.col(col))
    ts = F.coalesce(
        F.when(c.rlike(TS_ISO_RE), F.to_timestamp(c, "yyyy-MM-dd'T'HH:mm:ss'Z'")),
        F.when(c.rlike(TS_SPACE_RE), F.to_timestamp(c, "yyyy-MM-dd HH:mm:ss")),
        F.when(c.rlike(TS_OFFSET_RE), F.to_timestamp(c)),
    )
    canon = F.date_format(ts, "yyyy-MM-dd'T'HH:mm:ss'Z'")
    return canon, ts.isNotNull()


def canon_enum(col, values):
    c = F.trim(F.col(col))
    lower = F.lower(c)
    ok = lower.isin(values)
    return F.when(ok, lower), ok


def canon_text_default(col, default="未知"):
    c = F.trim(F.col(col))
    return F.when(c.isNull() | (c == ""), F.lit(default)).otherwise(c)


def canon_money(col):
    """整数分; 仅显式 unit=元 才换算, 不猜测单位。"""
    c = F.trim(F.col(col))
    yuan_ok = c.rlike(YUAN_RE)
    # 注意: 勿用 F.cast(x, "double") —— pyspark 3.4.1 实测该调用返回字符串 'double'
    # 而非 Column（随后 F.when(ok, 该串) 会把类型名当字面量），一律用 .cast() 方法形式。
    as_yuan = F.round(F.regexp_replace(c, "元$", "").cast("double") * 100).cast("long")
    as_yuan_s = as_yuan.cast("string")
    as_int, int_ok = canon_int(col)
    canon = F.when(yuan_ok, as_yuan_s).otherwise(as_int)
    return canon, (int_ok | yuan_ok)


def build_canonical(raw, spec):
    df = raw
    for col, kind in spec["cols"].items():
        if kind in MONEY_KINDS:
            canon, ok = canon_money(col)
        elif kind in INT_KINDS:
            canon, ok = canon_int(col)
        elif kind in FLOAT_KINDS:
            canon, ok = canon_float(col)
        elif kind in TS_KINDS:
            canon, ok = canon_ts(col)
        elif kind == "enum_r":
            canon, ok = canon_enum(col, spec["enum"][col])
        elif kind == "text_default":
            canon, ok = canon_text_default(col), F.lit(True)
        elif kind == "text_r":
            c = F.trim(F.col(col))
            canon, ok = c, c.isNotNull() & (c != "")
        else:
            raise ValueError(f"unknown kind {kind}")
        raw_trim = F.trim(F.col(col))
        rep = F.coalesce(canon != F.col(col), F.lit(False))
        df = (df.withColumn(f"{col}_canon", canon)
                .withColumn(f"{col}_ok", ok)
                .withColumn(f"{col}_rep", rep))

    # 解析失败 → reasons
    arrs = []

    def code_for(kind, field):
        if kind in TS_KINDS:
            return "DQ07_time_format"
        if kind == "enum_r":
            return "DQ06_status_unknown"
        if kind in INT_KINDS or kind in MONEY_KINDS or kind in FLOAT_KINDS:
            return "DQ05_type_unparsable"
        return "DQ03_missing_required"

    for col, kind in spec["cols"].items():
        blank = F.trim(F.col(col)).isNull() | (F.trim(F.col(col)) == "")
        # ok 为 NULL（原值为 NULL/空）时按“不可解析”处理：~NULL 仍为 NULL，
        # when 会静默落到 otherwise → 关键缺失永不报（实测踩坑，勿写回 ~ok）。
        bad = F.coalesce(F.col(f"{col}_ok"), F.lit(False)) == False  # noqa: E712
        if kind.endswith("_r"):
            arrs.append(F.when(blank & bad, F.array(F.lit(code_for(kind, col)))).otherwise(F.array()))
            arrs.append(F.when(~blank & bad, F.array(F.lit(code_for(kind, col)))).otherwise(F.array()))
        else:
            arrs.append(F.when(~blank & bad, F.array(F.lit(code_for(kind, col)))).otherwise(F.array()))

    for col, cond, code in spec["domain"]:
        arrs.append(F.when(F.col(f"{col}_ok") & F.expr(cond),
                           F.array(F.lit(code))).otherwise(F.array()))

    if "status_required" in spec:
        for status, cols in spec["status_required"].items():
            for col in cols:
                blank = F.trim(F.col(col)).isNull() | (F.trim(F.col(col)) == "")
                bad = F.coalesce(F.col(f"{col}_ok"), F.lit(False)) == False  # noqa: E712
                arrs.append(F.when((F.col("status_canon") == status) & blank & bad,
                                   F.array(F.lit("DQ03_missing_required"))).otherwise(F.array()))

    reasons = arrs[0]
    for a in arrs[1:]:
        reasons = F.array_union(reasons, a)
    return df.withColumn("parse_reasons", reasons)


def content_key_expr(spec):
    return F.concat_ws("\u001f", *[F.coalesce(F.col(f"{c}_canon"), F.lit(SENTINEL))
                                   for c in spec["cols"]])


def cast_expr(col, kind):
    if kind in INT_KINDS or kind in MONEY_KINDS:
        return F.col(f"{col}_canon").cast("long")
    if kind in FLOAT_KINDS:
        return F.col(f"{col}_canon").cast("double")
    if kind in TS_KINDS:
        return F.to_timestamp(F.col(f"{col}_canon"), "yyyy-MM-dd'T'HH:mm:ss'Z'")
    return F.col(f"{col}_canon")


def typed_select(spec, df_keep):
    return df_keep.select(
        F.col("batch_id"), F.col("source_record_id"),
        *[cast_expr(c, k).alias(c) for c, k in spec["cols"].items()])


# --------------------------------------------------------------------------- 单表清洗

def clean_table(spark, table, raw_path, batch_id, refs):
    spec = TABLE_SPECS[table]()
    raw = (spark.read.option("header", True).option("inferSchema", False).csv(raw_path))
    raw.cache()
    input_n = raw.count()

    df = build_canonical(raw, spec).cache()

    # S1: 解析失败/关键缺失 → 隔离
    s1_bad = df.filter(F.size("parse_reasons") > 0).withColumn("stage_reasons", F.col("parse_reasons"))
    df = df.filter(F.size("parse_reasons") == 0)

    # S2/S3: 主键分组
    key = spec["key"]
    df = df.withColumn("content_key", content_key_expr(spec))
    grp = (df.groupBy(F.col(f"{key}_canon").alias("_k"))
           .agg(F.countDistinct("content_key").alias("_nc"), F.count("*").alias("_nr")))
    conflict_keys = [r["_k"] for r in grp.filter(F.col("_nc") > 1).select("_k").collect()]
    if conflict_keys:
        df_conflict = df.filter(F.col(f"{key}_canon").isin(conflict_keys))
        df_rest = df.filter(~F.col(f"{key}_canon").isin(conflict_keys))
    else:
        df_conflict = df.limit(0)
        df_rest = df
    df_conflict = df_conflict.withColumn(
        "stage_reasons", F.array(F.lit("DQ02_key_conflict")))

    w = Window.partitionBy(f"{key}_canon").orderBy("source_record_id")
    df_rest = df_rest.withColumn("_rn", F.row_number().over(w))
    df_dups = df_rest.filter(F.col("_rn") > 1)
    df_rest = df_rest.filter(F.col("_rn") == 1)

    # S4 起只处理“去重整组/冲突组之外”的行——漏掉这行会导致重复行/冲突行
    # 同时进入 df_keep 与隔离区（双计、守恒失真，实测踩坑）。
    df = df_rest

    # S4: 剩余校验
    checks = []  # (code, condition)

    if table == "charging_piles":
        kept_stations = [r["id_canon"] for r in refs["stations"].select("id_canon").collect()]
        checks.append(("DQ09_fk_missing", ~F.col("station_id_canon").isin(kept_stations)))

    if table == "charging_orders":
        kept_users = [r["id_canon"] for r in refs["users"].select("id_canon").collect()]
        kept_piles = refs["charging_piles"].select("id_canon", "power_kw_canon").collect()
        pile_ids = [r["id_canon"] for r in kept_piles]
        power_map = {r["id_canon"]: r["power_kw_canon"] for r in kept_piles}
        checks.append(("DQ09_fk_missing",
                       ~F.col("user_id_canon").isin(kept_users) |
                       ~F.col("pile_id_canon").isin(pile_ids)))
        checks.append(("DQ07_time_order",
                       (F.col("ended_at_canon").isNotNull() & F.col("started_at_canon").isNotNull() &
                        (F.col("ended_at_canon") <= F.col("started_at_canon"))) |
                       (F.col("settled_at_canon").isNotNull() & F.col("ended_at_canon").isNotNull() &
                        (F.col("settled_at_canon") < F.col("ended_at_canon")))))
        e = F.col("energy_wh_canon").cast("long")
        p = F.col("unit_price_cents_per_kwh_canon").cast("long")
        f = F.col("service_fee_cents_canon").cast("long")
        expected = F.floor((e * p + 999) / 1000) + f
        checks.append(("DQ10_billing_mismatch",
                       (F.col("status_canon") == "completed") &
                       (F.col("total_amount_cents_canon").cast("long") != expected)))
        # 必须用带 'Z' 字面量的格式解析 canon 串：unix_timestamp 默认格式吃 "T...Z" 会得 NULL，
        # 使超物理上限检查静默永不触发（实测踩坑）。
        t_start = F.to_timestamp(F.col("started_at_canon"), "yyyy-MM-dd'T'HH:mm:ss'Z'").cast("long")
        t_end = F.to_timestamp(F.col("ended_at_canon"), "yyyy-MM-dd'T'HH:mm:ss'Z'").cast("long")
        dur = t_end - t_start
        pw = F.create_map(*[F.lit(k) for kv in power_map.items() for k in kv]) \
            if power_map else F.create_map(F.lit(""), F.lit(0.0))
        load_max = F.coalesce(pw[F.col("pile_id_canon")], F.lit(0.0)).cast("double") * 1000.0 * dur / 3600.0
        # 仅当桩存在（在已保留维表中）时才判超物理：掉桩订单 power 查不到→0 会误报，
        # 造成 DQ09 级联假因（实测 smoke 批次 231 条误报，勿去掉 isin 闸门）。
        checks.append(("DQ11_over_physical",
                       F.col("pile_id_canon").isin(pile_ids) & (dur > 0) & (e > F.floor(load_max) + 2)))

    wallet_join_done = False
    if table == "wallet_transactions":
        kept_orders = refs["charging_orders"]
        kept_all_ids = [r["id_canon"] for r in kept_orders.select("id_canon").collect()]
        completed = kept_orders.filter(F.col("status_canon") == "completed").select(
            F.col("id_canon").alias("_oid"), F.col("user_id_canon").alias("_uid"),
            F.col("total_amount_cents_canon").alias("_amt"),
            F.col("settled_at_canon").alias("_settled"))
        df = df.join(completed, F.col("order_id_canon") == F.col("_oid"), "left")
        matched = (F.col("_oid").isNotNull() &
                   (F.col("user_id_canon") == F.col("_uid")) &
                   (F.col("amount_cents_canon").cast("long")
                    == F.col("_amt").cast("long") * -1) &
                   (F.col("created_at_canon") == F.col("_settled")))
        df = df.withColumn("_receipt_bad",
                           (F.col("transaction_type_canon") == "charge") & ~matched) \
               .drop("_oid", "_uid", "_amt", "_settled")
        checks.append(("DQ10_receipt_missing", F.col("_receipt_bad")))
        checks.append(("DQ09_fk_missing",
                       (F.col("transaction_type_canon") != "charge") &
                       F.col("order_id_canon").isNotNull() &
                       ~F.col("order_id_canon").isin(kept_all_ids)))
        wallet_join_done = True

    arrs = [F.when(F.coalesce(cond, F.lit(False)), F.array(F.lit(code))).otherwise(F.array())
            for code, cond in checks]
    if arrs:
        reasons = arrs[0]
        for a in arrs[1:]:
            reasons = F.array_union(reasons, a)
        df = df.withColumn("check_reasons", reasons)
    else:
        df = df.withColumn("check_reasons", F.array().cast("array<string>"))

    if table == "charging_orders":
        w2 = (Window.partitionBy("pile_id_canon")
              .orderBy("started_at_canon", "source_record_id")
              .rowsBetween(Window.unboundedPreceding, -1))
        df = df.withColumn("_prev_max_end", F.max(F.when(
            F.col("started_at_canon").isNotNull() & F.col("ended_at_canon").isNotNull(),
            F.col("ended_at_canon"))).over(w2))
        overlap = (F.col("started_at_canon").isNotNull() & F.col("ended_at_canon").isNotNull() &
                   F.col("_prev_max_end").isNotNull() &
                   (F.col("started_at_canon") < F.col("_prev_max_end")))
        df = df.withColumn("check_reasons", F.array_union(
            F.col("check_reasons"),
            F.when(overlap, F.array(F.lit("DQ11_overlap"))).otherwise(F.array())))

    df_bad = df.filter(F.size("check_reasons") > 0).withColumn(
        "stage_reasons", F.col("check_reasons"))
    df_keep = df.filter(F.size("check_reasons") == 0)

    # 隔离区合并
    q_all = None
    for qf in (s1_bad, df_conflict, df_bad):
        sel = qf.select(
            F.col("source_record_id"), F.lit(batch_id).alias("batch_id"),
            F.lit(table).alias("table_name"),
            F.array_sort(F.array_distinct(F.col("stage_reasons"))).alias("reason_codes"))
        q_all = sel if q_all is None else q_all.unionByName(sel)

    # 修复日志（仅保留行）
    repairs = None
    for col, kind in spec["cols"].items():
        rsel = df_keep.filter(F.col(f"{col}_rep")).select(
            F.col("source_record_id"), F.lit(batch_id).alias("batch_id"),
            F.lit(table).alias("table_name"), F.lit(col).alias("field"),
            F.col(col).alias("original_value"),
            F.col(f"{col}_canon").alias("cleaned_value"))
        repairs = rsel if repairs is None else repairs.unionByName(rsel)

    kept_n = df_keep.count()
    dup_n = df_dups.count()
    quar_n = q_all.count()  # 实际隔离行数（不用 input-kept-dup 倒推，防止双计被掩盖）
    repaired_n = repairs.count()
    reason_flat: dict[str, int] = {}
    for r in q_all.groupBy("reason_codes").count().collect():
        for code in (r["reason_codes"] or []):
            reason_flat[code] = reason_flat.get(code, 0) + r["count"]

    report = {"input": input_n, "kept": kept_n, "duplicate": dup_n,
              "quarantine": quar_n, "repaired": repaired_n,
              "conservation_ok": input_n == kept_n + dup_n + quar_n,
              "quarantine_reasons": dict(sorted(reason_flat.items()))}

    return {"dwd": df_keep, "typed": typed_select(spec, df_keep), "quarantine": q_all,
            "repairs": repairs, "report": report, "spec": spec}


def run(spark, args):
    refs = {}
    results = {}
    repair_frames = []
    order = ["users", "stations", "charging_piles", "charging_orders", "wallet_transactions"]
    for table in order:
        t0 = time.time()
        r = clean_table(spark, table, f"{args.input}/{TABLE_FILES[table]}", args.batch_id, refs)
        refs[table] = r["dwd"]
        results[table] = r["report"]
        r["typed"].write.mode("error").parquet(f"{args.output}/{DWD_NAMES[table]}")
        r["quarantine"].write.mode("error").parquet(f"{args.quarantine}/{table}")
        if r["repairs"] is not None:
            repair_frames.append(r["repairs"])
        rep = r["report"]
        print(f"[{table}] input={rep['input']} kept={rep['kept']} dup={rep['duplicate']} "
              f"quar={rep['quarantine']} repair={rep['repaired']} "
              f"({time.time()-t0:.1f}s)", flush=True)

    if repair_frames:
        merged = repair_frames[0]
        for rf in repair_frames[1:]:
            merged = merged.unionByName(rf)
        merged.write.mode("error").parquet(f"{args.output}/repair_log")

    report = {"batch_id": args.batch_id, "tables": results}
    with open(args.report, "w", encoding="utf-8") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=2, sort_keys=True)
    print("CLEAN_SUMMARY " + json.dumps(
        {t: {k: v for k, v in r.items() if k != "quarantine_reasons"}
         for t, r in results.items()}, ensure_ascii=False))
    return results


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--quarantine", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--batch-id", required=True)
    args = ap.parse_args(argv)

    spark = (SparkSession.builder.appName(f"stage2-clean-{args.batch_id}")
             .config("spark.sql.session.timeZone", "UTC")
             .config("spark.sql.shuffle.partitions", "4")
             # 本地模式下 HDFS 块主机的 preferred location 与本地执行器主机不匹配时，
             # 延迟调度会让任务永久排队（SPARK-42923 征兆，实测卡 15 分钟零进展）——必须清零。
             .config("spark.locality.wait", "0")
             .config("spark.locality.wait.node", "0")
             .config("spark.locality.wait.rack", "0")
             .config("spark.locality.wait.process", "0")
             .getOrCreate())
    spark.sparkContext.setLogLevel("WARN")
    run(spark, args)
    spark.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
