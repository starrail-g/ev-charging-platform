# Socket Protocol v1

## Status and Scope

This document defines the initial shared TCP contract for the Qt user
client, Qt administration client, and `server`. It is the authoritative
contract for `libs/protocol`.

Version 1 freezes framing, common fields, error responses, and operation
names needed for the first project stage. The current server implements the
read/query and user charging lifecycle operations (`health`, `echo`,
`user.login`, `station.list`, `pile.list`, `order.active.get`,
`order.history.list`, `reservation.*`, and `charging.*`). Administrator,
profile, and wallet recharge operations remain contracted and will be
implemented against the database service.

## Transport and Framing

- Transport: TCP. The development default is `127.0.0.1:45454`.
- One TCP connection may carry multiple frames. Clients must not assume one
  `readyRead` event or `recv` call equals one message.
- A frame is a four-byte unsigned big-endian payload length followed by the
  payload. The length does not include its four-byte prefix.
- Payload: UTF-8 JSON object, max 1 MiB. Zero-length and over-limit frames
  are invalid; the server sends an `error` where possible and closes only
  that connection.
- If a TCP read contains valid frames followed by a malformed frame, the
  decoder returns the valid messages together with the error. The server
  dispatches those messages before sending the error and closing the session.
- The initial server processes a connection's requests in received order.
  Request IDs allow clients to correlate responses and will support later
  asynchronous work. Clients must generate unique IDs while a connection is
  active and must not retry a state-changing request with a different ID.

Example wire shape (spaces added for readability):

```text
00 00 00 2f {"v":1,"id":"a-1","type":"health","payload":{}}
```

## Envelope

Every request and response is a JSON object with exactly these common
fields. Unknown top-level fields may be ignored for forward compatibility.

| Field | Type | Rule |
|---|---|---|
| `v` | integer | Required protocol version. v1 is `1`. |
| `id` | string | Required request ID, 1 to 64 characters. Responses echo it. |
| `type` | string | Required operation or result type, 1 to 64 characters. |
| `payload` | object | Required operation-specific object; use `{}` when empty. |

Request:

```json
{"v":1,"id":"user-42","type":"health","payload":{}}
```

Successful response:

```json
{"v":1,"id":"user-42","type":"health.result","payload":{"status":"ok","service":"ev-server"}}
```

Error response:

```json
{"v":1,"id":"user-42","type":"error","payload":{"code":1002,"name":"INVALID_REQUEST","message":"unsupported request type: example"}}
```

Malformed frames without a usable request ID use `id: "server"`.

## Error Codes

| Code | Name | Meaning |
|---:|---|---|
| 0 | `OK` | Successful operation. |
| 1000 | `INVALID_FRAME` | Invalid length prefix or incomplete/invalid framing. |
| 1001 | `INVALID_JSON` | Payload cannot be parsed as a JSON object. |
| 1002 | `INVALID_REQUEST` | Envelope or operation payload is invalid, or type is unsupported. |
| 1003 | `UNSUPPORTED_VERSION` | `v` is not supported by this server. |
| 1100 | `UNAUTHORIZED` | Missing, invalid, or insufficient authentication. |
| 1200 | `NOT_FOUND` | Requested resource does not exist. |
| 1201 | `CONFLICT` | State transition is not permitted, including duplicate active orders. |
| 1202 | `INSUFFICIENT_BALANCE` | Wallet balance cannot cover settlement. |
| 1300 | `DATABASE_ERROR` | Persistence failure; server must roll back the affected transaction. |
| 1500 | `INTERNAL_ERROR` | Unexpected server failure. |

The `message` field is suitable for display/logging but clients must branch
on `code`, never on message text.

## Operation Contract

The following names and payloads are reserved for v1. Result types append
`.result`; all failures use `type: "error"`.

