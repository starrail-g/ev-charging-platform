#!/usr/bin/env python3
"""build_warehouse.py 验收测试：fixture 手算对账（需 PySpark，Ubuntu Spark 环境运行）。

覆盖:
- DQ01 fixture: 营收 1901 分 / 电量 15001Wh / 充电 5400s / 利用率 3.125% / 小时 5000,5000,5001
- crossday fixture: 跨日订单 23:30→00:30 分摊守恒; 缺测站点网格不出现（不冒充零）;
  空窗口站点补零行存在
"""
from __future__ import annotations

import json
import pathlib
import shutil
import sys
import tempfile
import unittest
from datetime import datetime, timezone

from pyspark.sql import SparkSession, functions as F

TESTS_DIR = pathlib.Path(__file__).resolve().parent
JOBS_DIR = TESTS_DIR.parent / "jobs"
sys.path.insert(0, str(JOBS_DIR))

import build_warehouse as wh  # noqa: E402
import clean as clean_job  # noqa: E402


def to_uri(p) -> str:
    return "file://" + str(pathlib.Path(p).resolve())


def write_manifest(path: pathlib.Path, batch_id: str, start: str, end: str, gaps=None):
    manifest = {
        "batch_id": batch_id, "source_type": "fixture_test",
        "data_start": start, "data_end_exclusive": end,
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "collection_gaps": gaps or [],
    }
    path.write_text(json.dumps(manifest, ensure_ascii=False), encoding="utf-8")
    return manifest


class WarehouseCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spark = (SparkSession.builder.master("local[2]")
                     .appName("test-warehouse")
                     .config("spark.sql.session.timeZone", "UTC")
                     .config("spark.sql.shuffle.partitions", "2")
                     .config("spark.ui.enabled", "false")
                     .getOrCreate())
        cls.spark.sparkContext.setLogLevel("ERROR")
        cls.tmp = pathlib.Path(tempfile.mkdtemp(prefix="wh-test-"))

    @classmethod
    def tearDownClass(cls):
        cls.spark.stop()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def build(self, fixture: str, batch_id: str, start: str, end: str, gaps=None):
        work = self.tmp / batch_id
        fx = TESTS_DIR / "fixtures" / fixture
        args = type("A", (), {
            "input": to_uri(fx), "output": to_uri(work / "dwd"),
            "quarantine": to_uri(work / "quarantine"),
            "report": str(work / "clean_report.json"), "batch_id": batch_id})()
        clean_job.run(self.spark, args)
        manifest_path = work / "manifest.json"
        write_manifest(manifest_path, batch_id, start, end, gaps)
        manifest = wh.load_manifest(str(manifest_path))
        manifest["clean_report"] = str(work / "clean_report.json")
        wh.run(self.spark, to_uri(work / "dwd"), to_uri(work), batch_id, manifest)
        return work


