# Current Project State

## Overview

Project: 东软电动汽车充电桩应用管理平台。

Current stage: stage-I integration of the B backend lifecycle, A user-client
Mock baseline, and C admin-client skeleton. Stage-I target date is 2026-09-10;
stage-II target date is 2026-09-17, with the individual report due 2026-09-18.
The product source of truth is the requirements matrix and project
specification; no Mock path is evidence of a real Socket/SQLite client
integration.

## Status

- qmake6 is the authoritative Qt/C++ build path.
- B currently provides SQLite schema v0.3, deterministic seed/migration,
  protocol v1 framing/envelope/error codes, and server handlers for login,
  profile read/update, wallet recharge, station/pile queries, active/history
  orders, reservation, charging and settlement.
- Lifecycle writes use `BEGIN IMMEDIATE`, request-ID replay records, frozen-user
  checks, direct idle-pile charging, and settlement rollback paths.
- Profile updates persist nickname/avatar/timestamps. Recharge atomically
  updates the non-negative integer-cent balance, writes a recharge ledger row,
  and stores the replay response. Injected UPDATE/INSERT failures prove full
  rollback; successful idempotent replay takes precedence over frozen checks.
- A user client is a deterministic Qt Widgets + Mock implementation. It has no
  `SocketUserService`; real DTO/protocol integration remains pending.
- C admin client has a qmake shell, repository boundary, Mock data source,
  login flow and overview states. Real management APIs remain pending.
- Cross-team gate: A retains Mock/offline fallback until a real Socket adapter
  is verified; C's administrator login, statistics, pile, station and user
  management APIs remain dependent on B-side endpoint implementation.
- The clean-database server path can load `EV_DATABASE_SEED_PATH` once during
  initial creation; existing databases are not reseeded.
- Main-branch A/C documentation is retained as collaboration context: A's
  Mock user flow remains separate from real Socket integration, while C's
  admin/dashboard work remains dependent on frozen B contracts.

## Architecture

```text
Qt user/admin clients -> protocol v1 / Socket -> server -> database layer -> SQLite
dashboard and ML consume separately defined data interfaces
```

Presentation code does not access SQLite directly. `libs/protocol` owns wire
contracts, `libs/database` owns persistence and transactions, and `server`
owns Socket dispatch and error mapping.

## TODO

- [x] Add `002_v0.2_to_v0.3.sql` for already-deployed v0.2 databases; it
  replaces the pile uniqueness rule and adds v0.3 replay/state safeguards.
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
- [ ] Complete A-S1-03 real Socket adapter and C's management/data integration
  after endpoint fields and error behavior are frozen.
- [ ] Meet the main-branch integration milestones: real A/C endpoint alignment
  by 2026-09-07 18:00, clean-environment integration evidence by 2026-09-10,
  and stage-II analysis/dashboard expansion by 2026-09-17.

## Known Issues

- Existing in-progress orders are not automatically closed when an
  administrator freezes a user. Frozen accounts can still use read and
  cleanup/settlement operations; only new reservation/start/recharge requests
  are blocked.
- Existing databases initialized at v0.2 must run the v0.2 -> v0.3 migration
  before starting the v0.3 server; the server rejects older versions.
- Database calls are synchronous in the Qt Socket thread.
- Real A Socket/SQLite integration and administrator server handlers are not
  implemented.
- Dashboard and ML remain extension/integration work and must not redefine the
  v1 protocol or SQLite state rules.
- Tencent Maps credentials remain local-only; user-client navigation must keep
  the documented Mock/offline fallback when a key or network is unavailable.

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
- `charging.stop` releases the pile immediately; `charging.settle` only performs
  financial completion and increments counters, leaving replacement sessions
  untouched. User-level active-order uniqueness includes `pending_settlement`,
  while pile-level uniqueness excludes it.
- Frozen policy is explicit: login succeeds with `status=frozen`; new
  reservation/confirm/start/recharge return `ACCOUNT_FROZEN` (1101), while reads,
  profile updates, cancel, stop and settle remain allowed; replay wins first.
- Schema version `0.3` is the current server contract. Migration `001` remains
  the immutable v0.1 -> v0.2 upgrade; migration `002` upgrades deployed v0.2
  databases to the released pile lifecycle and replay constraints.
- Main's collaboration gate remains applicable: before real A/C integration,
  preserve the Mock/offline fallback and record qmake6, smoke, and end-to-end
  evidence from a clean environment.
- Build output and local process material stay outside the repository. Real
  credentials and runtime databases are never committed.

## Recent History

- Synced PR #4 with `origin/main` at `84911db`; retained A user-client files
  and resolved configuration/state-document merge conflicts.
- Verified the latest review: direct start and frozen-user protections exist;
  state/time constraints, service-fee calculation, concurrency evidence and
  history contract details were corrected in this work; stop/settle pile
  semantics and frozen-user policy are now aligned across code, tests and docs;
  asynchronous database dispatch remains open.
- Added `user.profile.get`, `user.profile.update`, and `wallet.recharge` with
  atomic persistence, replay, failure-injection rollback tests, and API docs;
  validated qmake6 server/protocol/user-client builds, smoke, and concurrency.
- Earlier work added direct start, frozen-user guards, request replay,
  `order.history.list`, seed-on-empty startup and migration failure-path tests.
