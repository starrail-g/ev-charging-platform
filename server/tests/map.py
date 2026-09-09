#!/usr/bin/env python3
"""Map-service regression checks against a running mock-enabled server.

The server must be started with EV_MAP_SERVER_MOCK=1 and an isolated v0.4
database.  This test deliberately changes SQLite between two requests to
prove that ordinary map-cache hits re-aggregate live pile state, while a
request-id replay returns the original response byte-for-byte.
"""

import json
import os
import socket
import sqlite3
import struct


HOST = os.getenv("EV_SERVER_HOST", "127.0.0.1")
PORT = int(os.getenv("EV_SERVER_PORT", "45454"))
DATABASE_PATH = os.getenv("EV_DATABASE_PATH", "var/ev-charging.db")


def exchange(message):
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode()
    with socket.create_connection((HOST, PORT), timeout=3) as sock:
        sock.sendall(struct.pack(">I", len(body)) + body)
        header = sock.recv(4)
        assert len(header) == 4
        size = struct.unpack(">I", header)[0]
        assert 0 < size <= 1024 * 1024, size
        payload = b""
        while len(payload) < size:
            chunk = sock.recv(size - len(payload))
            assert chunk
            payload += chunk
        return json.loads(payload)


def request(identifier, operation, payload):
    return {"v": 1, "id": identifier, "type": operation, "payload": payload}


def assert_error(response, code):
    assert response["type"] == "error", response
    assert response["payload"]["code"] == code, response


def database_rows(query, parameters=()):
    with sqlite3.connect(DATABASE_PATH, timeout=5) as connection:
        return connection.execute(query, parameters).fetchall()


def execute(statement, parameters=()):
    with sqlite3.connect(DATABASE_PATH, timeout=5) as connection:
        connection.execute(statement, parameters)


def contains_key(value, forbidden):
    if isinstance(value, dict):
        return any(key in forbidden or contains_key(item, forbidden)
                   for key, item in value.items())
    if isinstance(value, list):
        return any(contains_key(item, forbidden) for item in value)
    return False


login = exchange(request("map-test-login", "user.login", {"phone": "13900000001"}))
assert login["type"] == "user.login.result", login
user_id = login["payload"]["user"]["id"]

search_payload = {
    "user_id": user_id,
    "origin": {"kind": "address", "value": "沈阳市浑南区软件园"},
    "radius_meters": 1000,
    "page_size": 20,
}
first_request = request("map-test-search-1", "map.station.search", search_payload)
first = exchange(first_request)
assert first["type"] == "map.station.search.result", first
first_payload = first["payload"]
assert first_payload["warning"]["code"] == 1410, first_payload
stations = first_payload["stations"]
assert stations and all("pile_idle" in station for station in stations), first_payload
station_ids = [station["id"] for station in stations]

cache_rows = database_rows("SELECT payload_json FROM map_cache_entries")
assert cache_rows, "map search should populate the map-only cache"
for (payload_json,) in cache_rows:
    cached = json.loads(payload_json)
    assert not contains_key(cached, {
        "status", "pile_idle", "pile_fault", "pile_reserved", "pile_charging",
        "pile_offline", "pile_total", "pile_snapshot_version", "power_kw",
        "price_cents",
    }), cached

# A normal business transition advances the imported station's snapshot.
station_with_idle = next(station for station in stations if station["pile_idle"] > 0)
pile_page = exchange(request("map-test-piles", "pile.list", {
    "station_id": station_with_idle["id"]}))
idle_pile = next(pile for pile in pile_page["payload"]["piles"]
                 if pile["status"] == "idle")
reservation = exchange(request("map-test-reservation", "reservation.create", {
    "user_id": user_id, "pile_id": idle_pile["id"]}))
assert reservation["type"] == "reservation.create.result", reservation

second = exchange(request("map-test-search-2", "map.station.search", search_payload))
assert second["type"] == "map.station.search.result", second
assert second["payload"]["data_source"] == "server_mock", second
assert second["payload"]["warning"]["code"] == 1410, second
second_station = next(station for station in second["payload"]["stations"]
                      if station["id"] == station_with_idle["id"])
assert second_station["pile_reserved"] == station_with_idle["pile_reserved"] + 1, second_station
assert (second_station["pile_snapshot_version"]
        == station_with_idle["pile_snapshot_version"] + 1), second_station

# Idempotent replay is the explicit exception: it returns the historical
# response, including the old pile snapshot.
assert exchange(first_request) == first

cancel = exchange(request("map-test-reservation-cancel", "reservation.cancel", {
    "user_id": user_id, "order_id": reservation["payload"]["order"]["id"]}))
assert cancel["type"] == "reservation.cancel.result", cancel

# Even an out-of-band database change cannot turn a map-only cache entry into
# a stale complete response: the next ordinary request aggregates SQLite.
placeholders = ",".join("?" for _ in station_ids)
execute(
    "UPDATE charging_piles SET status='fault', status_source='business', "
    "status_updated_at='2026-09-09T00:00:00Z' WHERE station_id IN (" + placeholders + ")",
    station_ids,
)
third = exchange(request("map-test-search-3", "map.station.search", search_payload))
for station in third["payload"]["stations"]:
    assert station["pile_idle"] == 0, station
    assert station["pile_fault"] == station["pile_total"], station

assert_error(exchange(request("map-test-bad-page-token", "map.station.search", {
    **search_payload, "page_token": "not-a-valid-token"})), 1002)

route = exchange(request("map-test-route", "map.route.plan", {
    "user_id": user_id,
    "origin": {"kind": "coordinate", "latitude": 41.7192, "longitude": 123.4315},
    "station_id": station_ids[0],
    "mode": "driving",
}))
assert route["type"] == "map.route.plan.result", route
assert len(route["payload"]["polyline"]) >= 2, route

admin_login = exchange(request("map-test-admin-login", "admin.login", {
    "username": "admin", "password": "123456"}))
assert admin_login["type"] == "admin.login.result", admin_login
token = admin_login["payload"]["token"]

def audit_request(identifier, page_token=None):
    payload = {"token": token, "limit": 1, "result_status": "mock"}
    if page_token is not None:
        payload["page_token"] = page_token
    return exchange(request(identifier, "admin.map.audit.list", payload))


audit_records = []
page_token = None
for index in range(10):
    page = audit_request(f"map-test-audit-{index}", page_token)
    assert page["type"] == "admin.map.audit.list.result", page
    records = page["payload"]["records"]
    audit_records.extend(records)
    page_token = page["payload"]["next_page_token"]
    if not page["payload"]["has_more"]:
        break
assert len(audit_records) >= 2, audit_records
assert len({record["id"] for record in audit_records}) == len(audit_records), audit_records
assert all(audit_records[index]["created_at"] >= audit_records[index + 1]["created_at"]
           or (audit_records[index]["created_at"] == audit_records[index + 1]["created_at"]
               and audit_records[index]["id"] > audit_records[index + 1]["id"])
           for index in range(len(audit_records) - 1)), audit_records
assert_error(exchange(request("map-test-audit-unauthorized", "admin.map.audit.list", {
    "limit": 1})), 1100)
assert_error(exchange(request("map-test-audit-bad-token", "admin.map.audit.list", {
    "token": token, "limit": 1, "page_token": "bad"})), 1002)

print("map service tests passed")
