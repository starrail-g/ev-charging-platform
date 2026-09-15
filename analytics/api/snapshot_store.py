#!/usr/bin/env python3
"""已发布快照的只读访问（不启动 Spark，只读小 JSON）。

目录约定（docs/architecture/analytics.md §5）:
  <root>/published/latest.json            {"batch_id": ..., "published_at": ..., "path": ...}
  <root>/published/<batch>/dashboard.json 大屏全量投影
  <root>/published/<batch>/quality.json   质量报告
  <root>/published/<batch>/manifest.json  批次清单
"""
from __future__ import annotations

import json
from pathlib import Path


class SnapshotMissing(Exception):
    """没有成功发布过任何快照（或指定批次不存在）。"""


class SnapshotCorrupt(Exception):
    """快照文件缺失/损坏/结构不合法。"""


REQUIRED_DASH_KEYS = ("status", "meta", "data")
REQUIRED_META_KEYS = ("batch_id", "source_type", "timezone", "data_start", "data_end_exclusive")


class SnapshotStore:
    def __init__(self, root):
        self.root = Path(root)

    # ---------------------------------------------------------------- 读取
    def latest(self):
        latest_path = self.root / "published" / "latest.json"
        if not latest_path.exists():
            raise SnapshotMissing("no published snapshot")
        try:
            latest = json.loads(latest_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise SnapshotCorrupt(f"latest.json unreadable: {exc}") from exc
        batch_id = latest.get("batch_id")
        if not batch_id:
            raise SnapshotCorrupt("latest.json missing batch_id")
        return self.batch(batch_id)

    def batch(self, batch_id: str):
        pub = self.root / "published" / batch_id
        dash = self._load_json(pub / "dashboard.json")
        if dash is None:
            raise SnapshotMissing(f"batch {batch_id} not published")
        self._validate_dashboard(dash, batch_id)
        quality = self._load_json(pub / "quality.json") or {}
        return batch_id, dash, quality

    def quality(self, batch_id: str | None):
        if batch_id is None:
            batch_id, _, quality = self.latest()
            return batch_id, quality
        pub = self.root / "published" / batch_id
        quality = self._load_json(pub / "quality.json")
        if quality is None:
            raise SnapshotMissing(f"quality for batch {batch_id} not published")
        return batch_id, quality

    # ---------------------------------------------------------------- 内部
    @staticmethod
    def _load_json(path: Path):
        if not path.exists():
            return None
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise SnapshotCorrupt(f"{path.name} unreadable: {exc}") from exc

    @staticmethod
    def _validate_dashboard(dash, expected_batch):
        for key in REQUIRED_DASH_KEYS:
            if key not in dash:
                raise SnapshotCorrupt(f"dashboard.json missing '{key}'")
        meta = dash.get("meta") or {}
        for key in REQUIRED_META_KEYS:
            if key not in meta:
                raise SnapshotCorrupt(f"dashboard.json meta missing '{key}'")
        if meta.get("batch_id") != expected_batch:
            raise SnapshotCorrupt(
                f"batch mismatch: meta={meta.get('batch_id')} expected={expected_batch}")
        data = dash.get("data") or {}
        for key in ("overview", "stations", "piles", "revenueDaily", "loadHourly"):
            if key not in data:
                raise SnapshotCorrupt(f"dashboard.json data missing '{key}'")
