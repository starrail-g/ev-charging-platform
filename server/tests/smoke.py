#!/usr/bin/env python3
"""End-to-end protocol smoke test.

Run against a clean server database initialized with database/schema/schema.sql
and database/seeds/dev.sql (or the server's EV_DATABASE_SEED_PATH support).
The seeded user 4 has enough balance for one deterministic settlement.
"""
import json
import os
import socket
import sqlite3
import struct
from contextlib import contextmanager


HOST = os.getenv("EV_SERVER_HOST", "127.0.0.1")
PORT = int(os.getenv("EV_SERVER_PORT", "45454"))
# Fault-injection assertions open this same database after each socket request
# to confirm a failed transaction did not leave a partial write behind.  The
# clean smoke command supplies EV_DATABASE_PATH; the default keeps the basic
# local invocation useful when server and test share the repository root.
DATABASE_PATH = os.getenv("EV_DATABASE_PATH", "var/ev-charging.db")


def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise RuntimeError("server closed connection before response completed")
        data += chunk
    return data


def exchange(request):
    payload = json.dumps(request, separators=(",", ":")).encode()
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.sendall(struct.pack(">I", len(payload)) + payload)
        size = struct.unpack(">I", recv_exact(sock, 4))[0]
        return json.loads(recv_exact(sock, size))


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
            responses.append(json.loads(recv_exact(sock, size)))
        return responses


def request(request_id, request_type, payload):
    return {"v": 1, "id": request_id, "type": request_type, "payload": payload}


def frame(message):
    payload = json.dumps(message, separators=(",", ":")).encode()
    return struct.pack(">I", len(payload)) + payload


def assert_error(response, code):
    assert response["type"] == "error", response
    assert response["payload"]["code"] == code, response
    return response["payload"]


def database_row(query, parameters=()):
    with sqlite3.connect(DATABASE_PATH, timeout=5) as connection:
        return connection.execute(query, parameters).fetchone()


def execute_database(statement):
    with sqlite3.connect(DATABASE_PATH, timeout=5) as connection:
        connection.execute(statement)


@contextmanager
def injected_trigger(name, statement):
    execute_database("DROP TRIGGER IF EXISTS " + name)
    execute_database(statement)
    try:
        yield
    finally:
        execute_database("DROP TRIGGER IF EXISTS " + name)


assert exchange(request("smoke-1", "health", {}))["payload"]["status"] == "ok"
assert exchange(request("smoke-2", "echo", {"value": 7}))["payload"]["value"] == 7
assert_error(exchange(request("smoke-3", "unknown", {})), 1002)

login = exchange(request("smoke-login-1", "user.login", {"phone": "13912345678"}))
assert login["type"] == "user.login.result"
new_user = login["payload"]["user"]
assert new_user["phone"] == "13912345678"
assert {"id", "phone", "nickname", "avatar_path", "balance_cents", "status"}.issubset(new_user)
repeat_login = exchange(request("smoke-login-2", "user.login", {"phone": "13912345678"}))
assert repeat_login["payload"]["user"]["id"] == new_user["id"]
assert_error(exchange(request("smoke-login-invalid", "user.login", {"phone": "139123"})), 1002)

# User profile reads accept only a JSON integer ID.  Each exchange uses a new
# connection, so the read after the update also proves the values persisted.
profile = exchange(request("smoke-profile-get", "user.profile.get",
                           {"user_id": new_user["id"]}))
assert profile["type"] == "user.profile.get.result"
assert profile["payload"]["user"] == new_user
assert_error(exchange(request("smoke-profile-invalid", "user.profile.get",
                              {"user_id": "1"})), 1002)
assert_error(exchange(request("smoke-profile-not-found", "user.profile.get",
                              {"user_id": 999999999})), 1200)

profile_update = request("smoke-profile-update", "user.profile.update", {
    "user_id": new_user["id"],
    "nickname": "资料测试用户",
    "avatar_path": "/avatars/smoke-user.png",
})
updated_profile = exchange(profile_update)
assert updated_profile["type"] == "user.profile.update.result"
assert updated_profile["payload"]["user"]["nickname"] == "资料测试用户"
assert updated_profile["payload"]["user"]["avatar_path"] == "/avatars/smoke-user.png"
assert exchange(profile_update) == updated_profile
persisted_profile = exchange(request("smoke-profile-get-after-update", "user.profile.get",
                                     {"user_id": new_user["id"]}))
