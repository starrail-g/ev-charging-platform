#!/usr/bin/env python3
"""generate_data.py 验收测试：确定性、合法底稿、污染标签、manifest（纯标准库，无需 Spark）。

运行:
  python3 -m unittest discover -s analytics/tests -p 'test_generator*.py' -v
"""
from __future__ import annotations

import csv
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from collections import Counter
from datetime import datetime, timedelta, timezone

from analytics.generate_data import Generator, PROFILES, hour_weight, weighted_slot_offsets

REPO = pathlib.Path(__file__).resolve().parents[2]
GEN = REPO / "analytics" / "generate_data.py"
RULES = [f"DQ{i:02d}" for i in range(1, 12)]


def load_csv(path):
    with open(path, encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def parse_iso(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


class GeneratorCase(unittest.TestCase):
    profile = "smoke"

    @classmethod
    def setUpClass(cls):
        cls.tmp = pathlib.Path(tempfile.mkdtemp(prefix="gen-test-"))
        for i in (1, 2):
            out = cls.tmp / f"gen{i}"
            subprocess.run(
                [sys.executable, str(GEN), "--profile", cls.profile, "--seed", "20260914",
                 "--batch-id", "gen-test", "--out-root", str(out), "--repo-root", str(REPO)],
                check=True, capture_output=True, text=True)
        cls.a = cls.tmp / "gen1" / "batches" / "gen-test"
        cls.b = cls.tmp / "gen2" / "batches" / "gen-test"
        cls.orders = load_csv(cls.a / "input" / "charging_orders.csv")
        cls.wallet = load_csv(cls.a / "input" / "wallet_transactions.csv")
        cls.users = load_csv(cls.a / "input" / "users.csv")
        cls.piles = load_csv(cls.a / "input" / "charging_piles.csv")
        with open(cls.a / "input" / "dirty_labels.jsonl", encoding="utf-8") as fh:
            cls.labels = [json.loads(line) for line in fh]
        cls.manifest = json.loads((cls.a / "manifest.json").read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_deterministic_outputs(self):
        for name in ("users.csv", "stations.csv", "charging_piles.csv", "charging_orders.csv",
                     "wallet_transactions.csv", "dirty_labels.jsonl"):
            a = (self.a / "input" / name).read_bytes()
            b = (self.b / "input" / name).read_bytes()
            self.assertEqual(hashlib.sha256(a).hexdigest(), hashlib.sha256(b).hexdigest(),
                             f"{name} 同 seed 两次运行不一致")

    def test_manifest_hashes_match_files(self):
        self.assertEqual(self.manifest["generator_version"], "1.1.0")
        for table, meta in self.manifest["tables"].items():
            f = self.a / meta["file"]
            self.assertEqual(hashlib.sha256(f.read_bytes()).hexdigest(), meta["sha256"], table)
            rows = len(f.read_text(encoding="utf-8").splitlines()) - 1
            self.assertEqual(rows, meta["rows"], table)

    def test_unlabeled_rows_valid(self):
        labeled = {l["source_record_id"] for l in self.labels}
        group_onos = {l["original_value"] for l in self.labels
                      if l["expected_action"] == "quarantine(group)"}
        txn_by_order = {}
        for w in self.wallet:
            if w["transaction_type"] == "charge":
                txn_by_order.setdefault(w["order_id"], w)
        user_ids = {u["id"] for u in self.users}
        pile_ids = {p["id"] for p in self.piles}
        checked = 0
        for o in self.orders:
            if o["source_record_id"] in labeled:
                continue  # 注入污染行由标签负责
            if o["order_no"] in group_onos:
                continue  # DQ02 原始行（整组隔离，属预期行为）
            self.assertIn(o["user_id"], user_ids, o["source_record_id"])
            self.assertIn(o["pile_id"], pile_ids, o["source_record_id"])
            if o["status"] != "completed":
                continue
            checked += 1
            expected = (int(o["energy_wh"]) * int(o["unit_price_cents_per_kwh"]) + 999) // 1000 \
                + int(o["service_fee_cents"])
            self.assertEqual(int(o["total_amount_cents"]), expected, o["order_no"])
            w = txn_by_order.get(o["id"])
            self.assertIsNotNone(w, f"订单 {o['order_no']} 缺少扣款流水")
            self.assertEqual(int(w["amount_cents"]), -int(o["total_amount_cents"]))
            self.assertEqual(w["created_at"], o["settled_at"])
            self.assertEqual(w["user_id"], o["user_id"])
        self.assertGreater(checked, 700, "小样完成的校验行数异常偏少")

    def test_no_overlap_unlabeled(self):
        labeled = {l["source_record_id"] for l in self.labels}
        by_pile = {}
        for o in self.orders:
            if o["source_record_id"] in labeled:
                continue
            if o["started_at"] and o["ended_at"] and o["ended_at"] > o["started_at"]:
                by_pile.setdefault(o["pile_id"], []).append(
                    (parse_iso(o["started_at"]), parse_iso(o["ended_at"]), o["source_record_id"]))
        for pile, spans in by_pile.items():
            spans.sort()
            for (s1, e1, _), (s2, e2, sid2) in zip(spans, spans[1:]):
                self.assertGreaterEqual(s2, e1, f"桩 {pile} 区间重叠于 {sid2}")

    def test_labels_cover_all_rules(self):
        from collections import Counter
        counts = Counter(l["rule"] for l in self.labels)
        for rule in RULES:
            self.assertGreaterEqual(counts.get(rule, 0), 3, f"{rule} 正例不足")
        for l in self.labels:
            for key in ("source_record_id", "table", "rule", "field", "original_value",
                        "polluted_value", "expected_action"):
                self.assertIn(key, l)
        self.assertEqual(self.manifest["pollution"]["injected_label_rows"], len(self.labels))
        polluted = {l["source_record_id"] for l in self.labels if l["source_record_id"]}
        self.assertEqual(self.manifest["pollution"]["distinct_polluted_records"], len(polluted))

    def test_wallet_balance_non_negative(self):
        labeled = {l["source_record_id"] for l in self.labels}
        for w in self.wallet:
            if w["source_record_id"] in labeled:
                continue
            self.assertGreaterEqual(int(w["balance_after_cents"]), 0)

    def test_time_of_day_curves_have_weekday_weekend_shape(self):
        monday = datetime(2026, 9, 14, tzinfo=timezone.utc)
        saturday = datetime(2026, 9, 12, tzinfo=timezone.utc)
        self.assertGreater(hour_weight(monday.replace(hour=8)), hour_weight(monday.replace(hour=2)))
        self.assertGreater(hour_weight(monday.replace(hour=18)), hour_weight(monday.replace(hour=2)))
        self.assertGreater(hour_weight(saturday.replace(hour=13)), hour_weight(saturday.replace(hour=8)))
        self.assertLess(hour_weight(saturday.replace(hour=8)), hour_weight(monday.replace(hour=8)))

        offsets = weighted_slot_offsets(monday, monday + timedelta(days=14), 200)
        self.assertEqual(len(offsets), 200)
        self.assertEqual(offsets, sorted(offsets))
        self.assertGreater(len(set(offsets)), 190)
        for count in (0, 1, 2, 100):
            offsets = weighted_slot_offsets(monday, monday + timedelta(days=14), count)
            self.assertEqual(len(offsets), count)
            if count:
                self.assertEqual(offsets[0], 0)
                self.assertLess(offsets[-1], 14 * 86400)
                self.assertEqual(len(set(offsets)), count)

    def test_generated_times_stay_inside_window(self):
        labeled = {l["source_record_id"] for l in self.labels}
        t0 = parse_iso(self.manifest["data_start"])
        t1 = parse_iso(self.manifest["data_end_exclusive"])
        for row in self.orders:
            if row["source_record_id"] in labeled:
                continue  # 有意污染由底稿测试覆盖，不能用业务 id 匹配来源记录标签。
            for field in ("started_at", "ended_at", "settled_at"):
                if row[field]:
                    value = parse_iso(row[field])
                    self.assertGreaterEqual(value, t0, (row["source_record_id"], field))
                    self.assertLess(value, t1, (row["source_record_id"], field))


class LowProfileGeneratorCase(GeneratorCase):
    profile = "low"

    def test_actual_weekday_weekend_distribution(self):
        """发布档真实 CSV 的每小时日均单量；仅检查权重数组不能发现均匀抖动回归。"""
        labeled = {l["source_record_id"] for l in self.labels}
        counts = {False: Counter(), True: Counter()}
        days = Counter()
        day = parse_iso(self.manifest["data_start"])
        while day < parse_iso(self.manifest["data_end_exclusive"]):
            days[day.weekday() >= 5] += 1
            day += timedelta(days=1)
        for row in self.orders:
            if row["source_record_id"] in labeled or not row["started_at"]:
                continue
            started = parse_iso(row["started_at"])
            counts[started.weekday() >= 5][started.hour] += 1

        def rate(weekend, hours):
            return sum(counts[weekend][h] for h in hours) / (days[weekend] * len(hours))

        for weekend in (False, True):
            self.assertGreater(sum(counts[weekend].values()), 1000)
            night = rate(weekend, range(0, 6))
            self.assertGreater(rate(weekend, range(11, 14)), night * 2)
            self.assertGreater(rate(weekend, range(17, 21)), night * 2)
        self.assertGreater(rate(False, range(7, 10)), rate(False, range(0, 6)) * 2)
        self.assertLess(rate(True, range(7, 10)), rate(False, range(7, 10)) * 0.9)
        self.assertGreater(rate(True, range(11, 14)), rate(True, range(7, 10)) * 1.2)
        self.assertGreater(rate(True, range(17, 21)), rate(True, range(7, 10)) * 1.2)

    def test_low_profile_regression_dq05_dq11_collision(self):
        """回归（9/15）：low 档 + seed 20260914 曾因 DQ05 将 power_kw 脏化为带空格字符串、
        DQ11 又对该桩做 功率×时长 数值运算而 TypeError 崩溃（sequence * float）。
        修复（float() 还原）后应完整产出批次，且 DQ11 标签与 manifest 哈希一致。"""
        self.assertGreaterEqual(len([l for l in self.labels if l["rule"] == "DQ11"]), 3,
                                "DQ11 正例缺失")
        self.assertEqual(self.manifest["profile"], "low")


class CleanDraftCase(unittest.TestCase):
    def test_all_profiles_before_pollution(self):
        """检查全部合法底稿，避免标签过滤掩盖边界、同桩重叠及活动订单问题。"""
        for profile in PROFILES:
            with self.subTest(profile=profile):
                g = Generator(profile, 20260914, "draft", REPO.parent, REPO)
                g.gen_users()
                g.gen_stations()
                g.gen_piles()
                g.gen_orders()
                spans = {}
                active_piles, active_users = set(), set()
                for row in g.orders:
                    for field in ("started_at", "ended_at", "settled_at"):
                        if row[field]:
                            self.assertGreaterEqual(parse_iso(row[field]), g.t0)
                            self.assertLess(parse_iso(row[field]), g.t1)
                    if row["started_at"]:
                        start = parse_iso(row["started_at"])
                        end = parse_iso(row["ended_at"]) if row["ended_at"] else g.t1
                        self.assertLess(start, end)
                        spans.setdefault(row["pile_id"], []).append((start, end))
                        for gap in g.collection_gaps:
                            station = g.piles[row["pile_id"] - 1]["station_id"]
                            if station == gap["station_id"]:
                                self.assertFalse(start < parse_iso(gap["gap_end_exclusive"])
                                                 and end > parse_iso(gap["gap_start"]))
                    if row["status"] in ("charging", "pending_settlement", "reserved",
                                         "pending_reservation"):
                        self.assertNotIn(row["pile_id"], active_piles)
                        self.assertNotIn(row["user_id"], active_users)
                        active_piles.add(row["pile_id"])
                        active_users.add(row["user_id"])
                for intervals in spans.values():
                    intervals.sort()
                    for previous, following in zip(intervals, intervals[1:]):
                        self.assertLessEqual(previous[1], following[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
