from __future__ import annotations

import importlib.util
import json
import sqlite3
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

import pytest

pyspark = pytest.importorskip("pyspark")

from pyspark.sql import SparkSession, functions as F, types as T  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
PIPELINE = ROOT / "ml/jobs/run_pipeline.py"
GENERATOR = ROOT / "ml/data/generate_analysis_dataset.py"
SNAPSHOT = ROOT / "ml/service/build_dashboard_snapshot.py"


def _load_module():
    spec = importlib.util.spec_from_file_location("run_pipeline", PIPELINE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


RUN_PIPELINE = _load_module()


def _utc(*args: int) -> datetime:
    """tz-aware UTC 构造：裸 datetime 会被 createDataFrame 按驱动本地时区解释。"""
    return datetime(*args, tzinfo=timezone.utc)


def _by_hour(df):
    """按 session 时区（UTC）渲染的小时字符串取行，与驱动本地时区无关。"""
    keyed = df.select(F.date_format("hour_start", "yyyy-MM-dd HH:mm:ss").alias("hour_key"), "*")
    return {row["hour_key"]: row for row in keyed.collect()}


@pytest.fixture(scope="module")
def spark():
    session = (SparkSession.builder.master("local[2]").appName("ml-pipeline-tests")
               .config("spark.sql.session.timeZone", "UTC")
               .config("spark.ui.enabled", "false")
               # 测试环境可能装有 Hadoop 配置：强制本地文件系统，避免 /tmp 被解析到 HDFS
               .config("spark.hadoop.fs.defaultFS", "file:///")
               .config("spark.locality.wait", "0")
               .config("spark.locality.wait.node", "0")
               .config("spark.locality.wait.rack", "0")
               .config("spark.locality.wait.process", "0")
               .getOrCreate())
    session.sparkContext.setLogLevel("WARN")
    yield session
    session.stop()


DWD_SCHEMA = T.StructType([
    T.StructField("order_id", T.LongType()),
    T.StructField("user_id", T.LongType()),
    T.StructField("station_id", T.LongType()),
    T.StructField("energy_wh", T.LongType()),
    T.StructField("total_amount_cents", T.LongType()),
    T.StructField("quality_valid", T.BooleanType()),
    T.StructField("quality_code", T.StringType()),
    T.StructField("started_ts", T.TimestampType()),
    T.StructField("ended_ts", T.TimestampType()),
])

PILE_SCHEMA = T.StructType([
    T.StructField("id", T.LongType()),
    T.StructField("station_id", T.LongType()),
])


def _dwd(spark, rows):
    return spark.createDataFrame(rows, DWD_SCHEMA)


def _piles(spark, rows):
    return spark.createDataFrame(rows, PILE_SCHEMA)


def test_allocation_conserves_seconds_energy_and_revenue(spark) -> None:
    """①Σ分摊充电秒数 = 源秒数 ②Σ分摊电量 = 源电量（跨小时订单逐桶分摊）。"""
    dwd = _dwd(spark, [
        (101, 1, 1, 1200, 600, True, None, _utc(2026, 9, 10, 10, 30), _utc(2026, 9, 10, 11, 30)),
        (102, 2, 1, 400, 200, True, None, _utc(2026, 9, 10, 11, 50), _utc(2026, 9, 10, 12, 10)),
    ])
    hourly, daily = RUN_PIPELINE.allocation_facts(dwd)
    rows = _by_hour(hourly)
    assert rows["2026-09-10 10:00:00"]["charge_seconds"] == 1800
    assert rows["2026-09-10 10:00:00"]["allocated_wh"] == pytest.approx(600.0)
    assert rows["2026-09-10 10:00:00"]["allocated_cents"] == pytest.approx(300.0)
    assert rows["2026-09-10 11:00:00"]["charge_seconds"] == 1800 + 600
    assert rows["2026-09-10 11:00:00"]["allocated_wh"] == pytest.approx(600.0 + 200.0)
    assert rows["2026-09-10 12:00:00"]["charge_seconds"] == 600
    assert rows["2026-09-10 12:00:00"]["allocated_wh"] == pytest.approx(200.0)

    total_seconds = hourly.groupBy().sum("charge_seconds").collect()[0][0]
    total_wh = hourly.groupBy().sum("allocated_wh").collect()[0][0]
    assert total_seconds == 3600 + 1200
    assert total_wh == pytest.approx(1200.0 + 400.0)
    assert daily.groupBy().sum("charge_seconds").collect()[0][0] == 3600 + 1200


def test_zero_order_hours_keep_capacity_in_denominator(spark) -> None:
    """③分母含零订单小时；且小时/日两层窗口统一：每日小时容量之和＝日容量。"""
    dwd = _dwd(spark, [
        (101, 1, 1, 1200, 600, True, None, _utc(2026, 9, 10, 10, 30), _utc(2026, 9, 10, 11, 30)),
        (102, 2, 1, 400, 200, True, None, _utc(2026, 9, 10, 14, 0), _utc(2026, 9, 10, 14, 30)),
    ])
    piles = _piles(spark, [(11, 1), (12, 1)])
    lo, hi = RUN_PIPELINE.hour_window(dwd)
    assert lo == "2026-09-10 10:00:00" and hi == "2026-09-10 14:00:00"

    hours = RUN_PIPELINE.hour_sequence(spark, lo, hi)
    hourly_alloc, daily_alloc = RUN_PIPELINE.allocation_facts(dwd)
    hourly = RUN_PIPELINE.grid_hourly(hourly_alloc, piles, hours)
    rows = _by_hour(hourly)
    assert sorted(rows) == [f"2026-09-10 {hour:02d}:00:00" for hour in (10, 11, 12, 13, 14)]
    zero = rows["2026-09-10 12:00:00"]
    assert zero["charge_seconds"] == 0 and zero["session_count"] == 0
    assert zero["utilization"] == 0.0
    assert zero["capacity_pile_seconds"] == 2 * 3600      # 分母仍覆盖零订单小时
    assert rows["2026-09-10 10:00:00"]["utilization"] == pytest.approx(1800 / (2 * 3600))
    assert rows["2026-09-10 14:00:00"]["utilization"] == pytest.approx(1800 / (2 * 3600))

    station_day = RUN_PIPELINE.grid_daily(daily_alloc, piles, hours)
    day_rows = {row["stat_date"].isoformat(): row for row in station_day.collect()}
    day = day_rows["2026-09-10"]
    assert day["hours_in_window"] == 5                     # 该日在小时网格中的桶数
    assert day["charge_seconds"] == 3600 + 1800
    assert day["capacity_pile_seconds"] == 2 * 5 * 3600    # 每日小时容量之和＝日容量
    assert day["utilization"] == pytest.approx((3600 + 1800) / (2 * 5 * 3600))
    assert day["load_kwh"] == pytest.approx(1.6)
    # 两层窗口统一：Σ小时容量 == Σ日容量（全站全窗口）
    hourly_capacity = hourly.groupBy().sum("capacity_pile_seconds").collect()[0][0]
    daily_capacity = station_day.groupBy().sum("capacity_pile_seconds").collect()[0][0]
    assert hourly_capacity == daily_capacity


def test_pipeline_quarantine_conservation_and_snapshot_same_source(spark, tmp_path: Path) -> None:
    """端到端：quarantine 独立落盘守恒（输入=有效+无效）；快照利用率与 DWS 同口径抽查一致。"""
    database = tmp_path / "analysis.sqlite"
    ods = tmp_path / "ods"
    subprocess.run([sys.executable, str(GENERATOR), "--output", str(database), "--ods-dir", str(ods),
                    "--seed", "9", "--days", "8", "--users", "4", "--stations", "2",
                    "--piles-per-station", "3", "--orders", "40", "--end-date", "2026-09-17"],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    output = tmp_path / "pipeline"
    report = RUN_PIPELINE.run_build(spark, ods, output)

    # 守恒与互斥：全部从两个实际落盘产物读回核验（不再是内存分类计数）
    dwd_written = spark.read.parquet(str(output / "dwd" / "charging_orders"))
    quarantine_written = spark.read.parquet(str(output / "dwd" / "quarantine_charging_orders"))
    assert dwd_written.count() == report["dwd_rows"]
    assert quarantine_written.count() == report["quarantine_rows"]
    assert report["conservation"]["balanced"] is True
    assert report["conservation"]["exclusive"] is True
    assert report["input_rows"] == report["dwd_rows"] + report["quarantine_rows"]
    assert (dwd_written.select("source_row_id")
            .join(quarantine_written.select("source_row_id"), "source_row_id").count()) == 0
    assert dwd_written.filter(~F.col("quality_valid")).count() == 0    # 正式 DWD 不含无效行

    hourly = spark.read.parquet(str(output / "dws" / "station_hourly"))
    station_day = spark.read.parquet(str(output / "dws" / "station_day"))
    assert station_day.count() == report["dws_station_day_rows"]
    # 分摊守恒：Σ分摊充电秒数 = 有效订单源秒数
    total_allocated = hourly.groupBy().sum("charge_seconds").collect()[0][0]
    source_seconds = (
        dwd_written.filter(F.col("started_ts").isNotNull() & F.col("ended_ts").isNotNull()
                           & (F.col("ended_ts") > F.col("started_ts")))
        .select((F.unix_timestamp("ended_ts") - F.unix_timestamp("started_ts")).alias("s"))
        .agg(F.sum("s")).collect()[0][0]
    )
    assert total_allocated == source_seconds
    # 窗口统一：Σ小时容量 == Σ日容量
    assert (hourly.groupBy().sum("capacity_pile_seconds").collect()[0][0]
            == station_day.groupBy().sum("capacity_pile_seconds").collect()[0][0])

    snapshot_path = tmp_path / "dashboard.json"
    subprocess.run([sys.executable, str(SNAPSHOT), "--database", str(database),
                    "--dws", str(output / "dws"), "--output", str(snapshot_path)],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))

    # 快照消费 DWS：逐站利用率必须与 station_day 汇总严格一致（同源验收）
    per_station = station_day.groupBy("station_id").agg(
        F.sum("charge_seconds").alias("seconds"),
        F.sum("capacity_pile_seconds").alias("capacity")).collect()
    expected = {int(r["station_id"]): round(min(1.0, (r["seconds"] or 0) / r["capacity"]), 4)
                for r in per_station if r["capacity"]}
    for row in snapshot["stationUtilization"]:
        assert row["utilization"] == pytest.approx(expected.get(row["station_id"], 0.0), abs=1e-4), row["station_id"]

    # 反证：业务库全量口径（含被 quarantine 的完成单）与快照值必须不同——快照不再自行推算
    connection = sqlite3.connect(database)
    db_all = connection.execute(
        "SELECT COALESCE(SUM((julianday(o.ended_at) - julianday(o.started_at)) * 86400), 0) "
        "FROM charging_orders o JOIN charging_piles p ON o.pile_id = p.id "
        "WHERE p.station_id = 101 AND o.status='completed' AND o.started_at IS NOT NULL "
        "AND o.ended_at IS NOT NULL AND o.ended_at > o.started_at").fetchone()[0]
    dws_101 = (station_day.filter(F.col("station_id") == 101)
               .groupBy().sum("charge_seconds").collect()[0][0] or 0)
    row_101 = [r for r in snapshot["stationUtilization"] if r["station_id"] == 101][0]
    assert db_all > dws_101                        # 业务库含被隔离完成单（tiny 用例必现）
    assert row_101["charge_seconds"] == dws_101    # 快照展示的是 DWS 口径
    connection.close()