assert persisted_profile["payload"]["user"] == updated_profile["payload"]["user"]
database_profile = database_row(
    "SELECT nickname, avatar_path, updated_at FROM users WHERE id = ?", (new_user["id"],))
assert database_profile[:2] == ("资料测试用户", "/avatars/smoke-user.png")
assert database_profile[2]
assert_error(exchange(request("smoke-profile-update-invalid", "user.profile.update", {
    "user_id": new_user["id"], "nickname": ""
})), 1002)

# Injecting an UPDATE abort must leave neither profile fields nor a successful
# request record behind.  The same request ID can then safely be retried.
profile_failure = request("smoke-profile-update-write-failure", "user.profile.update", {
    "user_id": new_user["id"], "nickname": "故障后资料"
})
with injected_trigger("smoke_profile_update_fail", """
    CREATE TRIGGER smoke_profile_update_fail
    BEFORE UPDATE OF nickname ON users
    WHEN NEW.id = %d
    BEGIN
        SELECT RAISE(ABORT, 'smoke injected profile update failure');
    END
""" % new_user["id"]):
    profile_failure_response = exchange(profile_failure)
    profile_failure_error = assert_error(profile_failure_response, 1300)
    assert "injected" not in profile_failure_error["message"].lower()
    assert exchange(request("smoke-profile-get-after-failure", "user.profile.get", {
        "user_id": new_user["id"]
    }))["payload"]["user"] == updated_profile["payload"]["user"]
    assert database_row("SELECT COUNT(*) FROM request_records WHERE request_id = ?",
                        ("smoke-profile-update-write-failure",))[0] == 0
profile_retried = exchange(profile_failure)
assert profile_retried["type"] == "user.profile.update.result"
assert profile_retried["payload"]["user"]["nickname"] == "故障后资料"

# Wallet changes are integer fen, idempotent by request ID, and immediately
# observable through the independently handled profile read.
balance_before_recharge = profile_retried["payload"]["user"]["balance_cents"]
recharge = request("smoke-recharge-success", "wallet.recharge", {
    "user_id": new_user["id"], "amount_cents": 375
})
recharged = exchange(recharge)
assert recharged["type"] == "wallet.recharge.result"
assert recharged["payload"]["balance_cents"] == balance_before_recharge + 375
assert isinstance(recharged["payload"]["transaction_id"], int)
assert recharged["payload"]["transaction_id"] > 0
assert exchange(recharge) == recharged
execute_database("UPDATE users SET status = 'frozen' WHERE id = %d" % new_user["id"])
assert_error(exchange(profile_update), 1100)
assert_error(exchange(recharge), 1100)
execute_database("UPDATE users SET status = 'active' WHERE id = %d" % new_user["id"])
assert exchange(profile_update) == updated_profile
assert exchange(recharge) == recharged
assert_error(exchange(request("smoke-recharge-id-conflict", "wallet.recharge", {
    "user_id": new_user["id"], "amount_cents": 0
})), 1002)
assert_error(exchange(request("smoke-recharge-invalid-user", "wallet.recharge", {
    "user_id": "1", "amount_cents": 10
})), 1002)
assert_error(exchange(request("smoke-recharge-success", "wallet.recharge", {
    "user_id": new_user["id"], "amount_cents": 376
})), 1201)
assert exchange(request("smoke-profile-get-after-recharge", "user.profile.get", {
    "user_id": new_user["id"]
}))["payload"]["user"]["balance_cents"] == recharged["payload"]["balance_cents"]
assert database_row("SELECT COUNT(*) FROM wallet_transactions WHERE idempotency_key = ?",
                    ("smoke-recharge-success",))[0] == 1

