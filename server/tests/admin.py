#!/usr/bin/env python3
"""Administrator API smoke test against a seeded v0.3 server."""
import json
import os
import socket
import struct
from datetime import datetime, time, timedelta, timezone


HOST = os.getenv("EV_SERVER_HOST", "127.0.0.1")
PORT = int(os.getenv("EV_SERVER_PORT", "45454"))


def exchange(message):
    body = json.dumps(message, separators=(",", ":")).encode()
    with socket.create_connection((HOST, PORT), timeout=3) as sock:
        sock.sendall(struct.pack(">I", len(body)) + body)
        header = sock.recv(4)
        assert len(header) == 4
        size = struct.unpack(">I", header)[0]
        response = b""
        while len(response) < size:
            response += sock.recv(size - len(response))
        return json.loads(response)


def request(identifier, operation, payload):
    return {"v": 1, "id": identifier, "type": operation, "payload": payload}


def assert_error(response, code):
    assert response["type"] == "error", response
    assert response["payload"]["code"] == code, response


login = exchange(request("admin-login", "admin.login", {
    "username": "admin", "password": "123456"}))
assert login["type"] == "admin.login.result", login
administrator = login["payload"]["admin"]
assert administrator["role"] == "super_admin", administrator
admin_id = administrator["id"]

assert_error(exchange(request("admin-bad-login", "admin.login", {
    "username": "admin", "password": "wrong"})), 1100)

statistics = exchange(request("admin-statistics", "admin.statistics.get", {"range": "30d"}))
assert statistics["type"] == "admin.statistics.get.result", statistics
statistics_payload = statistics["payload"]["statistics"]
daily = statistics_payload["revenue_daily"]
assert len(daily) == 30, daily
today = datetime.fromisoformat(
    statistics_payload["updated_at"].replace("Z", "+00:00")).date()
expected_dates = [(today - timedelta(days=offset)).isoformat() for offset in range(29, -1, -1)]
assert [row["date"] for row in daily] == expected_dates, daily
assert all(set(("date", "revenue_cents", "completed_order_count", "energy_wh"))
           <= set(row) for row in daily)
assert statistics_payload["revenue_cents"] == sum(row["revenue_cents"] for row in daily)
assert statistics_payload["completed_order_count"] == sum(
    row["completed_order_count"] for row in daily)
assert statistics_payload["energy_wh"] == sum(row["energy_wh"] for row in daily)

statistics_7d = exchange(request("admin-statistics-7d", "admin.statistics.get", {"range": "7d"}))
assert statistics_7d["type"] == "admin.statistics.get.result", statistics_7d
statistics_7d_payload = statistics_7d["payload"]["statistics"]
daily_7d = statistics_7d_payload["revenue_daily"]
assert len(daily_7d) == 7, daily_7d
today_7d = datetime.fromisoformat(
    statistics_7d_payload["updated_at"].replace("Z", "+00:00")).date()
expected_dates_7d = [(today_7d - timedelta(days=offset)).isoformat() for offset in range(6, -1, -1)]
assert [row["date"] for row in daily_7d] == expected_dates_7d, daily_7d
assert statistics_7d_payload["revenue_cents"] == sum(
    row["revenue_cents"] for row in daily_7d)
assert abs(statistics_payload["avg_station_utilization"]
           - statistics_7d_payload["avg_station_utilization"]) < 1e-4

stations = exchange(request("admin-stations", "admin.station.list", {}))
assert stations["type"] == "admin.station.list.result", stations
station_rows = stations["payload"]["stations"]
assert len(station_rows) >= 2
assert all(row["utilization_range"] == "7d" for row in station_rows), station_rows
assert all(0 <= row["utilization"] <= 1 for row in station_rows), station_rows

# The deterministic seed has one completed one-hour order on station 1 and
# one still-open charging order. Verify the interval intersection and the
# open-order cutoff are reflected in the reported seven-day ratio.
station_by_id = {row["id"]: row for row in station_rows}
updated_at_dt = datetime.fromisoformat(
    statistics_7d_payload["updated_at"].replace("Z", "+00:00"))
period_start_dt = datetime.combine(
    updated_at_dt.date() - timedelta(days=6), time.min, tzinfo=timezone.utc)

def overlap_seconds(start, end):
    return max(0, (min(end, updated_at_dt)
                   - max(start, period_start_dt)).total_seconds())

expected_numerator_seconds = overlap_seconds(
    datetime(2026, 8, 31, 5, 2, tzinfo=timezone.utc),
    datetime(2026, 8, 31, 6, 2, tzinfo=timezone.utc))
expected_numerator_seconds += overlap_seconds(
    datetime(2026, 9, 1, 0, 32, tzinfo=timezone.utc), updated_at_dt)
expected_denominator_seconds = 3 * (
    updated_at_dt - period_start_dt).total_seconds()
assert abs(station_by_id[1]["utilization"]
           - expected_numerator_seconds / expected_denominator_seconds) < 1e-5
assert station_by_id[2]["utilization"] == 0

# Both endpoints use the same seven-day station utilization definition. The
# snapshots are taken in separate requests, so allow the open charging order
# to advance by a few seconds between calls.
statistics_for_stations = exchange(
    request("admin-statistics-stations", "admin.statistics.get", {"range": "7d"}))
average_from_stations = sum(row["utilization"] for row in station_rows) / len(station_rows)
assert abs(statistics_for_stations["payload"]["statistics"]["avg_station_utilization"]
           - average_from_stations) < 1e-4

created_request = request("admin-create-station", "admin.station.create", {
    "administrator_id": admin_id, "name": "API 验证站", "address": "测试路 1 号",
    "latitude": 41.7, "longitude": 123.4, "pile_count": 2})
created = exchange(created_request)
assert created["type"] == "admin.station.create.result", created
assert created["payload"]["station"]["pile_total"] == 2
assert created["payload"]["station"]["utilization"] == 0
assert created["payload"]["station"]["utilization_range"] == "7d"
assert exchange(created_request) == created

restarted_request = request("admin-restart-pile", "admin.pile.restart", {
    "administrator_id": admin_id, "pile_id": 103})
restarted = exchange(restarted_request)
assert restarted["type"] == "admin.pile.restart.result", restarted
assert restarted["payload"]["pile"]["status"] == "idle"

users = exchange(request("admin-users", "admin.user.list", {"phone_query": "1390013"}))
assert users["type"] == "admin.user.list.result", users
assert users["payload"]["users"][0]["active_order_status"] == "charging"
assert users["payload"]["users"][0]["created_at"] == "2026-08-21T03:00:00Z"

frozen_request = request("admin-freeze-user", "admin.user.status.set", {
    "administrator_id": admin_id, "user_id": 2, "status": "frozen"})
frozen = exchange(frozen_request)
assert frozen["type"] == "admin.user.status.set.result", frozen
assert frozen["payload"]["user"]["status"] == "frozen"
assert exchange(frozen_request) == frozen

print("administrator API smoke test passed")
