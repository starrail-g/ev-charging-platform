# API Reference

The Socket API uses the protocol defined in
[`docs/architecture/protocol.md`](../architecture/protocol.md). Examples use
compact JSON for readability. IDs are integers, money is in Chinese fen, and
timestamps are UTC ISO-8601 strings.

Every state-changing request must use a client-generated `id` (1 to 64
characters). Replaying the same ID with the same operation and identifying
payload returns the original successful response, including after reconnect.
Reusing an ID for another operation or payload returns `CONFLICT` (`1201`).
The idempotent operations are `reservation.create`, `reservation.confirm`,
`reservation.cancel`, `charging.start`, `charging.stop`, and
`charging.settle`.

## Login and Queries

Login accepts an 11-digit ASCII phone number. It reads an existing user or
creates an active user with zero balance atomically:

```json
{"v":1,"id":"login-1","type":"user.login","payload":{"phone":"13912345678"}}
```

```json
{"v":1,"id":"login-1","type":"user.login.result","payload":{"user":{"id":1,"phone":"13912345678","nickname":"用户5678","avatar_path":null,"balance_cents":0,"status":"active"}}}
```

Invalid input returns `INVALID_REQUEST` (`1002`); database open,
initialization, or write failures return `DATABASE_ERROR` (`1300`).

List active stations:

```json
{"v":1,"id":"station-1","type":"station.list","payload":{}}
```

```json
{"v":1,"id":"station-1","type":"station.list.result","payload":{"stations":[{"id":1,"name":"软件园一号站","address":"沈阳市浑南区软件园路1号","latitude":41.7192,"longitude":123.4315,"status":"active"}]}}
```

List piles for one station:

```json
{"v":1,"id":"pile-1","type":"pile.list","payload":{"station_id":1}}
```

The result payload contains `piles`; each pile includes `id`, `station_id`,
`pile_code`, `pile_type`, `power_kw`, `unit_price_cents_per_kwh`, and
`status`. Pile status is one of `idle`, `reserved`, `charging`, `fault`, or
`offline`.

Active and historical orders:

```json
{"v":1,"id":"active-1","type":"order.active.get","payload":{"user_id":1}}
{"v":1,"id":"history-1","type":"order.history.list","payload":{"user_id":1}}
```

`order.active.get.result` has `payload.order`, an order object or JSON `null`.
`order.history.list.result` has `payload.orders`, ordered newest first. An
order contains its IDs, status, reservation/charging timestamps, energy,
price, service fee, total, settlement timestamp, and audit timestamps.

## Reservation

Create a reservation on an idle pile:

```json
{"v":1,"id":"reserve-1","type":"reservation.create","payload":{"user_id":1,"pile_id":101}}
```

The result contains `order` with status `pending_reservation` and `pile` with
status `reserved`. Use the returned `order.id` to confirm it:

```json
{"v":1,"id":"confirm-1","type":"reservation.confirm","payload":{"user_id":1,"order_id":2001}}
```

The confirmed order has status `reserved`. A pending or confirmed reservation
can be cancelled, releasing its pile:

```json
{"v":1,"id":"cancel-1","type":"reservation.cancel","payload":{"user_id":1,"order_id":2001}}
```

The cancel result contains a `cancelled` order and an `idle` pile.

## Charging and Settlement

Start a confirmed reservation:

```json
{"v":1,"id":"start-1","type":"charging.start","payload":{"user_id":1,"order_id":2001}}
```

Direct start is also supported by providing an idle `pile_id` instead of an
`order_id`:

```json
{"v":1,"id":"direct-start-1","type":"charging.start","payload":{"user_id":1,"pile_id":101}}
```

Both forms atomically return `charging.start.result` with an order and a pile,
both in the `charging` state. Stop changes the order to
`pending_settlement`:

```json
{"v":1,"id":"stop-1","type":"charging.stop","payload":{"user_id":1,"order_id":2001,"ended_at":"2026-09-01T10:15:00Z"}}
```

The result contains the order and `estimated_amount_cents`. Settle it with:

```json
{"v":1,"id":"settle-1","type":"charging.settle","payload":{"user_id":1,"order_id":2001}}
```

Settlement atomically writes the charge ledger, deducts the wallet, marks the
order `completed`, and releases the pile. A completed order has both
`ended_at` and `settled_at`.

## Errors and Rules

All failures use `type: "error"` and retain the request `id`:

```json
{"v":1,"id":"settle-1","type":"error","payload":{"code":1202,"name":"INSUFFICIENT_BALANCE","message":"insufficient balance"}}
```

Insufficient balance leaves the order in `pending_settlement`, the pile in
`charging`, and the wallet balance and charge ledger unchanged. Invalid state
transitions, duplicate active orders, and occupied or faulted piles return
`CONFLICT` (`1201`):

```json
{"v":1,"id":"start-1","type":"error","payload":{"code":1201,"name":"CONFLICT","message":"order is not reserved"}}
```

Frozen users cannot create or confirm reservations, start charging, or settle
an order. `NOT_FOUND` (`1200`) is used for missing users, orders, stations, or
piles. `DATABASE_ERROR` (`1300`) indicates persistence failure and the
affected transaction is rolled back. The diagnostic `health` and `echo`
operations remain available.
