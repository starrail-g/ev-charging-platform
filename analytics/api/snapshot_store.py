#!/usr/bin/env python3
"""已发布快照的只读访问（不启动 Spark，只读小 JSON）。

目录约定（docs/architecture/analytics.md §5）:
  <root>/published/latest.json            {"batch_id": ..., "published_at": ..., "path": ...}
  <root>/published/<batch>/dashboard.json 大屏全量投影
  <root>/published/<batch>/quality.json   质量报告
  <root>/published/<batch>/manifest.json  批次清单

安全（PR #24 评审 P2）：
- batch_id 只允许 [A-Za-z0-9][A-Za-z0-9._-]{0,127}，且解析后必须仍在 <root>/published 内——
  拒绝绝对路径/路径分隔符/..——越界参数抛 SnapshotInvalid（API 层映射为结构化 400）；
- dashboard.json 做容器与记录字段类型校验：结构坏 → SnapshotCorrupt（503 snapshot_corrupt），
  不允许在过滤阶段抛异常变成 HTML 500。
"""
from __future__ import annotations

import json
import re
from datetime import datetime
from pathlib import Path


class SnapshotMissing(Exception):
    """没有成功发布过任何快照（或指定批次不存在）。"""


class SnapshotCorrupt(Exception):
    """快照文件缺失/损坏/结构不合法。"""


class SnapshotInvalid(Exception):
    """请求参数不合法（如越界 batch_id）——API 层应返回结构化 400，而非 500。"""


REQUIRED_DASH_KEYS = ("status", "meta", "data")
REQUIRED_META_KEYS = ("batch_id", "source_type", "timezone", "data_start", "data_end_exclusive")

# 发布批次目录名白名单：不允许分隔符、不允许以点开头、不允许 ".." 子串。
BATCH_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")

# 大屏记录的最小字段与类型（API 过滤/前端适配都会读到的字段）。
DASH_RECORD_SPECS = {
    "revenueDaily": {"date": "str", "stationId": "int", "revenueCents": "int",
                     "completedOrders": "int", "energyWh": "int"},
    "loadHourly": {"hourStart": "str", "stationId": "int", "loadKw": "number",
                   "allocatedWh": "number", "chargeSeconds": "int", "capacityPileSeconds": "int"},
    "stations": {"id": "int", "name": "str", "latitude": "number", "longitude": "number"},
    "piles": {"id": "int", "stationId": "int", "code": "str", "status": "str"},
}


def _type_ok(value: object, kind: str) -> bool:
    if kind == "int":
        return isinstance(value, int) and not isinstance(value, bool)
    if kind == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if kind == "str":
        return isinstance(value, str)
    return False


def _date_prefix_ok(value: object) -> bool:
    """窗口字段的前 10 位必须是可解析日期（API 直接切片解析，解析失败即 500）。"""
    if not isinstance(value, str):
        return False
    try:
        datetime.strptime(value[:10], "%Y-%m-%d")
        return True
    except ValueError:
        return False


def _validate_record(record: object, spec: dict[str, str], where: str) -> None:
    if not isinstance(record, dict):
        raise SnapshotCorrupt(f"dashboard.json {where} must be an object")
    for key, kind in spec.items():
        if key not in record:
            raise SnapshotCorrupt(f"dashboard.json {where} missing '{key}'")
        if not _type_ok(record[key], kind):
            raise SnapshotCorrupt(f"dashboard.json {where}.{key} must be {kind}")