# The balance update precedes the recharge ledger insert.  A trigger aborting
# that insert proves the enclosing transaction rolls the balance update back;
# retrying the identical request after cleanup creates exactly one transaction.
balance_before_failed_recharge = recharged["payload"]["balance_cents"]
recharge_failure = request("smoke-recharge-ledger-failure", "wallet.recharge", {
    "user_id": new_user["id"], "amount_cents": 91
})
with injected_trigger("smoke_recharge_insert_fail", """
    CREATE TRIGGER smoke_recharge_insert_fail
    BEFORE INSERT ON wallet_transactions
    WHEN NEW.user_id = %d AND NEW.transaction_type = 'recharge'
    BEGIN
        SELECT RAISE(ABORT, 'smoke injected recharge ledger failure');
    END
""" % new_user["id"]):
    recharge_failure_response = exchange(recharge_failure)
    recharge_failure_error = assert_error(recharge_failure_response, 1300)
    assert "injected" not in recharge_failure_error["message"].lower()
    assert exchange(request("smoke-profile-get-after-recharge-failure", "user.profile.get", {
        "user_id": new_user["id"]
    }))["payload"]["user"]["balance_cents"] == balance_before_failed_recharge
    assert database_row("SELECT COUNT(*) FROM wallet_transactions WHERE idempotency_key = ?",
                        ("smoke-recharge-ledger-failure",))[0] == 0
    assert database_row("SELECT COUNT(*) FROM request_records WHERE request_id = ?",
                        ("smoke-recharge-ledger-failure",))[0] == 0
recharge_retried = exchange(recharge_failure)
assert recharge_retried["type"] == "wallet.recharge.result"
assert recharge_retried["payload"]["balance_cents"] == balance_before_failed_recharge + 91
assert exchange(recharge_failure) == recharge_retried
assert database_row("SELECT COUNT(*) FROM wallet_transactions WHERE idempotency_key = ?",
                    ("smoke-recharge-ledger-failure",))[0] == 1

stations = exchange(request("smoke-stations", "station.list", {}))
assert stations["type"] == "station.list.result"
assert any(station["id"] == 1 for station in stations["payload"]["stations"])
piles = exchange(request("smoke-piles", "pile.list", {"station_id": 1}))
assert piles["type"] == "pile.list.result"
assert any(pile["id"] == 101 and pile["status"] == "idle" for pile in piles["payload"]["piles"])

# Seeded user 4 and order 1003 exercise the complete successful lifecycle.
active = exchange(request("smoke-active-seeded", "order.active.get", {"user_id": 4}))
assert active["payload"]["order"]["id"] == 1003
start = request("smoke-start-seeded", "charging.start", {"user_id": 4, "order_id": 1003})
started = exchange(start)
assert started["type"] == "charging.start.result"
assert started["payload"]["order"]["status"] == "charging"
assert started["payload"]["pile"]["status"] == "charging"
assert exchange(start) == started
assert_error(exchange(request("smoke-start-duplicate", "charging.start", {"user_id": 4, "order_id": 1003})), 1201)

stop = request("smoke-stop-seeded", "charging.stop", {"user_id": 4, "order_id": 1003})
# Use the recorded start time so the fixture remains affordable regardless of
# the wall clock time when this smoke test is run.
stop["payload"]["ended_at"] = started["payload"]["order"]["started_at"]
stopped = exchange(stop)
assert stopped["type"] == "charging.stop.result"
assert stopped["payload"]["order"]["status"] == "pending_settlement"
assert stopped["payload"]["order"]["total_amount_cents"] == 53
assert exchange(stop) == stopped
settle = request("smoke-settle-seeded", "charging.settle", {"user_id": 4, "order_id": 1003})
settled = exchange(settle)
assert settled["type"] == "charging.settle.result"
assert settled["payload"]["order"]["status"] == "completed"
assert settled["payload"]["order"]["ended_at"]
assert settled["payload"]["order"]["settled_at"]
assert exchange(settle) == settled
history = exchange(request("smoke-history", "order.history.list", {"user_id": 4}))
history_order = next(order for order in history["payload"]["orders"] if order["id"] == 1003)
assert history_order["status"] == "completed"
assert {"station_name", "station_address", "pile_code"}.issubset(history_order)