class TestDq01Warehouse(WarehouseCase):
    def test_handcheck(self):
        work = self.build("dq01", "fx-dq01", "2026-09-01T00:00:00Z", "2026-09-02T00:00:00Z")
        # 时间标签在 Spark 侧格式化（session tz=UTC）：PySpark 收集 timestamps 得到的是
        # 「驱动本地时区」naive datetime，直接 strftime 在非 UTC 驱动机上会错位（实测踩坑）。
        hour = {(r["h"], r["station_id"]): r for r in
                self.spark.read.parquet(to_uri(work / "dws" / "dws_station_hour"))
                .select(F.date_format("hour_start", "HH").alias("h"), "station_id",
                        "allocated_wh", "charge_seconds", "capacity_pile_seconds").collect()}
        self.assertEqual(len(hour), 24, "24 小时网格")
        self.assertAlmostEqual(hour[("00", 1)]["allocated_wh"], 5000, places=6)
        self.assertAlmostEqual(hour[("01", 1)]["allocated_wh"], 5000, places=6)
        self.assertAlmostEqual(hour[("02", 1)]["allocated_wh"], 5001, places=6)
        self.assertEqual(hour[("00", 1)]["charge_seconds"], 1800)
        self.assertEqual(hour[("00", 1)]["capacity_pile_seconds"], 7200)
        total_alloc = sum(r["allocated_wh"] for r in hour.values())
        self.assertAlmostEqual(total_alloc, 15001, places=3)

        rank = self.spark.read.parquet(to_uri(work / "ads" / "ads_station_rank")).collect()
        self.assertEqual(len(rank), 1)
        self.assertAlmostEqual(rank[0]["utilization"], 0.03125, places=9)
        self.assertEqual(rank[0]["revenue_cents"], 1901)
        self.assertEqual(rank[0]["completed_orders"], 2)

        ov = self.spark.read.parquet(to_uri(work / "ads" / "ads_overview")).collect()[0]
        self.assertEqual(ov["revenue_cents"], 1901)
        self.assertEqual(ov["energy_wh"], 15001)
        self.assertEqual(ov["completed_orders"], 2)

        users = self.spark.read.parquet(to_uri(work / "ads" / "ads_user_activity")).collect()
        self.assertEqual(sum(r["frequency"] for r in users), 2)
        self.assertEqual(sum(r["monetary"] for r in users), 1901)
        orders = self.spark.read.parquet(to_uri(work / "ads" / "ads_order_activity")).collect()
        self.assertEqual(sum(r["order_count"] for r in orders if r["status"] == 'completed'), 2)
        self.assertEqual(sum(r["duration_seconds"] for r in orders), 5400)

        trend = self.spark.read.parquet(to_uri(work / "ads" / "ads_revenue_trend")).collect()
        self.assertEqual(len(trend), 1)
        self.assertEqual(str(trend[0]["stat_date"]), "2026-09-01")
        self.assertEqual(trend[0]["revenue_cents"], 1901)

        load = {r["h"]: r for r in
                self.spark.read.parquet(to_uri(work / "ads" / "ads_load_hour"))
                .select(F.date_format("hour_start", "HH").alias("h"), "load_kw").collect()}
        self.assertAlmostEqual(load["00"]["load_kw"], 5.0, places=6)
        self.assertAlmostEqual(load["02"]["load_kw"], 5.001, places=6)

        # 站点发布字段（大屏地图/命名来源）：name 与有限坐标必须随桩快照带出
        snap = self.spark.read.parquet(to_uri(work / "ads" / "ads_pile_snapshot")).collect()
        self.assertTrue(snap, "ads_pile_snapshot 不应为空")
        for r in snap:
            self.assertTrue(r["station_name"], "station_name 不得为空")
            self.assertIsInstance(r["latitude"], float)
            self.assertIsInstance(r["longitude"], float)


class TestCrossdayWarehouse(WarehouseCase):
    def test_crossday_split_and_gap(self):
        gaps = [{"station_id": 2, "gap_start": "2026-09-03T00:00:00Z",
                 "gap_end_exclusive": "2026-09-05T00:00:00Z", "note": "fixture gap"}]
        work = self.build("crossday", "fx-crossday", "2026-09-03T00:00:00Z",
                          "2026-09-05T00:00:00Z", gaps)
        hour = [(r["label"], r["station_id"], r["allocated_wh"]) for r in
                self.spark.read.parquet(to_uri(work / "dws" / "dws_station_hour"))
                .select(F.date_format("hour_start", "MM-dd HH").alias("label"),
                        "station_id", "allocated_wh").collect()]
        total = sum(h[2] for h in hour)
        # 只有站点1 有数据; 站点2 缺测（网格不出现）
        self.assertTrue(all(h[1] == 1 for h in hour), "缺测站点不应出现网格行")
        self.assertAlmostEqual(total, 20000, places=3, msg="跨日分摊守恒")
        h23 = [h for h in hour if h[0] == "09-03 23"]
        h00 = [h for h in hour if h[0] == "09-04 00"]
        self.assertEqual(len(h23), 1)
        self.assertEqual(len(h00), 1)
        self.assertAlmostEqual(h23[0][2], 10000, places=6)
        self.assertAlmostEqual(h00[0][2], 10000, places=6)
        self.assertEqual(len(hour), 48, "站点1 48 小时网格, 站点2 全缺测不出现")

        day = [(str(r["stat_date"]), r["revenue_cents"]) for r in
               self.spark.read.parquet(to_uri(work / "dws" / "dws_station_day")).collect()]
        self.assertIn(("2026-09-03", 0), day, "空日补零行存在")
        self.assertIn(("2026-09-04", 2000), day, "结算日归 09-04")
        self.assertTrue(all(d[0] >= "2026-09-03" for d in day))
        self.assertEqual(len(day), 2, "站点2 缺测不出现")

        rank = self.spark.read.parquet(to_uri(work / "ads" / "ads_station_rank")).collect()
        util = {r["station_id"]: r["utilization"] for r in rank}
        self.assertAlmostEqual(util[1], 3600 / (1 * 48 * 3600), places=9)


if __name__ == "__main__":
    unittest.main(verbosity=2)
