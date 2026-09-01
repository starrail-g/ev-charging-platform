# Database Architecture

## Scope and conventions

`database/schema/schema.sql` defines SQLite schema version `0.2` for the
server-side source of truth. The schema is deliberately small enough for the
stage-I demo while retaining the entities required by the requirements and
the backend traceability matrix.

- Every connection must execute `PRAGMA foreign_keys = ON`.
- Timestamps are UTC ISO-8601 text (`YYYY-MM-DDTHH:MM:SSZ`); clients format
  them for local display.
- Monetary values are integer Chinese fen (`*_cents`), never floating point.
- `schema_meta.schema_version` identifies the schema. Existing v0.1 databases
  must apply `database/migrations/001_v0.1_to_v0.2.sql` before use.
- `database/seeds/dev.sql` is deterministic and may be run repeatedly.

## Entities

| Table | Purpose | Important rules |
|---|---|---|
| `users` | Phone login, profile, wallet balance, freeze state | Phone is exactly 11 digits; `active`/`frozen`; balance cannot be negative |
| `administrators` | Management authentication and role | Seed account `admin` / `123456`; only a SHA-256 digest is stored |
| `stations` | Charging station identity and location | Latitude/longitude range checks; `active`/`inactive` |
| `charging_piles` | Physical/simulated pile inventory and live state | Unique code per station; `idle`, `reserved`, `charging`, `fault`, `offline`; positive rate/power |
| `charging_orders` | Reservation, charging, billing and settlement lifecycle | `pending_reservation`, `reserved`, `charging`, `pending_settlement`, `completed`, `cancelled`, `exception` |
| `wallet_transactions` | Append-only wallet ledger | Positive recharge/refund, negative charge; one charge entry per order |
| `pile_restart_logs` | Auditable remote restart attempts | Result is `succeeded`, `rejected` or `failed` |

`station_pile_status` and `revenue_daily` are read-only views for management
and dashboard queries. They derive values from source tables and must not be
written directly. `revenue_daily` groups completed orders by `settled_at`,
because revenue becomes final when wallet settlement succeeds; `ended_at`
remains the physical charging-end timestamp for operational metrics.

## Status transitions

The service is the only component that changes lifecycle states. It validates
the transition and performs order, pile, wallet, and counter updates in one
write transaction.

| Object | State | Allowed next states | Meaning |
|---|---|---|---|
| Pile | `idle` | `reserved`, `charging`, `fault`, `offline` | Available to accept a reservation or direct start |
| Pile | `reserved` | `charging`, `idle`, `fault`, `offline` | Held by one accepted reservation |
| Pile | `charging` | `idle`, `fault`, `offline` | Physical charging session is active |
| Pile | `fault` | `idle`, `offline` | Unavailable; recovery/restart must be audited |
| Pile | `offline` | `idle`, `fault` | Administratively or physically unavailable |
| Order | `pending_reservation` | `reserved`, `cancelled`, `exception` | Reservation request exists and temporarily holds a `reserved` pile |
| Order | `reserved` | `charging`, `cancelled`, `exception` | Reservation accepted and pile is `reserved` |
| Order | `charging` | `pending_settlement`, `exception` | Charging has started and pile is `charging` |
| Order | `pending_settlement` | `completed`, `exception` | Charging ended; charge amount awaits settlement |
| Order | `completed` | none | Financially settled terminal state |
| Order | `cancelled` | none | Cancelled terminal state |
| Order | `exception` | none | Terminal state requiring audit/support handling |

Creating a pending reservation atomically updates its pile from `idle` to
`reserved`; this makes the hold visible to clients and prevents another user
from selecting it. Confirming the reservation changes only the order from
`pending_reservation` to `reserved`. Starting charging changes the order and
pile to `charging` atomically. Cancelling a pending or confirmed reservation
returns its pile to `idle`. A charging or settlement exception places the pile
in `fault` when the device is unsafe; otherwise it returns it to `idle` only
after the service has resolved the physical session.

## State and consistency constraints

