#!/usr/bin/env python3
"""发布覆盖窗口纯函数测试（无 Spark/Flask 依赖；PR #24 评审 P2 跨边界口径）。"""
from __future__ import annotations

import pathlib
import sys
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

from analytics.publish_coverage import coverage_end_exclusive  # noqa: E402


class TestCoverageEnd(unittest.TestCase):
    def test_settlement_spill_extends_published_coverage(self):
        """末端结算追加日：覆盖末端 = 最大数据日 + 1（右开），而非生成窗口末端。"""
        self.assertEqual(
            coverage_end_exclusive("2026-09-14T00:00:00Z",
                                   ["2026-09-12", "2026-09-13", "2026-09-14T00:00:00Z"]),
            "2026-09-15")

    def test_hour_rows_count_towards_their_own_day(self):
        self.assertEqual(
            coverage_end_exclusive("2026-09-14T00:00:00Z", ["2026-09-14T23:00:00Z"]),
            "2026-09-15")

    def test_no_spill_keeps_generation_window(self):
        self.assertEqual(
            coverage_end_exclusive("2026-09-14T00:00:00Z", ["2026-09-10", None, ""]),
            "2026-09-14")

    def test_empty_data_keeps_generation_window(self):
        self.assertEqual(coverage_end_exclusive("2026-09-14", []), "2026-09-14")


if __name__ == "__main__":
    unittest.main(verbosity=2)
