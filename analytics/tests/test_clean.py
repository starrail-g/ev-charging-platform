#!/usr/bin/env python3
"""clean.py 验收测试：fixture 回放 + 守恒/原因码/修复断言（需 PySpark，Ubuntu Spark 环境运行）。

运行（VM）:
  cd ev-charging-platform
  python3 -m unittest discover -s analytics/tests -p 'test_clean*.py' -v
（pyspark 走 Spark 发行版自带 zip；见 scripts/stage2/run_tests.sh）
"""
from __future__ import annotations

import csv
import json
import pathlib
import shutil
import sys
import tempfile
import unittest

from pyspark.sql import SparkSession

TESTS_DIR = pathlib.Path(__file__).resolve().parent
JOBS_DIR = TESTS_DIR.parent / "jobs"
sys.path.insert(0, str(JOBS_DIR))

import clean as clean_job  # noqa: E402


def to_uri(p) -> str:
    """Spark 路径显式加 file:// 前缀（默认 fs 是 hdfs:// 时裸绝对路径会被误当 HDFS）。"""
    return "file://" + str(pathlib.Path(p).resolve())


class TestFixtureIntegrity(unittest.TestCase):
    """夹具守门：所有 CSV 每行字段数必须与表头一致——手工编辑多/少一个逗号会
    造成整行错位（Spark 静默丢字段），曾真实踩坑，勿删。"""

    def test_all_fixture_csv_wellformed(self):
        fixtures_dir = TESTS_DIR / "fixtures"
        checked = 0
        for csv_path in sorted(fixtures_dir.rglob("*.csv")):
            with open(csv_path, newline="", encoding="utf-8") as fh:
                rows = list(csv.reader(fh))
            header_n = len(rows[0])
            for line_no, row in enumerate(rows[1:], start=2):
                self.assertEqual(
                    len(row), header_n,
                    f"{csv_path.relative_to(fixtures_dir)}:{line_no} 字段数 {len(row)} != 表头 {header_n}")
            checked += 1
        self.assertGreaterEqual(checked, 9, "夹具 CSV 数量异常")


class FixtureCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spark = (SparkSession.builder.master("local[2]")
                     .appName("test-clean")
                     .config("spark.sql.session.timeZone", "UTC")
                     .config("spark.sql.shuffle.partitions", "2")
                     .config("spark.ui.enabled", "false")
                     .getOrCreate())
        cls.spark.sparkContext.setLogLevel("ERROR")
        cls.tmp = pathlib.Path(tempfile.mkdtemp(prefix="clean-test-"))
        cls.results = {}

    @classmethod
    def tearDownClass(cls):
        cls.spark.stop()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    @classmethod
    def run_fixture(cls, name: str, batch_id: str):
        if name in cls.results:
            return cls.results[name]
        fx = TESTS_DIR / "fixtures" / name
        out = cls.tmp / name
        args = type("A", (), {
            "input": to_uri(fx), "output": to_uri(out / "dwd"),
            "quarantine": to_uri(out / "quarantine"),
            "report": str(out / "report.json"), "batch_id": batch_id})()
        clean_job.run(cls.spark, args)
        report = json.loads((out / "report.json").read_text(encoding="utf-8"))
        cls.results[name] = (out, report)
        return cls.results[name]

    def quarantine_map(self, out: pathlib.Path, table: str) -> dict[str, list[str]]:
        rows = self.spark.read.parquet(to_uri(out / "quarantine" / table)).collect()
        return {r["source_record_id"]: sorted(r["reason_codes"]) for r in rows}

    def dwd_rows(self, out: pathlib.Path, name: str) -> list:
        return self.spark.read.parquet(to_uri(out / "dwd" / name)).collect()


class TestDq01(FixtureCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.out, cls.report = cls.run_fixture("dq01", "fx-dq01")
        cls.expected = json.loads((TESTS_DIR / "fixtures" / "dq01" / "expected.json")
                                  .read_text(encoding="utf-8"))

    def test_orders_conservation(self):
        got = self.report["tables"]["charging_orders"]
        exp = self.expected["orders"]
        for k in ("input", "kept", "duplicate", "quarantine", "repaired"):
            self.assertEqual(got[k], exp[k], f"orders.{k}: {got[k]} != {exp[k]}")
        self.assertTrue(got["conservation_ok"])

    def test_wallet_conservation(self):
        got = self.report["tables"]["wallet_transactions"]
        exp = self.expected["wallet"]
        for k in ("input", "kept", "duplicate", "quarantine", "repaired"):
            self.assertEqual(got[k], exp[k], f"wallet.{k}: {got[k]} != {exp[k]}")

    def test_kept_orders(self):
        rows = self.dwd_rows(self.out, "fact_order")
        order_nos = sorted(r["order_no"] for r in rows)
        self.assertEqual(order_nos, sorted(self.expected["kept_order_nos"]))
        revenue = sum(r["total_amount_cents"] for r in rows if r["status"] == "completed")
        self.assertEqual(revenue, self.expected["revenue_cents"])
        energy = sum(r["energy_wh"] for r in rows if r["status"] == "completed")
        self.assertEqual(energy, self.expected["settled_energy_wh"])

    def test_quarantine_reasons(self):
        q = self.quarantine_map(self.out, "charging_orders")
        self.assertEqual(set(q.keys()), {"orders-000004", "orders-000005", "orders-000006"})
        self.assertIn("DQ09_fk_missing", q["orders-000004"])
        self.assertIn("DQ07_time_order", q["orders-000005"])
        self.assertIn("DQ03_missing_required", q["orders-000006"])

    def test_dims_all_kept(self):
        for t in ("users", "stations", "charging_piles"):
            rep = self.report["tables"][t]
            self.assertEqual(rep["quarantine"], 0, t)
            self.assertEqual(rep["duplicate"], 0, t)


class TestExtras(FixtureCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.out, cls.report = cls.run_fixture("extras", "fx-extras")
        cls.expected = json.loads((TESTS_DIR / "fixtures" / "extras" / "expected_extras.json")
                                  .read_text(encoding="utf-8"))

    def test_conservation(self):
        for table, key in (("charging_orders", "orders"), ("wallet_transactions", "wallet")):
            got = self.report["tables"][table]
            exp = self.expected[key]
            for k in ("input", "kept", "duplicate", "quarantine", "repaired"):
                self.assertEqual(got[k], exp[k], f"{table}.{k}: {got[k]} != {exp[k]}")

    def test_quarantine_reasons_exact(self):
        q = self.quarantine_map(self.out, "charging_orders")
        exp = self.expected["quarantine_expected"]
        self.assertEqual(set(q.keys()), set(exp.keys()))
        for srid, codes in exp.items():
            self.assertEqual(q[srid], codes, f"{srid}: {q[srid]} != {codes}")

    def test_repair_log(self):
        rows = self.spark.read.parquet(to_uri(self.out / "dwd" / "repair_log")).collect()
        exp = self.expected["repair_expected"]
        got = {}
        for r in rows:
            got.setdefault(r["source_record_id"], {})[r["field"]] = {
                "original": r["original_value"], "cleaned": r["cleaned_value"]}
        self.assertEqual(got, exp)

    def test_kept_only_valid(self):
        rows = self.dwd_rows(self.out, "fact_order")
        self.assertEqual([r["order_no"] for r in rows], self.expected["kept_order_nos"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
