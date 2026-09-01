# Database Architecture

## Scope and conventions

`database/schema/schema.sql` defines SQLite schema version `0.1` for the
server-side source of truth. The schema is deliberately small enough for the
stage-I demo while retaining the entities required by the requirements and
the backend traceability matrix.

- Every connection must execute `PRAGMA foreign_keys = ON`.
- Timestamps are UTC ISO-8601 text (`YYYY-MM-DDTHH:MM:SSZ`); clients format
  them for local display.
- Monetary values are integer Chinese fen (`*_cents`), never floating point.
- `schema_meta.schema_version` identifies the schema; future incompatible
  changes require a migration and version increment.
- `database/seeds/dev.sql` is deterministic and may be run repeatedly.

## Entities

| Table | Purpose | Important rules |
|---|---|---|
| `users` | Phone login, profile, wallet balance, freeze state | Phone is exactly 11 digits; `active`/`frozen`; balance cannot be negative |
| `administrators` | Management authentication and role | Seed account `admin` / `123456`; only a SHA-256 digest is stored |
| `stations` | Charging station identity and location | Latitude/longitude range checks; `active`/`inactive` |
| `charging_piles` | Physical/simulated pile inventory and live state | Unique code per station; `idle`, `charging`, `fault`, `offline`; positive rate/power |
| `charging_orders` | Reservation, charging, billing and settlement lifecycle | `reserved` -> `charging` -> `pending_settlement` -> `completed`, or `cancelled` |
| `wallet_transactions` | Append-only wallet ledger | Positive recharge/refund, negative charge; one charge entry per order |
| `pile_restart_logs` | Auditable remote restart attempts | Result is `succeeded`, `rejected` or `failed` |

`station_pile_status` and `revenue_daily` are read-only views for management
and dashboard queries. They derive values from source tables and must not be
written directly.

## State and consistency constraints

The partial unique indexes `ux_orders_one_active_user` and
`ux_orders_one_active_pile` prevent two active orders for one user or pile.
The service should still query first to produce the protocol's stable business
error (for example duplicate-order or pile-state conflict) and translate a
unique-index violation to the same error.

Only an `idle` pile may enter a new `reserved`/`charging` order. A frozen user
cannot start a new order. Fault/offline piles are unavailable to users.
These cross-row rules belong in the service transaction because SQLite CHECK
constraints cannot inspect another table.

## Required transaction boundaries

1. **Phone login/auto-registration**: begin transaction, select by phone,
   insert a default nickname if absent, commit, then return the stable user ID.
2. **Recharge**: validate a positive amount, lock the logical user row by
   updating it in the transaction, insert a `recharge` ledger row with an
   idempotency key, update `users.balance_cents`, commit. A duplicate key is a
   no-op/replay response.
3. **Start charging**: validate user status, unfinished-order absence and pile
   state; begin `IMMEDIATE` transaction; update pile `idle` -> `charging` and
   insert order (`reserved` then `charging` if reservation is represented);
   commit or roll back all writes.
4. **End and settle**: calculate amount from the order's stored unit rate and
   energy; begin `IMMEDIATE`; verify order is `pending_settlement` and balance
   is sufficient; insert one negative `charge` ledger row, decrement balance,
   update order to `completed` with `settled_at`, update pile to `idle` and
   increment pile counters; commit. Any failure rolls back every change.
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

The seed provides two stations, five piles covering all displayed states, one
completed order for revenue, one active order for unfinished-order checks, a
frozen user, wallet ledger rows and a restart audit event.