# Reservation create/duplicate/cancel verifies pile ownership and replay.
reservation_user = exchange(request("smoke-login-reservation", "user.login", {"phone": "13612345678"}))["payload"]["user"]
reservation = request("smoke-reservation", "reservation.create", {"user_id": reservation_user["id"], "pile_id": 101})
created = exchange(reservation)
assert created["type"] == "reservation.create.result"
assert created["payload"]["order"]["status"] == "pending_reservation"
assert created["payload"]["pile"]["status"] == "reserved"
assert exchange(reservation) == created
assert_error(exchange(request("smoke-reservation-duplicate", "reservation.create", {"user_id": reservation_user["id"], "pile_id": 101})), 1201)
assert_error(exchange(request("smoke-reservation", "reservation.create", {"user_id": reservation_user["id"], "pile_id": 102})), 1201)
order_id = created["payload"]["order"]["id"]
confirm = request("smoke-confirm", "reservation.confirm", {"user_id": reservation_user["id"], "order_id": order_id})
confirmed = exchange(confirm)
assert confirmed["payload"]["order"]["status"] == "reserved"
assert exchange(confirm) == confirmed
cancel = request("smoke-cancel", "reservation.cancel", {"user_id": reservation_user["id"], "order_id": order_id})
cancelled = exchange(cancel)
assert cancelled["payload"]["order"]["status"] == "cancelled"
assert cancelled["payload"]["pile"]["status"] == "idle"
assert exchange(cancel) == cancelled

# Frozen users are rejected before pile allocation and direct start is valid.
frozen = exchange(request("smoke-login-frozen", "user.login", {"phone": "13700137000"}))["payload"]["user"]
assert frozen["status"] == "frozen"
assert_error(exchange(request("smoke-frozen-profile-get", "user.profile.get", {
    "user_id": frozen["id"]
})), 1100)
assert_error(exchange(request("smoke-frozen-profile-update", "user.profile.update", {
    "user_id": frozen["id"], "nickname": "不应写入"
})), 1100)
assert_error(exchange(request("smoke-frozen-recharge", "wallet.recharge", {
    "user_id": frozen["id"], "amount_cents": 100
})), 1100)
assert database_row("SELECT nickname, balance_cents FROM users WHERE id = ?", (frozen["id"],)) \
    == ("演示冻结用户", 0)
frozen_error = assert_error(exchange(request("smoke-frozen-reservation", "reservation.create", {"user_id": frozen["id"], "pile_id": 101})), 1201)
assert "frozen" in frozen_error["message"]

direct_user = exchange(request("smoke-login-direct", "user.login", {"phone": "13512345678"}))["payload"]["user"]
direct = request("smoke-direct-start", "charging.start", {"user_id": direct_user["id"], "pile_id": 101})
direct_started = exchange(direct)
assert direct_started["type"] == "charging.start.result"
assert direct_started["payload"]["order"]["status"] == "charging"
direct_order_id = direct_started["payload"]["order"]["id"]
direct_stop = request("smoke-direct-stop", "charging.stop", {"user_id": direct_user["id"], "order_id": direct_order_id})
direct_stopped = exchange(direct_stop)
assert direct_stopped["payload"]["order"]["status"] == "pending_settlement"
assert_error(exchange(request("smoke-direct-settle", "charging.settle", {"user_id": direct_user["id"], "order_id": direct_order_id})), 1202)
direct_active = exchange(request("smoke-direct-active", "order.active.get", {"user_id": direct_user["id"]}))
assert direct_active["payload"]["order"]["status"] == "pending_settlement"
direct_after_failure = exchange(request("smoke-direct-login-after-failure", "user.login", {"phone": "13512345678"}))
assert direct_after_failure["payload"]["user"]["balance_cents"] == 0
direct_pile = exchange(request("smoke-direct-pile", "pile.list", {"station_id": 1}))
assert any(pile["id"] == 101 and pile["status"] == "charging" for pile in direct_pile["payload"]["piles"])

# Valid frames already parsed in a batch are dispatched before malformed input.
batch = exchange_batch([
    frame(request("smoke-good", "echo", {"value": 8})),
    b"\x00\x00\x00\x01!",
])
assert [response["id"] for response in batch] == ["smoke-good", "server"]
assert batch[0]["payload"]["value"] == 8
assert batch[1]["payload"]["code"] == 1001
print("server smoke test passed")