class SnapshotStore:
    def __init__(self, root):
        self.root = Path(root)

    # ---------------------------------------------------------------- 路径安全
    def _published_root(self) -> Path:
        return self.root / "published"

    def _batch_dir(self, batch_id) -> Path:
        """发布批次目录：名字白名单 + 解析后必须仍位于 published 根目录内。"""
        if not isinstance(batch_id, str) or not BATCH_ID_RE.match(batch_id) or ".." in batch_id:
            raise SnapshotInvalid(f"invalid batch_id: {batch_id!r}")
        root = self._published_root().resolve()
        candidate = root / batch_id
        if not candidate.resolve().is_relative_to(root):   # 双保险：符号链接/编码别名也拦
            raise SnapshotInvalid(f"batch_id escapes published root: {batch_id!r}")
        return candidate

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
        try:
            return self.batch(batch_id)
        except SnapshotInvalid as exc:      # 指针内容坏属于服务端数据问题 → 503 语义
            raise SnapshotCorrupt(f"latest.json batch_id invalid: {exc}") from exc

    def batch(self, batch_id: str):
        pub = self._batch_dir(batch_id)
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
        pub = self._batch_dir(batch_id)
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
        if not isinstance(dash, dict):
            raise SnapshotCorrupt("dashboard.json must be an object")
        for key in REQUIRED_DASH_KEYS:
            if key not in dash:
                raise SnapshotCorrupt(f"dashboard.json missing '{key}'")
        meta = dash.get("meta")
        if not isinstance(meta, dict):
            raise SnapshotCorrupt("dashboard.json meta must be an object")
        for key in REQUIRED_META_KEYS:
            if key not in meta:
                raise SnapshotCorrupt(f"dashboard.json meta missing '{key}'")
        if meta.get("batch_id") != expected_batch:
            raise SnapshotCorrupt(
                f"batch mismatch: meta={meta.get('batch_id')} expected={expected_batch}")
        # meta 里的窗口字段直接参与 API 切片（start/end），必须是可切片的非空字符串。
        for key in ("source_type", "timezone", "data_start", "data_end_exclusive"):
            if not isinstance(meta.get(key), str) or not meta[key]:
                raise SnapshotCorrupt(f"dashboard.json meta.{key} must be a non-empty string")
        # 窗口字段还必须是可解析日期前缀（API 会解析它们算默认窗口/覆盖校验）。
        for key in ("data_start", "data_end_exclusive", "available_start", "available_end_exclusive"):
            value = meta.get(key)
            if value is not None and not _date_prefix_ok(value):
                raise SnapshotCorrupt(
                    f"dashboard.json meta.{key} must start with a valid YYYY-MM-DD date")
        data = dash.get("data")
        if not isinstance(data, dict):
            raise SnapshotCorrupt("dashboard.json data must be an object")
        for key in ("overview", "stations", "piles", "revenueDaily", "loadHourly"):
            if key not in data:
                raise SnapshotCorrupt(f"dashboard.json data missing '{key}'")
        # 容器与记录字段类型校验（评审 P2）：结构坏 → 503 snapshot_corrupt，
        # 不允许延迟到过滤阶段抛异常（HTML 500）。
        if not isinstance(data.get("overview"), dict):
            raise SnapshotCorrupt("dashboard.json data.overview must be an object")
        for list_key, spec in DASH_RECORD_SPECS.items():
            rows = data.get(list_key)
            if not isinstance(rows, list):
                raise SnapshotCorrupt(f"dashboard.json data.{list_key} must be a list")
            for index, record in enumerate(rows):
                _validate_record(record, spec, f"data.{list_key}[{index}]")
        quality = data.get("quality")
        if quality is not None and not isinstance(quality, dict):
            raise SnapshotCorrupt("dashboard.json data.quality must be an object")
        facts = data.get("workbenchFacts")
        if facts is not None:
            if (not isinstance(facts, dict) or facts.get("version") not in (1, 2)
                    or not _type_ok(facts.get("totalUsers"), "int") or facts["totalUsers"] < 0):
                raise SnapshotCorrupt("invalid workbenchFacts header")
            specs = {
                "users": {"user_id": "int", "station_id": "int", "frequency": "int",
                          "monetary": "int", "stat_date": "str", "last_settled_at": "str"},
                "orders": {"station_id": "int", "status": "str", "stat_date": "str",
                           "order_count": "int"},
            }
            for key, spec in specs.items():
                rows = facts.get(key)
                if not isinstance(rows, list):
                    raise SnapshotCorrupt(f"workbenchFacts.{key} must be a list")
                for row in rows:
                    _validate_record(row, spec, f"workbenchFacts.{key}")
                    if not _date_prefix_ok(row["stat_date"]):
                        raise SnapshotCorrupt("invalid workbench date")
                    if key == "users":
                        try:
                            datetime.fromisoformat(row["last_settled_at"].replace("Z", "+00:00"))
                        except ValueError as exc:
                            raise SnapshotCorrupt("invalid RFM timestamp") from exc
                        if row["frequency"] < 1 or row["monetary"] < 0:
                            raise SnapshotCorrupt("invalid RFM counts")
                    else:
                        hour = row.get("start_hour")
                        duration = row.get("duration_seconds")
                        if (hour is not None and (not _type_ok(hour, "int") or not 0 <= hour <= 23)
                                or duration is not None and (not _type_ok(duration, "int") or duration < 0)
                                or row["order_count"] < 0):
                            raise SnapshotCorrupt("invalid order aggregates")
                        if facts["version"] == 2:
                            buckets = [row.get(k) for k in ("duration_le15", "duration_15_30",
                                                           "duration_30_60", "duration_gt60")]
                            if (any(not _type_ok(v, "int") or v < 0 for v in buckets)
                                    or sum(buckets) != (row["order_count"] if row["status"] == "completed" else 0)):
                                raise SnapshotCorrupt("invalid duration bucket conservation")
