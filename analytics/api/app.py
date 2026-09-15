#!/usr/bin/env python3
"""Flask 只读快照 API + 静态大屏（第二阶段）。

启动（VM）:
  python3 -m flask --app 'analytics.api.app:create_app()' run --host 127.0.0.1 --port 61469

路由:
  GET /api/health                                     进程可用性（不起 Spark）
  GET /api/dashboard?start=YYYY-MM-DD&end=YYYY-MM-DD&station_id=<int>
  GET /api/quality?batch_id=<id>
  GET /runtime-config.js                              兼容原 serve.py（不搬 WebService 凭据）
  GET /                                               大屏静态资产（dashboard/）

语义（docs/api/analytics.md）: 400 参数/覆盖错误（结构化）; 200+empty 无业务记录;
503 无快照或快照损坏; 任一响应只含单一 batch; 不回退 demo 数据伪装成功。
"""
from __future__ import annotations

import json
import os
import re
from datetime import datetime, timedelta, timezone
from pathlib import Path

from flask import Flask, Response, jsonify, request, send_from_directory

try:  # 作为包导入（python3 -m flask / 测试）
    from .snapshot_store import SnapshotCorrupt, SnapshotMissing, SnapshotStore
except ImportError:  # pragma: no cover - 脚本式直跑
    from snapshot_store import SnapshotCorrupt, SnapshotMissing, SnapshotStore

UTC = timezone.utc
MAX_WINDOW_DAYS = 90


def _err(code: str, message: str, http_status: int, available_range=None):
    err = {"code": code, "message": message}
    if available_range is not None:
        err["available_range"] = available_range
    return jsonify({"status": "error", "error": err}), http_status


DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def _parse_date(value: str | None):
    if value is None or not DATE_RE.match(value):
        return None
    try:
        return datetime.strptime(value, "%Y-%m-%d").date()
    except (TypeError, ValueError):
        return None


def _is_stale(meta: dict, stale_hours: int) -> bool:
    stamp = meta.get("batch_generated_at") or meta.get("generated_at")
    if not stamp:
        return False
    try:
        ts = datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)
    except (TypeError, ValueError):
        return False
    return (datetime.now(UTC) - ts) > timedelta(hours=stale_hours)


def _filter_data(data: dict, start_s: str, end_s: str, station_id):
    def in_win(value: str) -> bool:
        return start_s <= value[:10] < end_s

    rev = [r for r in data.get("revenueDaily", [])
           if in_win(r["date"]) and (station_id is None or r["stationId"] == station_id)]
    load = [r for r in data.get("loadHourly", [])
            if in_win(r["hourStart"]) and (station_id is None or r["stationId"] == station_id)]
    stations = [s for s in data.get("stations", [])
                if station_id is None or s["id"] == station_id]
    piles = [p for p in data.get("piles", [])
             if station_id is None or p["stationId"] == station_id]

    revenue = sum(r["revenueCents"] for r in rev)
    completed = sum(r["completedOrders"] for r in rev)
    energy = sum(r["energyWh"] for r in rev)

    util_acc: dict[int, list[int]] = {}
    for r in load:
        acc = util_acc.setdefault(r["stationId"], [0, 0])
        acc[0] += r["chargeSeconds"]
        acc[1] += r["capacityPileSeconds"]
    station_util = [{"stationId": sid,
                     "utilization": round(acc[0] / acc[1], 6) if acc[1] else 0.0}
                    for sid, acc in sorted(util_acc.items())]

    by_day: dict[str, dict] = {}
    for r in rev:
        d = by_day.setdefault(r["date"], {"revenueCents": 0, "completedOrders": 0, "energyWh": 0})
        d["revenueCents"] += r["revenueCents"]
        d["completedOrders"] += r["completedOrders"]
        d["energyWh"] += r["energyWh"]
    revenue_daily = [{"date": d, **by_day[d]} for d in sorted(by_day)]

    by_hour: dict[str, dict] = {}
    for r in load:
        h = by_hour.setdefault(r["hourStart"], {"loadKw": 0.0, "allocatedWh": 0.0, "chargeSeconds": 0})
        h["loadKw"] += r["loadKw"]
        h["allocatedWh"] += r["allocatedWh"]
        h["chargeSeconds"] += r["chargeSeconds"]
    load_hourly = [{"hourStart": h,
                    "loadKw": round(by_hour[h]["loadKw"], 6),
                    "allocatedWh": round(by_hour[h]["allocatedWh"], 6),
                    "chargeSeconds": by_hour[h]["chargeSeconds"]}
                   for h in sorted(by_hour)]

    rev_by_st: dict[int, dict] = {}
    for r in rev:
        acc = rev_by_st.setdefault(r["stationId"], {"revenueCents": 0, "completedOrders": 0, "energyWh": 0})
        acc["revenueCents"] += r["revenueCents"]
        acc["completedOrders"] += r["completedOrders"]
        acc["energyWh"] += r["energyWh"]
    util_map = {s["stationId"]: s["utilization"] for s in station_util}
    rank_sorted = sorted(rev_by_st.items(),
                         key=lambda kv: (-kv[1]["revenueCents"], kv[0]))
    station_rank = [{"stationId": sid, "rank": i + 1, **v,
                     "utilization": util_map.get(sid, 0.0)}
                    for i, (sid, v) in enumerate(rank_sorted)]

    matched = bool(stations)
    has_business = (revenue or completed or energy
                    or any(s["chargeSeconds"] for s in by_hour.values()))
    status = "ok" if (matched and has_business) else "empty"

    avg_util = (sum(s["utilization"] for s in station_util) / len(station_util)) \
        if station_util else 0.0
    filtered = {
        "overview": {
            "revenueCents": revenue,
            "completedOrders": completed,
            "energyWh": energy,
            "energyKwh": round(energy / 1000.0, 3),
            "avgStationUtilization": round(avg_util, 6),
        },
        "stations": stations,
        "piles": piles,
        "stationUtilization": station_util,
        "stationRank": station_rank,
        "revenueDaily": revenue_daily,
        "loadHourly": load_hourly,
        "quality": data.get("quality", {}),
    }
    return status, filtered


