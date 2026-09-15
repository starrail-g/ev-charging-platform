#!/usr/bin/env python3
"""Export a Dashboard snapshot from the validated Schema v0.4 SQLite copy."""

from __future__ import annotations

import argparse
import json
import sqlite3
from datetime import datetime, timezone
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    connection = sqlite3.connect(args.database)
    connection.row_factory = sqlite3.Row
    stations = [dict(row) for row in connection.execute(
        "SELECT id, name, address, latitude, longitude, status FROM stations ORDER BY id")]
    piles = [dict(row) for row in connection.execute(
        "SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, "
        "status, total_charge_count, total_charge_seconds FROM charging_piles ORDER BY id")]
    revenue = [dict(row) for row in connection.execute(
        "SELECT revenue_date AS date, revenue_cents FROM revenue_daily "
        "ORDER BY revenue_date DESC LIMIT 30")]
    revenue.reverse()
    hourly = [dict(row) for row in connection.execute(
        "SELECT CAST(substr(started_at, 12, 2) AS INTEGER) AS hour, "
        "COALESCE(SUM(energy_wh), 0) / 1000.0 AS load_kwh, "
        "COUNT(*) AS order_count, COALESCE(SUM(total_amount_cents), 0) AS revenue_cents "
        "FROM charging_orders WHERE status='completed' AND started_at IS NOT NULL "
        "GROUP BY hour ORDER BY hour")]
    utilization = [dict(row) for row in connection.execute(
        "SELECT s.id AS station_id, s.name, "
        "COALESCE(SUM(CASE WHEN o.status='completed' THEN o.energy_wh ELSE 0 END), 0) AS energy_wh, "
        "COUNT(DISTINCT p.id) AS device_count, COUNT(DISTINCT o.id) AS order_count "
        "FROM stations s LEFT JOIN charging_piles p ON p.station_id=s.id "
        "LEFT JOIN charging_orders o ON o.pile_id=p.id AND o.status='completed' "
        "GROUP BY s.id, s.name ORDER BY s.id")]

    for pile in piles:
        pile.update(code=pile.pop("pile_code"), type=pile.pop("pile_type"), powerKw=pile.pop("power_kw"),
                    unitPriceCentsPerKwh=pile.pop("unit_price_cents_per_kwh"), totalChargeCount=pile.pop("total_charge_count"),
                    totalChargeSeconds=pile.pop("total_charge_seconds"), stationId=pile.pop("station_id"))
    for station in stations:
        station["code"] = f"S{station['id']}"
        station["pileCount"] = sum(1 for pile in piles if pile["stationId"] == station["id"])
    for row in utilization:
        row["utilization"] = min(1.0, row.pop("order_count") / max(1, row.pop("device_count") * 90))
    values = [int(row["revenue_cents"]) for row in revenue]

    users_summary = {
        "total": int(connection.execute("SELECT COUNT(*) FROM users").fetchone()[0]),
        "active": int(connection.execute("SELECT COUNT(*) FROM users WHERE status='active'").fetchone()[0]),
        "frozen": int(connection.execute("SELECT COUNT(*) FROM users WHERE status='frozen'").fetchone()[0]),
    }
    user_orders = [dict(row) for row in connection.execute(
        "SELECT u.id, COUNT(o.id) AS order_count FROM users u "
        "LEFT JOIN charging_orders o ON o.user_id=u.id AND o.status='completed' "
        "GROUP BY u.id")]
    buckets = {"0 单": 0, "1 单": 0, "2–4 单": 0, "5 单及以上": 0}
    for row in user_orders:
        count = int(row["order_count"])
        if count == 0:
            buckets["0 单"] += 1
        elif count == 1:
            buckets["1 单"] += 1
        elif count < 5:
            buckets["2–4 单"] += 1
        else:
            buckets["5 单及以上"] += 1
    users_summary["repeat_users"] = sum(value for label, value in buckets.items() if label in ("2–4 单", "5 单及以上"))
    users_summary["segments"] = [{"label": label, "count": count} for label, count in buckets.items()]
    # RFM-style customer mining: frequency + monetary value + recency. These
    # are derived from completed orders and expose an interpretable segment,
    # not a cosmetic aggregation.
    user_rfm = [dict(row) for row in connection.execute(
        "SELECT u.id AS user_id, COUNT(o.id) AS frequency, "
        "COALESCE(SUM(o.total_amount_cents), 0) AS monetary, "
        "CAST(julianday((SELECT MAX(settled_at) FROM charging_orders)) - "
        "julianday(MAX(o.settled_at)) AS INTEGER) AS recency_days "
        "FROM users u LEFT JOIN charging_orders o ON o.user_id=u.id AND o.status='completed' GROUP BY u.id")]
    for row in user_rfm:
        # Zero days is a valid, high-value RFM recency.  Only users without a
        # completed order should fall back to the explicit 999-day sentinel.
        recency = row["recency_days"]
        row["frequency"], row["monetary"], row["recency_days"] = (
            int(row["frequency"]),
            int(row["monetary"]),
            int(recency) if recency is not None else 999,
        )
        row["segment"] = "高价值" if row["frequency"] >= 8 and row["monetary"] >= 100000 else "成长" if row["frequency"] >= 3 else "低频"

    order_counts = {row["status"]: int(row["count"]) for row in connection.execute(
        "SELECT status, COUNT(*) AS count FROM charging_orders GROUP BY status")}
    total_orders = sum(order_counts.values())
    completed_orders = order_counts.get("completed", 0)
    cancelled_orders = order_counts.get("cancelled", 0)
    daily_orders = [dict(row) for row in connection.execute(
        "SELECT substr(started_at, 1, 10) AS date, COUNT(*) AS total, "
        "SUM(CASE WHEN status='completed' THEN 1 ELSE 0 END) AS completed, "
        "SUM(CASE WHEN status='cancelled' THEN 1 ELSE 0 END) AS cancelled, "
        "COALESCE(SUM(CASE WHEN status='completed' THEN total_amount_cents ELSE 0 END), 0) AS revenue_cents, "
        "COALESCE(SUM(CASE WHEN status='completed' THEN energy_wh ELSE 0 END), 0) / 1000.0 AS energy_kwh "
        "FROM charging_orders WHERE started_at IS NOT NULL GROUP BY date ORDER BY date DESC LIMIT 30")]
    daily_orders.reverse()
    hourly_order_status = [dict(row) for row in connection.execute(
        "SELECT CAST(substr(started_at, 12, 2) AS INTEGER) AS hour, "
        "COUNT(*) AS total, "
        "SUM(CASE WHEN status='completed' THEN 1 ELSE 0 END) AS completed, "
        "SUM(CASE WHEN status='cancelled' THEN 1 ELSE 0 END) AS cancelled "
        "FROM charging_orders WHERE started_at IS NOT NULL "
        "GROUP BY hour ORDER BY hour")]
    duration_buckets = {"≤15 min": 0, "16–30 min": 0, "31–60 min": 0, ">60 min": 0}
    duration_values = [float(row[0]) for row in connection.execute(
        "SELECT (julianday(ended_at)-julianday(started_at))*24*60 "
        "FROM charging_orders WHERE status='completed' "
        "AND started_at IS NOT NULL AND ended_at IS NOT NULL") if row[0] is not None]
    for minutes in duration_values:
        bucket = "≤15 min" if minutes <= 15 else "16–30 min" if minutes <= 30 else "31–60 min" if minutes <= 60 else ">60 min"
        duration_buckets[bucket] += 1
    duration_row = connection.execute(
        "SELECT AVG((julianday(ended_at)-julianday(started_at))*24*60) "
        "FROM charging_orders WHERE status='completed' AND started_at IS NOT NULL AND ended_at IS NOT NULL").fetchone()
    avg_duration = round(float(duration_row[0] or 0.0), 2)

    type_counts = {row["pile_type"]: int(row["count"]) for row in connection.execute(
        "SELECT pile_type, COUNT(*) AS count FROM charging_piles GROUP BY pile_type")}
    status_counts = {row["status"]: int(row["count"]) for row in connection.execute(
        "SELECT status, COUNT(*) AS count FROM charging_piles GROUP BY status")}
    power_bands = {"≤30 kW": 0, "31–120 kW": 0, ">120 kW": 0}
    for row in connection.execute("SELECT power_kw FROM charging_piles"):
        power = float(row[0])
        power_bands["≤30 kW" if power <= 30 else "31–120 kW" if power <= 120 else ">120 kW"] += 1
    restart_count = int(connection.execute("SELECT COALESCE(SUM(restart_count), 0) FROM charging_piles").fetchone()[0])

    energy_total = float(connection.execute(
        "SELECT COALESCE(SUM(energy_wh), 0) FROM charging_orders WHERE status='completed'").fetchone()[0] or 0) / 1000.0
    peak_hour = max(hourly, key=lambda row: float(row["load_kwh"]), default={"hour": None}).get("hour")

    station_rows = [dict(row) for row in connection.execute(
        "SELECT s.id AS station_id, s.name, s.status, COUNT(DISTINCT p.id) AS device_count, "
        "COUNT(DISTINCT CASE WHEN o.status='completed' THEN o.id END) AS order_count, "
        "COALESCE(SUM(CASE WHEN o.status='completed' THEN o.energy_wh ELSE 0 END), 0) / 1000.0 AS energy_kwh, "
        "COALESCE(SUM(CASE WHEN o.status='completed' THEN o.total_amount_cents ELSE 0 END), 0) AS revenue_cents "
        "FROM stations s LEFT JOIN charging_piles p ON p.station_id=s.id "
        "LEFT JOIN charging_orders o ON o.pile_id=p.id "
        "GROUP BY s.id, s.name, s.status ORDER BY revenue_cents DESC")]
    for row in station_rows:
        row["utilization"] = round(min(1.0, int(row.pop("order_count")) / max(1, int(row["device_count"]) * 90)), 4)
        row["device_count"] = int(row["device_count"])
        row["revenue_cents"] = int(row["revenue_cents"])
        row["energy_kwh"] = round(float(row["energy_kwh"]), 3)

    # Deterministic, explainable outlier mining for pile operations.
    pile_activity = [dict(row) for row in connection.execute(
        "SELECT p.id AS pile_id, p.pile_code, p.station_id, p.total_charge_count, "
        "COALESCE(SUM(CASE WHEN o.status='completed' THEN o.energy_wh ELSE 0 END),0) AS energy_wh "
        "FROM charging_piles p LEFT JOIN charging_orders o ON o.pile_id=p.id GROUP BY p.id")]
    activity = [float(row["total_charge_count"] or 0) for row in pile_activity]
    mean_activity = sum(activity) / max(1, len(activity))
    deviation = (sum((value - mean_activity) ** 2 for value in activity) / max(1, len(activity))) ** 0.5
    for row in pile_activity:
        z = (float(row["total_charge_count"] or 0) - mean_activity) / deviation if deviation else 0.0
        row["z_score"] = round(z, 3)
        row["anomaly"] = abs(z) >= 2.0
    station_sorted = sorted(station_rows, key=lambda row: (row["utilization"], row["energy_kwh"]))
    for index, row in enumerate(station_sorted):
        row["cluster"] = "高负荷" if row["utilization"] >= 0.65 else "均衡" if row["utilization"] >= 0.35 else "低负荷"

    # Explainable station cluster centroids and Pareto concentration are
    # calculated from the same station facts that drive the ranking chart.
    cluster_centroids = []
    for label in ["高负荷", "均衡", "低负荷"]:
        members = [row for row in station_rows if row["cluster"] == label]
        cluster_centroids.append({
            "label": label,
            "count": len(members),
            "avg_utilization": round(sum(row["utilization"] for row in members) / max(1, len(members)), 4),
            "avg_energy_kwh": round(sum(row["energy_kwh"] for row in members) / max(1, len(members)), 3),
            "avg_revenue_cents": int(sum(row["revenue_cents"] for row in members) / max(1, len(members))),
        })
    total_station_revenue = sum(row["revenue_cents"] for row in station_rows)
    running_revenue = 0
    pareto_stations = []
    for row in station_rows:
        running_revenue += row["revenue_cents"]
        pareto_stations.append({
            "station_id": row["station_id"], "name": row["name"],
            "revenue_cents": row["revenue_cents"],
            "share": round(row["revenue_cents"] / max(1, total_station_revenue), 4),
            "cumulative_share": round(running_revenue / max(1, total_station_revenue), 4),
        })

    # A small ordinary-least-squares trend is more useful than a decorative
    # arrow: it exposes the daily revenue slope and fit quality used by the
    # business page. The result is descriptive, not a future commitment.
    revenue_values = [float(value) for value in values]
    n_revenue = len(revenue_values)
    x_mean = (n_revenue - 1) / 2 if n_revenue else 0.0
    y_mean = sum(revenue_values) / max(1, n_revenue)
    denominator = sum((index - x_mean) ** 2 for index in range(n_revenue))
    trend_slope = sum((index - x_mean) * (value - y_mean) for index, value in enumerate(revenue_values)) / denominator if denominator else 0.0
    ss_total = sum((value - y_mean) ** 2 for value in revenue_values)
    ss_residual = sum((value - (y_mean + trend_slope * (index - x_mean))) ** 2 for index, value in enumerate(revenue_values))
    trend_r2 = 1.0 - ss_residual / ss_total if ss_total else 0.0

    energy_values = [float(row["load_kwh"]) for row in hourly]
    order_values = [float(row["order_count"]) for row in hourly]
    energy_mean = sum(energy_values) / max(1, len(energy_values))
    order_mean = sum(order_values) / max(1, len(order_values))
    covariance = sum((energy - energy_mean) * (orders - order_mean) for energy, orders in zip(energy_values, order_values))
    energy_std = sum((energy - energy_mean) ** 2 for energy in energy_values) ** 0.5
    order_std = sum((orders - order_mean) ** 2 for orders in order_values) ** 0.5
    energy_order_correlation = covariance / (energy_std * order_std) if energy_std and order_std else 0.0
    peak_energy = max(energy_values, default=0.0)
    peak_share = peak_energy / max(0.001, sum(energy_values))

    analytics = {
        "version": 1,
        "users": {**users_summary, "frequency_distribution": [{"label": label, "count": count} for label, count in buckets.items()]},
        "user_mining": {"method": "RFM", "segments": [{"segment": label, "count": sum(1 for row in user_rfm if row["segment"] == label)} for label in ["高价值", "成长", "低频"]], "top_users": sorted(user_rfm, key=lambda row: (-row["monetary"], row["recency_days"]))[:10]},
        "equipment": {"status_counts": status_counts, "type_counts": type_counts,
                       "power_bands": [{"label": label, "count": count} for label, count in power_bands.items()],
                       "restart_count": restart_count, "simulated_count": int(connection.execute("SELECT COUNT(*) FROM charging_piles WHERE simulated=1").fetchone()[0])},
        "orders": {"total": total_orders, "completed": completed_orders, "cancelled": cancelled_orders,
                   "active": total_orders - completed_orders - cancelled_orders,
                   "completion_rate": round(completed_orders / max(1, total_orders), 4),
                   "cancel_rate": round(cancelled_orders / max(1, total_orders), 4),
                   "avg_duration_minutes": avg_duration, "duration_buckets": [{"label": label, "count": count} for label, count in duration_buckets.items()],
                   "status_counts": [{"label": label, "count": count} for label, count in sorted(order_counts.items())],
                   "hourly_status": hourly_order_status, "daily": daily_orders},
        "energy": {"total_kwh": round(energy_total, 3),
                   "avg_session_kwh": round(energy_total / max(1, completed_orders), 3),
                   "peak_hour": peak_hour, "peak_share": round(peak_share, 4),
                   "order_energy_correlation": round(energy_order_correlation, 4),
                   "time_bands": [{"label": "夜间 00–06", "energy_kwh": round(sum(float(row["load_kwh"]) for row in hourly if int(row["hour"]) < 6), 3)},
                                  {"label": "早高峰 07–10", "energy_kwh": round(sum(float(row["load_kwh"]) for row in hourly if 7 <= int(row["hour"]) <= 10), 3)},
                                  {"label": "日间 11–16", "energy_kwh": round(sum(float(row["load_kwh"]) for row in hourly if 11 <= int(row["hour"]) <= 16), 3)},
                                  {"label": "晚高峰 17–23", "energy_kwh": round(sum(float(row["load_kwh"]) for row in hourly if int(row["hour"]) >= 17), 3)}],
                   "hourly": [{"hour": int(row["hour"]), "energy_kwh": round(float(row["load_kwh"]), 3),
                               "order_count": int(row["order_count"]), "revenue_cents": int(row["revenue_cents"])} for row in hourly]},
        "revenue": {"total_30d_cents": int(sum(values)), "avg_daily_cents": int(sum(values) / max(1, len(values))),
                    "trend": {"method": "ordinary least squares", "slope_cents_per_day": round(trend_slope, 2), "r2": round(max(0.0, trend_r2), 4)},
                    "daily": [{"date": row["date"], "revenue_cents": int(row["revenue_cents"])} for row in revenue]},
        "stations": station_rows,
        "station_mining": {"method": "rule-based utilization-energy clustering", "clusters": [{"label": label, "count": sum(1 for row in station_rows if row["cluster"] == label)} for label in ["高负荷", "均衡", "低负荷"]], "centroids": cluster_centroids, "pareto": pareto_stations},
        "equipment_mining": {"method": "z-score outlier detection", "threshold": 2.0, "anomaly_count": sum(1 for row in pile_activity if row["anomaly"]), "top_anomalies": sorted(pile_activity, key=lambda row: -abs(row["z_score"]))[:8]},
        "service": {"rating_available": False, "rating_note": "Schema v0.4 未包含评价表；以下为订单完成/取消和复购代理指标",
                     "completion_rate": round(completed_orders / max(1, total_orders), 4),
                     "cancel_rate": round(cancelled_orders / max(1, total_orders), 4),
                     "repeat_user_rate": round(users_summary["repeat_users"] / max(1, users_summary["total"]), 4),
                     "avg_duration_minutes": avg_duration, "duration_buckets": [{"label": label, "count": count} for label, count in duration_buckets.items()],
                     "service_control": {"method": "statistical process control", "baseline_completion_rate": round(completed_orders / max(1, total_orders), 4), "rating_status": "not_available"}},
    }
    snapshot = {
        "demo": False,
        "meta": {"name": "schema-v0.4-analysis-snapshot", "version": 1, "source": "validated SQLite Schema v0.4"},
        "updatedAt": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "overview": {"revenueCents": sum(values[-7:]), "avgStationUtilization": round(sum(r["utilization"] for r in utilization) / max(1, len(utilization)), 4)},
        "stations": stations, "piles": piles, "stationUtilization": utilization,
        "revenue7dCents": values[-7:], "revenue30dCents": values,
        "demoSeries": {"label": "站点负荷（来自 Schema v0.4 订单聚合）", "unit": "kWh",
                       "points": [{"hour": int(row["hour"]), "loadKw": round(float(row["load_kwh"]), 3)} for row in hourly]},
        "analytics": analytics,
    }
    connection.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "stations": len(stations), "piles": len(piles), "revenue_days": len(values)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