| Request type | Request payload | Success payload/result type |
|---|---|---|
| `health` | `{}` | `health.result`: `status`, `service` |
| `echo` | any object | `echo.result`: same object (diagnostic only) |
| `user.login` | `phone` (11 digit string) | `user.login.result`: `user` |
| `user.profile.get` | `user_id` | `user.profile.get.result`: `user` |
| `user.profile.update` | `user_id`, optional `nickname`, `avatar_path` | `user.profile.update.result`: `user` |
| `wallet.recharge` | `user_id`, `amount_cents` (>0 integer) | `wallet.recharge.result`: `balance_cents`, `transaction_id` |
| `station.list` | optional location/filter fields | `station.list.result`: `stations` |
| `pile.list` | `station_id` | `pile.list.result`: `piles` |
| `order.active.get` | `user_id` | `order.active.get.result`: `order` or `null` |
| `reservation.create` | `user_id`, `pile_id` | `reservation.create.result`: `order` (`pending_reservation`), `pile` (`reserved`) |
| `reservation.confirm` | `user_id`, `order_id` | `reservation.confirm.result`: `order` (`reserved`) |
| `reservation.cancel` | `user_id`, `order_id` | `reservation.cancel.result`: `order` (`cancelled`), `pile` (`idle`) |
| `charging.start` | `user_id`, `order_id` for a reservation, or `pile_id` for direct start | `charging.start.result`: `order`, `pile` (`charging`) |
| `charging.stop` | `user_id`, `order_id`, optional `ended_at` | `charging.stop.result`: `order`, `estimated_amount_cents` |
| `charging.settle` | `user_id`, `order_id` | `charging.settle.result`: `order`, `balance_cents` |
| `admin.login` | `username`, `password` | `admin.login.result`: `admin` |
| `admin.statistics.get` | `range` (`7d` or `30d`) | `admin.statistics.get.result`: `statistics` |
| `admin.station.list` | optional `query` | `admin.station.list.result`: `stations` |
| `admin.station.create` | `name`, `address`, `latitude`, `longitude`, `pile_count` | `admin.station.create.result`: `station` |
| `admin.pile.restart` | `pile_id` | `admin.pile.restart.result`: `pile` |
| `admin.user.list` | optional `phone_query` | `admin.user.list.result`: `users` |
| `admin.user.status.set` | `user_id`, `status` (`active` or `frozen`) | `admin.user.status.set.result`: `user` |

Unless an operation says otherwise, object IDs are integers. Money is always
an integer number of Chinese fen (`*_cents`), never a floating-point yuan
value. Timestamps are UTC ISO-8601 strings such as
`2026-09-01T10:15:00Z`.

The `user` object must at least contain `id`, `phone`, `nickname`,
`balance_cents`, and `status`. The `station`, `pile`, `order`, `admin`, and
`statistics` fields are finalized with the database schema and documented in
the API reference before their handlers are enabled.

Pile status values are `idle`, `reserved`, `charging`, `fault`, and `offline`.
Order status values are `pending_reservation`, `reserved`, `charging`,
`pending_settlement`, `completed`, `cancelled`, and `exception`.

## State and Failure Rules

- A user with an unfinished order cannot create another reservation or start
  another charging order.
- Creating a pending reservation atomically changes an idle pile to
  `reserved`; confirmation changes only the order to `reserved`. Starting or
  cancelling a reservation atomically changes both order and pile. Fault or
  occupied piles return `CONFLICT`.
- Every state-changing request must carry a client-generated `id`. The
  database stores the operation, a fingerprint of its identifying payload,
  and the complete successful response in `request_records`. Replaying the
  same `id` with the same operation and identifying payload returns the
  original result, including after a reconnect. Reusing an `id` for another
  operation or different identifying payload returns `CONFLICT`; clients must
  not retry the same action with a new ID.
- Idempotency currently covers `reservation.create`,
  `reservation.confirm`, `reservation.cancel`, `charging.start`,
  `charging.stop`, and `charging.settle`. Read operations do not require
  persistence records.
- Settlement must atomically update the order, pile, wallet balance, and
  wallet transaction. Insufficient balance returns `INSUFFICIENT_BALANCE`
  without partial updates.
- `charging.start` accepts either a confirmed reservation `order_id` or an
  idle `pile_id` for direct start. The direct path creates the charging order
  and changes the pile from `idle` to `charging` in one transaction.
- Frozen users cannot create or confirm reservations, start charging, or
  settle an order. A user must be `active` for each of those operations;
  existing in-progress orders are not silently resumed after a freeze.
- A client connection is not an authentication session in v1. Until a
  token/session design is explicitly added, handlers verify the IDs and
  credentials supplied by their payloads. Administrative request access is
  restricted by the corresponding verified administrator context when that
  handler is implemented.

## Current Implementation

`libs/protocol` implements envelope validation and incremental frame
encoding/decoding. `server` accepts multiple TCP clients and currently
implements `health`, `echo`, `user.login`, station/pile, active-order and
order-history queries, reservation transitions, and charging
start/stop/settlement; unknown operations return `INVALID_REQUEST`.
When a read contains valid messages before a malformed frame, the server
dispatches the valid messages before returning the frame error and closing
that connection.
The server dispatches directly to the shared SQLite-backed database service;
clients still depend only on this wire contract and never access SQLite.
