# Current Project State

## Overview

Project: 东软电动汽车充电桩应用管理平台。

Current stage: B backend lifecycle and the unified admin/dashboard UI have been
merged into `main`; the project is now in stage-I cross-module integration with
the A user-client Mock baseline and C admin-client UI/data Mock baseline.
Stage-I target date is 2026-09-10;
stage-II target date is 2026-09-17, with the individual report due 2026-09-18.
The product source of truth is the requirements matrix and project
specification; no Mock path is evidence of a real Socket/SQLite client
integration.

The three files at the repository root named `01.项目说明书-东软电动汽车充电桩应用管理平台.doc`,
`01需求矩阵-第10组-张之杰.xls`, and `三人分工.md` are local reference material
and are intentionally outside the upload/publish set. The tracked Markdown
requirements and architecture documents under `docs/` are the repository's
formal project artifacts.

## Status

- qmake6 is the authoritative Qt/C++ build path.
- B currently provides SQLite schema v0.3, deterministic seed/migration,
  protocol v1 framing/envelope/error codes, and server handlers for login,
  profile read/update, wallet recharge, station/pile queries, active/history
  orders, reservation, charging and settlement.
- Lifecycle writes use `BEGIN IMMEDIATE`, request-ID replay records, frozen-user
  checks, direct idle-pile charging, and settlement rollback paths.
- PR #4 is merged into `main` at `20a3815`; remote `main` advanced to `994e5ff`
  with the restored unified admin/dashboard UI and associated tests. Future
  work must start from the updated `main` on a new task branch.
- Profile updates persist nickname/avatar/timestamps. Recharge atomically
  updates the non-negative integer-cent balance, writes a recharge ledger row,
  and stores the replay response. Injected UPDATE/INSERT failures prove full
  rollback; successful idempotent replay takes precedence over frozen checks.
- A user client is a deterministic Qt Widgets + Mock implementation. It has no
  `SocketUserService`; real DTO/protocol integration remains pending.
- C admin client now has the restored unified day/night UI, qmake build, async
  Repository boundary, Mock data source, overview/pile/station/user pages and
  UI tests. It still has no real Socket repository.
- `admin.statistics.get` now returns `revenue_daily` with exactly 7 or 30 UTC
  calendar-day rows (zero-filled), and aggregate revenue/order/energy values
  are sums of that returned series. The real Socket adapter still needs to map
  this series into C/Dashboard models.
- Station utilization is now calculated by one shared seven-day UTC method:
  charging interval intersections over every pile's available period from
  `created_at`; `fault`/`offline` time remains in the denominator. The method
  is reused by `admin.station.list` and `admin.statistics.get`; station rows
  expose `utilization`/`utilization_range`, and the global average is the
  unweighted mean of station values.
- Administrator restart semantics are now covered by the server smoke suite:
  only `fault`/`offline` piles recover to `idle`; `idle`/`reserved`/`charging`
  are rejected without mutating the pile or active order. Successful restart
  replay and failed-request retry are verified against a clean seeded database.
- Cross-team gate: A retains Mock/offline fallback until a real Socket adapter
  is verified; B's administrator endpoints now exist, while C's real Socket
  repository and end-to-end management integration remain pending.
- The clean-database server path can load `EV_DATABASE_SEED_PATH` once during
  initial creation; existing databases are not reseeded.
- Current checkout is `feature/admin-api` based on `origin/main` at `994e5ff`.
  Administrator API and daily statistics work are local branch changes; the
  previous feature branch remains at `3189e24`.
- Local verification on 2026-09-04/05 used qmake6/Qt 6.2.4 and external `/tmp`
  build directories: protocol tests passed; user-client app and QtTest passed
  (6); admin app build, launch smoke (6), and login-flow tests (7) passed;
  database Python tests passed (10); and server smoke plus concurrency passed
  against a clean seeded v0.3 database. After syncing `origin/main`, admin UI
  build and tests still pass; database/config/token tests pass (10/3/6).
  Dashboard Node tests could not run because `node` is unavailable. `sqlite3`
  CLI is not installed in this environment.
