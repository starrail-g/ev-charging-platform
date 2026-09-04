#!/usr/bin/env python3
"""Administrator API smoke test against a seeded v0.3 server."""
import json
import os
import socket
import struct


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
assert statistics["payload"]["statistics"]["revenue_cents"] >= 0

stations = exchange(request("admin-stations", "admin.station.list", {}))
assert stations["type"] == "admin.station.list.result", stations
assert len(stations["payload"]["stations"]) >= 2

created_request = request("admin-create-station", "admin.station.create", {
    "administrator_id": admin_id, "name": "API 验证站", "address": "测试路 1 号",
    "latitude": 41.7, "longitude": 123.4, "pile_count": 2})
created = exchange(created_request)
assert created["type"] == "admin.station.create.result", created
assert created["payload"]["station"]["pile_total"] == 2
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