def create_app(config: dict | None = None):
    cfg = dict(config or {})
    root = cfg.get("ANALYTICS_ROOT") or os.environ.get("EV_ANALYTICS_ROOT") \
        or str(Path.home() / "ev-stage2-artifacts")
    dash_dir = cfg.get("DASHBOARD_DIR") or os.environ.get("EV_DASHBOARD_DIR") \
        or str(Path(__file__).resolve().parents[2] / "dashboard")
    stale_hours = int(cfg.get("STALE_HOURS")
                      or os.environ.get("EV_SNAPSHOT_STALE_HOURS") or 72)
    store = SnapshotStore(root)

    app = Flask(__name__, static_folder=None)

    # ------------------------------------------------------------------ API
    @app.get("/api/health")
    def health():
        try:
            batch_id, _, _ = store.latest()
            return jsonify({"status": "ok", "active_batch": batch_id,
                            "snapshot_ready": True, "is_realtime": False})
        except (SnapshotMissing, SnapshotCorrupt):
            return jsonify({"status": "ok", "active_batch": None,
                            "snapshot_ready": False, "is_realtime": False})

    @app.get("/api/dashboard")
    def api_dashboard():
        try:
            batch_id, dash, _ = store.latest()
        except SnapshotMissing as exc:
            return _err("snapshot_missing", str(exc), 503)
        except SnapshotCorrupt as exc:
            return _err("snapshot_corrupt", str(exc), 503)

        meta = dash["meta"]
        avail_start = meta["data_start"][:10]
        avail_end = meta["data_end_exclusive"][:10]

        start_s = request.args.get("start") or avail_start
        end_s = request.args.get("end") or avail_end
        start_d = _parse_date(start_s)
        end_d = _parse_date(end_s)
        if start_d is None or end_d is None:
            return _err("invalid_date", "start/end must be YYYY-MM-DD", 400,
                        {"start": avail_start, "end": avail_end})
        if start_d >= end_d:
            return _err("invalid_window", "start must be before end", 400,
                        {"start": avail_start, "end": avail_end})
        if (end_d - start_d).days > MAX_WINDOW_DAYS:
            return _err("window_too_large", f"window must be <= {MAX_WINDOW_DAYS} days", 400,
                        {"start": avail_start, "end": avail_end})

        station_raw = request.args.get("station_id")
        station_id = None
        if station_raw not in (None, ""):
            try:
                station_id = int(station_raw)
            except (TypeError, ValueError):
                return _err("invalid_station_id", "station_id must be a positive integer", 400)
            if station_id <= 0:
                return _err("invalid_station_id", "station_id must be a positive integer", 400)

        if start_s < avail_start or end_s > avail_end:
            return _err("out_of_coverage",
                        "requested window is outside batch coverage", 400,
                        {"start": avail_start, "end": avail_end})

        status, data = _filter_data(dash["data"], start_s, end_s, station_id)
        meta = dict(meta)
        meta["stale"] = _is_stale(meta, stale_hours)
        return jsonify({
            "status": status,
            "meta": meta,
            "query": {"start": start_s, "end": end_s, "station_id": station_id},
            "data": data,
        })

    @app.get("/api/quality")
    def api_quality():
        batch_raw = request.args.get("batch_id") or None
        try:
            batch_id, quality = store.quality(batch_raw)
        except SnapshotMissing as exc:
            return _err("snapshot_missing", str(exc), 503)
        except SnapshotCorrupt as exc:
            return _err("snapshot_corrupt", str(exc), 503)
        return jsonify({"status": "ok", "meta": {"batch_id": batch_id}, "data": quality})

    # ------------------------------------------------------------------ 静态与运行时配置
    @app.get("/runtime-config.js")
    def runtime_config():
        # 兼容原 serve.py: window.__EV_CONFIG__；不搬 WebService 凭据，地图默认拓扑（无 key）
        cfg_js = json.dumps({"tencentMapKey": "", "demo": False, "source": "analytics"},
                            ensure_ascii=False)
        return Response(f"window.__EV_CONFIG__ = {cfg_js};\n",
                        mimetype="application/javascript")

    @app.get("/")
    def index():
        return send_from_directory(dash_dir, "index.html")

    @app.get("/<path:filename>")
    def static_files(filename: str):
        if filename.startswith("api/"):
            return _err("not_found", f"unknown api route: {filename}", 404)
        return send_from_directory(dash_dir, filename)

    return app


if __name__ == "__main__":  # pragma: no cover - 仅本地调试
    create_app().run(host="127.0.0.1", port=int(os.environ.get("EV_API_PORT", "61469")))
