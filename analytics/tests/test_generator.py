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
from datetime import datetime, timezone

REPO = pathlib.Path(__file__).resolve().parents[2]
GEN = REPO / "analytics" / "generate_data.py"
RULES = [f"DQ{i:02d}" for i in range(1, 12)]


def load_csv(path):
    with open(path, encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def parse_iso(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


class GeneratorCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = pathlib.Path(tempfile.mkdtemp(prefix="gen-test-"))
        for i in (1, 2):
            out = cls.tmp / f"gen{i}"
            subprocess.run(
                [sys.executable, str(GEN), "--profile", "smoke", "--seed", "20260914",
                 "--batch-id", "gen-test", "--out-root", str(out), "--repo-root", str(REPO)],
                check=True, capture_output=True, text=True)
        cls.a = cls.tmp / "gen1" / "batches" / "gen-test"
        cls.b = cls.tmp / "gen2" / "batches" / "gen-test"
        cls.orders = load_csv(cls.a / "input" / "charging_orders.csv")
        cls.wallet = load_csv(cls.a / "input" / "wallet_transactions.csv")
        cls.users = load_csv(cls.a / "input" / "users.csv")
        cls.piles = load_csv(cls.a / "input" / "charging_piles.csv")
        cls.labels = [json.loads(line) for line in
                      open(cls.a / "input" / "dirty_labels.jsonl", encoding="utf-8")]
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
