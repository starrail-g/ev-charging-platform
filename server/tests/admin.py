#!/usr/bin/env python3
"""Administrator API smoke test against a seeded v0.3 server."""
import json
import os
import sqlite3
import socket
import struct
from datetime import datetime, time, timedelta, timezone


HOST = os.getenv("EV_SERVER_HOST", "127.0.0.1")
PORT = int(os.getenv("EV_SERVER_PORT", "45454"))
DATABASE_PATH = os.getenv("EV_DATABASE_PATH", "var/ev-charging.db")


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


def admin_request(identifier, operation, payload):
    payload = dict(payload)
    payload["token"] = admin_token
    return request(identifier, operation, payload)


def assert_error(response, code):
    assert response["type"] == "error", response
    assert response["payload"]["code"] == code, response


def database_row(query, parameters=()):
    with sqlite3.connect(DATABASE_PATH, timeout=5) as connection:
        return connection.execute(query, parameters).fetchone()


def execute_database(statement, parameters=()):
    with sqlite3.connect(DATABASE_PATH, timeout=5) as connection:
        connection.execute(statement, parameters)


def pile_from_station(station_id, pile_id):
    response = exchange(request(
        f"admin-pile-state-{station_id}-{pile_id}", "pile.list", {"station_id": station_id}))
    assert response["type"] == "pile.list.result", response
    matches = [pile for pile in response["payload"]["piles"] if pile["id"] == pile_id]
    assert len(matches) == 1, response
    return matches[0]


def active_order_for_user(user_id):
    response = exchange(request(
        f"admin-active-order-{user_id}", "order.active.get", {"user_id": user_id}))
    assert response["type"] == "order.active.get.result", response
    return response["payload"]["order"]


login = exchange(request("admin-login", "admin.login", {
    "username": "admin", "password": "123456"}))
assert login["type"] == "admin.login.result", login
administrator = login["payload"]["admin"]
assert administrator["role"] == "super_admin", administrator
admin_id = administrator["id"]
admin_token = login["payload"]["token"]
assert isinstance(admin_token, str) and len(admin_token) == 64
assert login["payload"]["expires_in_seconds"] > 0

assert_error(exchange(request("admin-bad-login", "admin.login", {
    "username": "admin", "password": "wrong"})), 1100)

# All management operations, including sensitive read endpoints, require a
# login-issued token.  administrator_id alone is not an authentication factor.
for operation, payload in (
    ("admin.statistics.get", {"range": "7d"}),
    ("admin.station.list", {}),
    ("admin.user.list", {}),
    ("admin.station.create", {
        "administrator_id": admin_id, "name": "未授权站点", "address": "不可达",
        "latitude": 41.7, "longitude": 123.4, "pile_count": 1}),
):
    assert_error(exchange(request(f"admin-unauthorized-{operation}", operation, payload)), 1100)
assert_error(exchange(request("admin-invalid-token", "admin.station.list", {"token": "invalid"})), 1100)
assert_error(exchange(admin_request("admin-mismatched-id", "admin.station.create", {
    "administrator_id": admin_id + 1, "name": "伪造站点", "address": "不可达",
    "latitude": 41.7, "longitude": 123.4, "pile_count": 1})), 1100)
# Token validation also re-checks the administrator's current active status;
# disabling an account revokes its existing process-local token immediately.
execute_database("UPDATE administrators SET status = 'disabled' WHERE id = ?", (admin_id,))
assert_error(exchange(admin_request("admin-disabled-token", "admin.station.list", {})), 1100)
execute_database("UPDATE administrators SET status = 'active' WHERE id = ?", (admin_id,))

statistics = exchange(admin_request("admin-statistics", "admin.statistics.get", {"range": "30d"}))
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

statistics_7d = exchange(admin_request("admin-statistics-7d", "admin.statistics.get", {"range": "7d"}))
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

stations = exchange(admin_request("admin-stations", "admin.station.list", {}))
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
    admin_request("admin-statistics-stations", "admin.statistics.get", {"range": "7d"}))
average_from_stations = sum(row["utilization"] for row in station_rows) / len(station_rows)
assert abs(statistics_for_stations["payload"]["statistics"]["avg_station_utilization"]
           - average_from_stations) < 1e-4

created_request = admin_request("admin-create-station", "admin.station.create", {
    "administrator_id": admin_id, "name": "API 验证站", "address": "测试路 1 号",
    "latitude": 41.7, "longitude": 123.4, "pile_count": 2})
