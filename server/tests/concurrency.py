#!/usr/bin/env python3
"""Concurrent lifecycle regression checks against a seeded server.

Run the server with a fresh database and deterministic seed before invoking
this script. Each request uses a separate TCP connection, matching two
independent clients.
"""
import json
import os
import socket
import struct
import threading


HOST = os.getenv("EV_SERVER_HOST", "127.0.0.1")
PORT = int(os.getenv("EV_SERVER_PORT", "45454"))


def exchange(message):
    payload = json.dumps(message, separators=(",", ":")).encode()
    with socket.create_connection((HOST, PORT), timeout=5) as sock:
        sock.sendall(struct.pack(">I", len(payload)) + payload)
        header = sock.recv(4)
        if len(header) != 4:
            raise RuntimeError("server closed before response header")
        size = struct.unpack(">I", header)[0]
        body = b""
        while len(body) < size:
            body += sock.recv(size - len(body))
        return json.loads(body)


def request(request_id, request_type, payload):
    return {"v": 1, "id": request_id, "type": request_type, "payload": payload}


def parallel(messages):
    barrier = threading.Barrier(len(messages))
    results = [None] * len(messages)

    def run(index, message):
        barrier.wait()
        results[index] = exchange(message)

    threads = [threading.Thread(target=run, args=(i, message))
               for i, message in enumerate(messages)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    return results


def login(phone, request_id):
    response = exchange(request(request_id, "user.login", {"phone": phone}))
    assert response["type"] == "user.login.result", response
    return response["payload"]["user"]["id"]


def assert_conflict(response):
    assert response["type"] == "error", response
    assert response["payload"]["code"] == 1201, response


user_a = login("13100000001", "concurrent-login-a")
user_b = login("13100000002", "concurrent-login-b")
reservation_messages = [
    request("concurrent-reservation-a", "reservation.create",
            {"user_id": user_a, "pile_id": 101}),
    request("concurrent-reservation-b", "reservation.create",
            {"user_id": user_b, "pile_id": 101}),
]
reservation_results = parallel(reservation_messages)
reservation_successes = [result for result in reservation_results
                         if result["type"] == "reservation.create.result"]
assert len(reservation_successes) == 1, reservation_results
assert sum(result["type"] == "error" for result in reservation_results) == 1
assert_conflict(next(result for result in reservation_results
                     if result["type"] == "error"))
winner = reservation_successes[0]
winner_user = winner["payload"]["order"]["user_id"]
winner_order = winner["payload"]["order"]["id"]
cancel = exchange(request("concurrent-reservation-cleanup", "reservation.cancel",
                          {"user_id": winner_user, "order_id": winner_order}))
assert cancel["type"] == "reservation.cancel.result", cancel
assert cancel["payload"]["pile"]["status"] == "idle", cancel

stop = exchange(request("concurrent-stop", "charging.stop",
                        {"user_id": 2, "order_id": 1002,
                         "ended_at": "2026-09-01T00:32:00Z"}))
assert stop["type"] == "charging.stop.result", stop
settle_results = parallel([
    request("concurrent-settle-a", "charging.settle",
            {"user_id": 2, "order_id": 1002}),
    request("concurrent-settle-b", "charging.settle",
            {"user_id": 2, "order_id": 1002}),
])
settle_successes = [result for result in settle_results
                    if result["type"] == "charging.settle.result"]
assert len(settle_successes) == 1, settle_results
assert_conflict(next(result for result in settle_results
                     if result["type"] == "error"))
settled = settle_successes[0]["payload"]
assert settled["order"]["status"] == "completed", settled
assert settled["balance_cents"] == 4996, settled

history = exchange(request("concurrent-history", "order.history.list", {"user_id": 2}))
completed = [order for order in history["payload"]["orders"] if order["id"] == 1002]
assert len(completed) == 1, history
assert completed[0]["status"] == "completed", history
print("concurrent lifecycle test passed")
