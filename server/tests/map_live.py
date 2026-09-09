#!/usr/bin/env python3
"""Production-adapter map checks against a configured running EV server.

The Tencent key remains exclusively in the server process environment. This
client neither reads nor prints it and works with both the official endpoint
and the loopback fake endpoint.
"""

import json
import os
import socket
import struct


HOST = os.getenv("EV_SERVER_HOST", "127.0.0.1")
PORT = int(os.getenv("EV_SERVER_PORT", "45540"))


def exchange(message):
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode()
    with socket.create_connection((HOST, PORT), timeout=10) as connection:
        connection.sendall(struct.pack(">I", len(body)) + body)
        header = connection.recv(4)
        assert len(header) == 4
        size = struct.unpack(">I", header)[0]
        assert 0 < size <= 1024 * 1024, size
        response = b""
        while len(response) < size:
            chunk = connection.recv(size - len(response))
            assert chunk
            response += chunk
        return json.loads(response)


def request(identifier, operation, payload):
    return {"v": 1, "id": identifier, "type": operation, "payload": payload}


login = exchange(request("map-live-login", "user.login", {"phone": "13900000001"}))
assert login["type"] == "user.login.result", login
user_id = login["payload"]["user"]["id"]

search = exchange(request("map-live-search", "map.station.search", {
    "user_id": user_id,
    "origin": {"kind": "address", "value": "沈阳市浑南区软件园"},
    "radius_meters": 1000,
    "page_size": 20,
}))
assert search["type"] == "map.station.search.result", search
payload = search["payload"]
assert payload["data_source"] == "tencent_live", payload
assert payload["warning"] is None, payload
assert payload["stations"], payload
assert all(station["provider"] == "tencent" for station in payload["stations"]), payload
assert all("pile_idle" in station and "pile_snapshot_version" in station
           for station in payload["stations"]), payload

if os.getenv("EV_EXPECT_PAGED") == "1":
    assert len(payload["stations"]) == 20, payload
    assert payload["has_more"] is True and payload["next_page_token"], payload
    second = exchange(request("map-live-search-page-2", "map.station.search", {
        "user_id": user_id,
        "origin": {"kind": "address", "value": "沈阳市浑南区软件园"},
        "radius_meters": 1000,
        "page_size": 20,
        "page_token": payload["next_page_token"],
    }))
    assert second["type"] == "map.station.search.result", second
    assert len(second["payload"]["stations"]) == 1, second
    assert second["payload"]["stations"][0]["provider_poi_id"] == "fake-live-paged-21", second

route = exchange(request("map-live-route", "map.route.plan", {
    "user_id": user_id,
    "origin": {"kind": "coordinate", "latitude": 41.7192, "longitude": 123.4315},
    "station_id": payload["stations"][0]["id"],
    "mode": "driving",
}))
assert route["type"] == "map.route.plan.result", route
route_payload = route["payload"]
assert route_payload["data_source"] == "tencent_live", route_payload
assert route_payload["warning"] is None, route_payload
assert route_payload["distance_meters"] > 0, route_payload
assert route_payload["duration_seconds"] > 0, route_payload
assert 2 <= len(route_payload["polyline"]) <= 4096, route_payload

print("live Tencent adapter integration tests passed")