The partial unique indexes `ux_orders_one_active_user` and
`ux_orders_one_active_pile` prevent two active orders for one user or pile,
including `pending_reservation` requests. The service should still query first
to produce the protocol's stable business error (for example duplicate-order
or pile-state conflict) and translate a unique-index violation to the same
error.

Only an `idle` pile may be selected for a new reservation or direct charge. A
`pending_reservation` order holds its selected pile in `reserved`. A frozen
user cannot start a new order. Reserved, fault, and offline piles are
unavailable to other users.
These cross-row rules belong in the service transaction because SQLite CHECK
constraints cannot inspect another table.

`charging_orders` CHECK constraints require a `pending_settlement` order to
have `ended_at`, and a `completed` order to have `started_at`, `ended_at`, and
`settled_at`. Its amount columns are non-null integer values, preserving the
settled total and pricing inputs even after tariffs change. The schema also
requires every `charge` wallet entry to reference an order and allows at most
one such entry per order. SQLite cannot express the reverse requirement that a
completed order has a matching ledger row without preventing valid write
ordering with a CHECK alone, so database triggers and the settlement
transaction enforce it: insert exactly one
negative `charge` entry whose `user_id` and absolute `amount_cents` match the
completed order's `user_id` and `total_amount_cents`, using `settled_at` as the
ledger timestamp. The triggers reject completion without that row and prevent
the matching charge row from being deleted or changed afterward. The service
must reject a completed zero-energy/zero-amount
result unless an explicitly supported free-charge policy is introduced.

## Required transaction boundaries

1. **Phone login/auto-registration**: begin transaction, select by phone,
   insert a default nickname if absent, commit, then return the stable user ID.
2. **Recharge**: validate a positive amount, lock the logical user row by
   updating it in the transaction, insert a `recharge` ledger row with an
   idempotency key, update `users.balance_cents`, commit. A duplicate key is a
   no-op/replay response.
3. **Create/confirm reservation and start charging**: begin `IMMEDIATE`,
   validate user status, unfinished-order absence and pile state, then create
   a `pending_reservation` order and update its pile `idle` -> `reserved` in
   the same transaction. Confirmation changes the order to `reserved`.
   Starting charging changes the matching order/pile pair to `charging`.
   Commit or roll back all writes.
4. **End and settle**: calculate amount from the order's stored unit rate and
   energy; begin `IMMEDIATE`; verify order is `pending_settlement` and balance
   is sufficient; insert one negative `charge` ledger row, decrement balance,
   update order to `completed` with non-null `ended_at` and `settled_at`,
   update pile to `idle` and
   increment pile counters; before commit, verify that the ledger row's user
   and absolute amount match the completed order. Any failure rolls back every
   change.
5. **Freeze/unfreeze**: update `users.status` in one transaction. Freezing
   does not cancel an existing order, but blocks new starts.
6. **Remote restart**: begin transaction, verify administrator role and pile
   status, insert `pile_restart_logs`, update restart counters/timestamp and
   optionally clear a recoverable fault. Rejected states still get an audit
   row and no pile mutation.

Use `BEGIN IMMEDIATE` for write transactions so concurrent clients cannot
both claim the same idle pile. Set a busy timeout and map lock/constraint
failures to observable database/business errors at the service boundary.

## Seed and validation

```sh
sqlite3 var/ev-charging.db < database/schema/schema.sql
sqlite3 var/ev-charging.db < database/seeds/dev.sql
```

The seed provides two stations, six piles covering all displayed states,
orders in `pending_reservation`, `reserved`, `charging`, and `exception`, one
completed order for revenue, a frozen user, wallet ledger rows and a restart
audit event.

## Schema upgrades

`schema/schema.sql` initializes new databases only. Because SQLite cannot add
these CHECK constraints in place, an existing v0.1 database must be backed up
and upgraded with `migrations/001_v0.1_to_v0.2.sql` while the server is
stopped. The migration rebuilds the affected tables, preserves valid rows,
recreates indexes/views, changes revenue grouping to `settled_at`, and updates
`schema_meta`. Invalid legacy completed/charge rows cause the transaction to
roll back instead of being silently accepted. Run `PRAGMA foreign_key_check`
after the migration; a successful check returns no rows.
