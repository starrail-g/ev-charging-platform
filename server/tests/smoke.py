#!/usr/bin/env python3
"""Minimal protocol smoke test. Start ev-server before running this script."""
import json
import os
import socket
import struct

HOST = os.getenv("EV_SERVER_HOST", "127.0.0.1")
PORT = int(os.getenv("EV_SERVER_PORT", "45454"))


def exchange(request):
    payload = json.dumps(request, separators=(",", ":")).encode()
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.sendall(struct.pack(">I", len(payload)) + payload)
        header = sock.recv(4)
        size = struct.unpack(">I", header)[0]
        body = b""
        while len(body) < size:
            body += sock.recv(size - len(body))
        return json.loads(body)


def exchange_batch(frames):
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.sendall(b"".join(frames))
        responses = []
        while True:
            try:
                header = sock.recv(4)
            except socket.timeout:
                break
            if not header:
                break
            size = struct.unpack(">I", header)[0]
            body = b""
            while len(body) < size:
                body += sock.recv(size - len(body))
            responses.append(json.loads(body))
        return responses


def frame(request):
    payload = json.dumps(request, separators=(",", ":")).encode()
    return struct.pack(">I", len(payload)) + payload


assert exchange({"v": 1, "id": "smoke-1", "type": "health", "payload": {}})["payload"]["status"] == "ok"
assert exchange({"v": 1, "id": "smoke-2", "type": "echo", "payload": {"value": 7}})["payload"]["value"] == 7
assert exchange({"v": 1, "id": "smoke-3", "type": "unknown", "payload": {}})["type"] == "error"
login = exchange({"v": 1, "id": "smoke-login-1", "type": "user.login",
                  "payload": {"phone": "13912345678"}})
assert login["type"] == "user.login.result"
user = login["payload"]["user"]
assert user["phone"] == "13912345678"
assert {"id", "phone", "nickname", "avatar_path", "balance_cents", "status"}.issubset(user)
repeat_login = exchange({"v": 1, "id": "smoke-login-2", "type": "user.login",
                         "payload": {"phone": "13912345678"}})
assert repeat_login["payload"]["user"]["id"] == user["id"]
invalid_login = exchange({"v": 1, "id": "smoke-login-invalid", "type": "user.login",
                          "payload": {"phone": "139123"}})
assert invalid_login["type"] == "error"
assert invalid_login["payload"]["code"] == 1002
batch = exchange_batch([
    frame({"v": 1, "id": "smoke-good", "type": "echo", "payload": {"value": 8}}),
    b"\x00\x00\x00\x01!",
])
assert [response["id"] for response in batch] == ["smoke-good", "server"]
assert batch[0]["payload"]["value"] == 8
assert batch[1]["payload"]["code"] == 1001
print("server smoke test passed")
