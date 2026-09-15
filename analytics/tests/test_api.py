#!/usr/bin/env python3
"""Flask API 契约测试（需 flask；不依赖 Spark、不起服务器）。

覆盖: 合法/默认窗口求和与 ADS 一致、站点过滤、空结果、参数错误 400、
覆盖错误 400（附 available_range）、无快照 503、坏快照 503、健康检查、
quality 路由、latest 切换后新 batch。
"""
from __future__ import annotations

import json
import pathlib
import shutil
import sys
import tempfile
import unittest
from datetime import datetime, timedelta

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

from analytics.api.app import create_app  # noqa: E402

BATCH = "t-batch-1"


def _load_grid() -> list[dict]:
    """48 小时全网格（09-01..09-03）; 09-01 前 3 小时有数据, 其余为零行。
    与生产口径一致: 利用率分母来自完整网格（capacity=桩数×3600）。"""
    grid = []
    base = datetime(2026, 9, 1)
    data_hours = {0: (5000.0, 5.0, 1800), 1: (5000.0, 5.0, 1800), 2: (5001.0, 5.001, 1800)}
    for i in range(48):
        hour_start = (base + timedelta(hours=i)).strftime("%Y-%m-%dT%H:%M:%SZ")
        aw, lk, cs = data_hours.get(i, (0.0, 0.0, 0))
        grid.append({"hourStart": hour_start, "stationId": 1, "allocatedWh": aw,
                     "loadKw": lk, "chargeSeconds": cs, "capacityPileSeconds": 7200})
    return grid


def dash_payload(batch_id: str) -> dict:
    return {
        "status": "ok",
        "meta": {"batch_id": batch_id, "source_type": "synthetic_warehouse",
                 "timezone": "UTC", "coverage": "complete",
                 "data_start": "2026-09-01T00:00:00Z",
                 "data_end_exclusive": "2026-09-03T00:00:00Z",
                 "generated_at": "2026-09-15T00:00:00Z",
                 "batch_generated_at": "2026-09-15T00:00:00Z",
                 "is_realtime": False, "stale": False},
        "data": {
            "overview": {"revenueCents": 1901, "completedOrders": 2, "energyWh": 15001},
            "stations": [{"id": 1, "name": "测试充电站", "address": "测试路1号",
                          "latitude": 22.5, "longitude": 113.9, "status": "active",
                          "pileCount": 2, "pileCounts": {"idle": 1, "reserved": 0,
                                                         "charging": 0, "fault": 1, "offline": 0}}],
            "piles": [
                {"id": 1, "stationId": 1, "code": "P-1-A", "type": "fast", "powerKw": 100.0,
                 "unitPriceCentsPerKwh": 120, "status": "idle",
                 "totalChargeCount": 1, "totalChargeSeconds": 3600},
                {"id": 2, "stationId": 1, "code": "P-1-B", "type": "fast", "powerKw": 100.0,
                 "unitPriceCentsPerKwh": 120, "status": "fault",
                 "totalChargeCount": 1, "totalChargeSeconds": 1800}],
            "stationUtilization": [{"stationId": 1, "utilization": 0.03125}],
            "stationRank": [{"stationId": 1, "rank": 1, "revenueCents": 1901,
                             "completedOrders": 2, "energyWh": 15001, "utilization": 0.03125}],
            "revenueDaily": [{"date": "2026-09-01", "stationId": 1, "revenueCents": 1901,
                              "completedOrders": 2, "energyWh": 15001}],
            "loadHourly": _load_grid(),
            "quality": {"input": 7, "kept": 3, "duplicate": 1, "quarantine": 3, "repaired": 0},
        },
    }


class ApiCase(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="api-test-"))
        self.app = create_app({"ANALYTICS_ROOT": str(self.tmp)})
        self.client = self.app.test_client()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def publish(self, batch_id: str, dash=None):
        pub = self.tmp / "published" / batch_id
        pub.mkdir(parents=True, exist_ok=True)
        (pub / "dashboard.json").write_text(json.dumps(dash or dash_payload(batch_id)),
                                            encoding="utf-8")
        (pub / "quality.json").write_text(json.dumps({"batch_id": batch_id, "tables": {}}),
                                          encoding="utf-8")
        (self.tmp / "published" / "latest.json").write_text(
            json.dumps({"batch_id": batch_id, "path": f"published/{batch_id}"}), encoding="utf-8")