created = exchange(created_request)
assert created["type"] == "admin.station.create.result", created
assert created["payload"]["station"]["pile_total"] == 2
assert created["payload"]["station"]["utilization"] == 0
assert created["payload"]["station"]["utilization_range"] == "7d"
assert exchange(created_request) == created

# Restart is a fault-recovery action.  It must reject idle/reserved/charging
# without changing either the pile or the reservation/charging session.
restart_rejected_cases = (
    ("idle", 101, 1, None),
    ("reserved", 203, 2, 4),
    ("charging", 102, 1, 2),
)
for label, pile_id, station_id, user_id in restart_rejected_cases:
    pile_before = pile_from_station(station_id, pile_id)
    assert pile_before["status"] == label, pile_before
    order_before = active_order_for_user(user_id) if user_id else None
    if user_id:
        assert order_before["pile_id"] == pile_id, order_before

    rejected_request = admin_request(f"admin-restart-reject-{label}", "admin.pile.restart", {
        "administrator_id": admin_id, "pile_id": pile_id})
    rejected = exchange(rejected_request)
    assert_error(rejected, 1201)
    assert pile_from_station(station_id, pile_id) == pile_before
    if user_id:
        assert active_order_for_user(user_id) == order_before
    # Business rejections are audited but do not create a replay record.
    assert database_row(
        "SELECT result, reason FROM pile_restart_logs WHERE pile_id = ? "
        "ORDER BY id DESC LIMIT 1", (pile_id,)
    ) == ("rejected", "pile is not recoverable")
    assert database_row("SELECT COUNT(*) FROM request_records WHERE request_id = ?",
                        (rejected_request["id"],))[0] == 0

# Fault and offline piles are the only successful restart paths.  A successful
# request is persisted and an identical request ID returns the original result
# without incrementing restart_count again.
for label, pile_id, station_id in (("fault", 103, 1), ("offline", 202, 2)):
    pile_before = pile_from_station(station_id, pile_id)
    assert pile_before["status"] == label, pile_before
    restart_request = admin_request(f"admin-restart-{label}", "admin.pile.restart", {
        "administrator_id": admin_id, "pile_id": pile_id})
    restarted = exchange(restart_request)
    assert restarted["type"] == "admin.pile.restart.result", restarted
    restarted_pile = restarted["payload"]["pile"]
    assert restarted_pile["status"] == "idle", restarted
    assert restarted_pile["restart_count"] == pile_before["restart_count"] + 1, restarted
    assert restarted_pile["last_restart_at"], restarted
    assert database_row("SELECT COUNT(*) FROM request_records WHERE request_id = ?",
                        (restart_request["id"],))[0] == 1
    assert exchange(restart_request) == restarted
    assert pile_from_station(station_id, pile_id)["restart_count"] == restarted_pile["restart_count"]

# A failed request is not persisted: after the rejected idle pile is repaired
# to fault, the same request ID can be retried and then becomes idempotent.
failed_retry_request = admin_request("admin-restart-failed-retry", "admin.pile.restart", {
    "administrator_id": admin_id, "pile_id": 101})
idle_before_retry = pile_from_station(1, 101)
assert idle_before_retry["status"] == "idle", idle_before_retry
assert_error(exchange(failed_retry_request), 1201)
assert database_row("SELECT COUNT(*) FROM request_records WHERE request_id = ?",
                    (failed_retry_request["id"],))[0] == 0
execute_database("UPDATE charging_piles SET status = 'fault' WHERE id = ?", (101,))
retried = exchange(failed_retry_request)
assert retried["type"] == "admin.pile.restart.result", retried
assert retried["payload"]["pile"]["status"] == "idle", retried
assert retried["payload"]["pile"]["restart_count"] == idle_before_retry["restart_count"] + 1
assert exchange(failed_retry_request) == retried

users = exchange(admin_request("admin-users", "admin.user.list", {"phone_query": "1390013"}))
assert users["type"] == "admin.user.list.result", users
assert users["payload"]["users"][0]["active_order_status"] == "charging"
assert users["payload"]["users"][0]["created_at"] == "2026-08-21T03:00:00Z"

frozen_request = admin_request("admin-freeze-user", "admin.user.status.set", {
    "administrator_id": admin_id, "user_id": 2, "status": "frozen"})
frozen = exchange(frozen_request)
assert frozen["type"] == "admin.user.status.set.result", frozen
assert frozen["payload"]["user"]["status"] == "frozen"
assert exchange(frozen_request) == frozen

print("administrator API smoke test passed")