- Main-branch A/C documentation is retained as collaboration context: A's
  Mock user flow remains separate from real Socket integration, while C's
  admin/dashboard work remains dependent on frozen B contracts.

## Architecture

```text
Qt user/admin clients -> protocol v1 / Socket -> server -> database layer -> SQLite
dashboard and ML consume separately defined data interfaces
```
- The unified day/night UI milestone `T-C1.1` (Qt admin + Web dashboard) is part
  of the current `origin/main` baseline. This branch adds the B-side administrator
  APIs and keeps the UI's Mock Repository boundary intact until Socket integration.

Presentation code does not access SQLite directly. `libs/protocol` owns wire
contracts, `libs/database` owns persistence and transactions, and `server`
owns Socket dispatch and error mapping.

## Next Development Plan

1. **B administrator APIs**: maintain `admin.login`, statistics with daily
   revenue series, station/pile queries and mutation endpoints, and user
   list/status changes while resolving the remaining auth/session contract.
2. **A real Socket adapter**: implement `SocketUserService` against Protocol
   v1 for login, profile, wallet, station/pile, order history, reservation,
   charging and settlement; retain Mock/offline fallback.
3. **Cross-module integration**: run A/B/C against a clean v0.3 database and
   verify lifecycle state visibility, error mapping, request replay, disconnect
   handling, and concurrent reservation/settlement.
4. **Delivery evidence**: record qmake6 build commands, server smoke,
   concurrency, end-to-end results, seed/migration commands and remaining
   unimplemented interfaces.
5. **Stage-II follow-up**: move synchronous database work behind a bounded
   worker strategy, then connect dashboard statistics and the ML
   forecast/recommendation/warning pipeline.

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
- [x] Implement server-side administrator login, statistics (including 7/30-day
  zero-filled daily revenue series), station list/create, pile restart, user
  list and user status APIs on `feature/admin-api`; real Qt Socket adapters and
  end-to-end client integration remain open.
- [ ] Implement Socket adapters, dashboard live data integration and intelligent-
  analysis pipeline.
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
- Real A/C Socket/SQLite integration is not implemented; the current admin API
  implementation is server-side and still needs a production client adapter.
- `admin.statistics.get`, `admin.station.list`, and `admin.user.list` are
  currently read-only endpoints without an `administrator_id`; this is a
  temporary v1 exposure and must be resolved before production Socket
  integration (either authenticated session context or an explicit admin ID).
- Administrator API smoke coverage exists in `server/tests/admin.py`; full
  end-to-end management-client tests and a persisted administrator session/token
  design remain open.
- Utilization snapshots are read-only and can advance by seconds between the
  separate station-list and statistics requests; both use the same formula and
  seven-day UTC window.
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
- Station utilization uses order `started_at`/`ended_at` intersections for
  `charging`, `pending_settlement`, and `completed`; an open charging order is
  closed at the snapshot cutoff. The statistics average is station-weighted
  equally, not pile-weighted.
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
- Synced the restored unified admin/dashboard UI from `origin/main` at `994e5ff`.
- Added server-side administrator login, statistics, station/pile management,
  user listing/status APIs, request replay handling, and administrator API smoke
  coverage; aligned `admin.user.list` with the admin UI's `created_at` field.
- Confirmed the 9/4 statistics gap: aggregate-only responses could not render
  trends. Added fixed-length zero-filled UTC daily revenue rows for `7d`/`30d`,
  aggregate consistency assertions, and updated protocol/API/database docs.
- Rebuilt the server and reran administrator smoke on 2026-09-05: both daily
  series lengths and date ordering passed, with aggregates matching row sums.
- Unified seven-day station utilization across the admin station list and
  statistics average; added response-field assertions and documented the
  interval/intersection and created-pile denominator rules.
- Clarified restart as fault recovery only, added four restart/idempotency test
  groups, and corrected administrator idempotency lists in the protocol/API docs.