class TestApiHappy(ApiCase):
    def test_default_window_sums_match_ads(self):
        self.publish(BATCH)
        resp = self.client.get("/api/dashboard")
        self.assertEqual(resp.status_code, 200)
        body = resp.get_json()
        self.assertEqual(body["status"], "ok")
        self.assertEqual(body["meta"]["batch_id"], BATCH)
        self.assertEqual(body["data"]["overview"]["revenueCents"], 1901)
        self.assertEqual(body["data"]["overview"]["energyWh"], 15001)
        self.assertEqual(body["query"]["start"], "2026-09-01")
        self.assertEqual(body["query"]["end"], "2026-09-03")

    def test_explicit_window_and_station(self):
        self.publish(BATCH)
        resp = self.client.get("/api/dashboard?start=2026-09-01&end=2026-09-02&station_id=1")
        body = resp.get_json()
        self.assertEqual(body["status"], "ok")
        self.assertEqual(body["data"]["overview"]["revenueCents"], 1901)
        self.assertEqual(len(body["data"]["stations"]), 1)
        self.assertEqual(len(body["data"]["revenueDaily"]), 1)
        self.assertEqual(body["data"]["stationUtilization"][0]["utilization"], 0.03125)

    def test_zero_window_is_empty(self):
        self.publish(BATCH)
        resp = self.client.get("/api/dashboard?start=2026-09-02&end=2026-09-03")
        self.assertEqual(resp.status_code, 200)
        body = resp.get_json()
        self.assertEqual(body["status"], "empty")
        self.assertEqual(body["data"]["overview"]["revenueCents"], 0)

    def test_unknown_station_is_empty(self):
        self.publish(BATCH)
        resp = self.client.get("/api/dashboard?station_id=999")
        body = resp.get_json()
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(body["status"], "empty")
        self.assertEqual(body["data"]["stations"], [])

    def test_health(self):
        self.publish(BATCH)
        body = self.client.get("/api/health").get_json()
        self.assertEqual(body["active_batch"], BATCH)
        self.assertTrue(body["snapshot_ready"])
        self.assertFalse(body["is_realtime"])

    def test_quality_route(self):
        self.publish(BATCH)
        body = self.client.get("/api/quality").get_json()
        self.assertEqual(body["meta"]["batch_id"], BATCH)
        self.assertIn("tables", body["data"])
        body2 = self.client.get(f"/api/quality?batch_id={BATCH}").get_json()
        self.assertEqual(body2["status"], "ok")

    def test_batch_switch(self):
        self.publish(BATCH)
        self.publish("t-batch-2")
        body = self.client.get("/api/dashboard").get_json()
        self.assertEqual(body["meta"]["batch_id"], "t-batch-2")


class TestApiErrors(ApiCase):
    def test_invalid_date(self):
        self.publish(BATCH)
        resp = self.client.get("/api/dashboard?start=2026-9-1&end=2026-09-03")
        self.assertEqual(resp.status_code, 400)
        self.assertEqual(resp.get_json()["error"]["code"], "invalid_date")

    def test_invalid_window(self):
        self.publish(BATCH)
        resp = self.client.get("/api/dashboard?start=2026-09-02&end=2026-09-02")
        self.assertEqual(resp.status_code, 400)
        self.assertEqual(resp.get_json()["error"]["code"], "invalid_window")

    def test_window_too_large(self):
        self.publish(BATCH)
        resp = self.client.get("/api/dashboard?start=2026-05-01&end=2026-10-01")
        self.assertEqual(resp.status_code, 400)
        self.assertEqual(resp.get_json()["error"]["code"], "window_too_large")

    def test_invalid_station_id(self):
        self.publish(BATCH)
        resp = self.client.get("/api/dashboard?station_id=abc")
        self.assertEqual(resp.status_code, 400)
        self.assertEqual(resp.get_json()["error"]["code"], "invalid_station_id")
        resp = self.client.get("/api/dashboard?station_id=-1")
        self.assertEqual(resp.status_code, 400)

    def test_out_of_coverage(self):
        self.publish(BATCH)
        resp = self.client.get("/api/dashboard?start=2026-08-01&end=2026-09-03")
        self.assertEqual(resp.status_code, 400)
        err = resp.get_json()["error"]
        self.assertEqual(err["code"], "out_of_coverage")
        self.assertEqual(err["available_range"],
                         {"start": "2026-09-01", "end": "2026-09-03"})

    def test_no_snapshot_503(self):
        resp = self.client.get("/api/dashboard")
        self.assertEqual(resp.status_code, 503)
        self.assertEqual(resp.get_json()["error"]["code"], "snapshot_missing")
        health = self.client.get("/api/health").get_json()
        self.assertFalse(health["snapshot_ready"])
        self.assertIsNone(health["active_batch"])

    def test_corrupt_snapshot_503(self):
        bad = dash_payload(BATCH)
        del bad["data"]["overview"]
        self.publish(BATCH, dash=bad)
        resp = self.client.get("/api/dashboard")
        self.assertEqual(resp.status_code, 503)
        self.assertEqual(resp.get_json()["error"]["code"], "snapshot_corrupt")

    def test_batch_mismatch_503(self):
        bad = dash_payload("other-batch")
        self.publish(BATCH, dash=bad)
        resp = self.client.get("/api/dashboard")
        self.assertEqual(resp.status_code, 503)
        self.assertEqual(resp.get_json()["error"]["code"], "snapshot_corrupt")

    def test_quality_missing_503(self):
        resp = self.client.get("/api/quality?batch_id=nope")
        self.assertEqual(resp.status_code, 503)


if __name__ == "__main__":
    unittest.main(verbosity=2)
