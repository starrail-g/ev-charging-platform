# Current Project State

## Overview

Project: 东软电动汽车充电桩应用管理平台。

Current stage: stage-I integration of the B backend lifecycle, A user-client
Mock baseline, and C admin-client skeleton. The product source of truth is the
requirements matrix and project specification; no Mock path is evidence of a
real Socket/SQLite client integration.

## Status

- qmake6 is the authoritative Qt/C++ build path.
- B currently provides SQLite schema v0.2, deterministic seed/migration,
  protocol v1 framing/envelope/error codes, and server handlers for login,
  profile read/update, wallet recharge, station/pile queries, active/history
  orders, reservation, charging and settlement.
- Lifecycle writes use `BEGIN IMMEDIATE`, request-ID replay records, frozen-user
  checks, direct idle-pile charging, and settlement rollback paths.
- Profile updates persist nickname/avatar/timestamps. Recharge atomically
  updates the non-negative integer-cent balance, writes a recharge ledger row,
  and stores the replay response. Injected UPDATE/INSERT failures prove full
  rollback; frozen-user checks take precedence over successful replay.
- A user client is a deterministic Qt Widgets + Mock implementation. It has no
  `SocketUserService`; real DTO/protocol integration remains pending.
- C admin client has a qmake shell, repository boundary, Mock data source,
  login flow and overview states. Real management APIs remain pending.
- The clean-database server path can load `EV_DATABASE_SEED_PATH` once during
  initial creation; existing databases are not reseeded.

## Architecture

```text
Qt user/admin clients -> protocol v1 / Socket -> server -> database layer -> SQLite
dashboard and ML consume separately defined data interfaces
```

Presentation code does not access SQLite directly. `libs/protocol` owns wire
contracts, `libs/database` owns persistence and transactions, and `server`
owns Socket dispatch and error mapping.

## TODO

- [ ] Add a follow-up migration for already-deployed v0.2 databases if the
  project needs in-place rollout; fresh schema and v0.1->v0.2 migration now
  enforce the state/time constraints.
- [x] Settlement includes `service_fee_cents` using the documented integer rule
  (`ceil(energy_wh * unit_price / 1000) + service_fee`).
- [x] History is formally completed-only, newest-first by `settled_at`, with
  `station_name`, `station_address` and `pile_code` display fields.
- [x] Cancellation verifies the order/pile/user relationship in the release
  update; request-ID global scope and failed-request retry semantics are
  documented.
- [x] Reproducible concurrent reservation/settlement tests and isolated smoke
  runs cover profile, recharge, lifecycle, malformed-frame, and injected
  mid-transaction SQL failure paths.
- [ ] Move slow database work off the Socket event-loop thread or define a
  bounded worker/lock strategy.
- [ ] Implement administrator/statistics/management APIs, Socket adapters,
  dashboard and intelligent-analysis pipeline.

## Known Issues

- Existing in-progress orders are not automatically closed when an
  administrator freezes a user; freeze currently blocks later lifecycle calls.
- Existing databases already initialized at v0.2 do not receive the new
  timestamp checks automatically; an in-place follow-up migration is needed
  before production rollout.
- Database calls are synchronous in the Qt Socket thread.
- Real A Socket/SQLite integration and administrator server handlers are not
  implemented.

## Decisions

- Keep B as owner of `server`, `libs/protocol`, `libs/database` and `database`;
  A owns the user client; C owns the admin client/dashboard.
- Request IDs are retained in the database for successful state-changing
  responses; a future protocol revision must explicitly define global scope,
  retention and failed-request replay rules.
- Request IDs are currently globally unique database keys across users and
  operations; clients must not reuse an ID for another request.
- Revenue reports use `settled_at` because revenue is final at settlement;
  `ended_at` remains the physical charging-end timestamp.
- Build output and local process material stay outside the repository. Real
  credentials and runtime databases are never committed.

## Recent History

- Synced PR #4 with `origin/main` at `84911db`; retained A user-client files
  and resolved configuration/state-document merge conflicts.
- Verified the latest review: direct start and frozen-user protections exist;
  state/time constraints, service-fee calculation, concurrency evidence and
  history contract details were corrected in this work; injected SQL failure
  coverage and asynchronous database dispatch remain open.
- Added `user.profile.get`, `user.profile.update`, and `wallet.recharge` with
  atomic persistence, replay, failure-injection rollback tests, and API docs;
  validated qmake6 server/protocol/user-client builds, smoke, and concurrency.
- Earlier work added direct start, frozen-user guards, request replay,
  `order.history.list`, seed-on-empty startup and migration failure-path tests.
