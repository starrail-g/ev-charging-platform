# Current Project State

## Stage and source of truth

- Project: 东软电动汽车充电桩应用管理平台。
- Stage I deadline: 2026-09-10 24:00; Stage II deadline: 2026-09-17 24:00; personal report deadline: 2026-09-18 24:00.
- Requirements baseline: `docs/requirements/requirements-matrix.md`.
- Runtime contracts: `docs/architecture/protocol.md`, `docs/architecture/database.md`, and `docs/architecture/map-service-protocol.md`.
- This contract branch is based on GitHub `main` commit `4242e1f`, which includes merged PR #11. PR #13 remains an open, compatible proposal for `admin.pile.list`.

## Fixed architecture and collaboration rules

- A owns `apps/user-client`; B owns `server`, `libs/protocol`, `libs/database`, and `database`; C owns `apps/admin-client` and `dashboard`.
- Clients communicate through Socket Protocol v1 and never access runtime SQLite directly.
- Qt/C++ build, test, acceptance, and release use `qmake6` only; CMake is not authoritative.
- Every code or architecture change updates this file and the corresponding formal documentation while removing stale detail.
- Work is delivered through task branches and Pull Requests; do not push or force-push directly to `main`.

## Current delivered baseline

- B Schema v0.3 and Socket Protocol v1 implement user login/profile/wallet, station/pile reads, order/reservation/charging lifecycle, administrator authentication, statistics, and management operations.
- A user client has a deterministic Mock path and an opt-in `SocketUserService`; A-S1-04 coordinated cross-module regression and clean-environment evidence remain pending.
- C PR #11 is merged into `main`; the management UI, Socket adapter, dashboard, tests, and delivery documents are present.
- Existing charging consistency remains authoritative: `reserved` and `charging` are business states controlled by order transactions; stop releases the pile before settlement; request-ID replay protects state-changing operations.

## Accepted map-service contract

- `docs/architecture/map-service-protocol.md` freezes a Protocol v1-compatible target contract for server-side Tencent Maps and simulated charging piles.
- Tencent WebService credentials will be server-only. The user client will call `map.station.search` and `map.route.plan`, then draw returned markers and route geometry locally without loading credential-bearing Tencent JavaScript.
- `station.list` remains a database-only read. `pile.list` keeps its request shape and gains optional snapshot/source metadata. `admin.map.audit.list` provides authenticated, sanitized operational evidence.
- Tencent POIs contribute only station identity and location. The service persists each `provider + provider_poi_id` once and generates 4–12 deterministic demo piles only on first import.
- Simulation may change only eligible simulated piles among `idle`, `fault`, and `offline`. It skips active orders and never invents `reserved` or `charging`.
- Map queries, upstream outcomes, cache use, and pile-state transitions retain sanitized records for 30 days. Keys, complete credential-bearing URLs, raw upstream JSON, and long-term precise user locations are forbidden.
- The extension is compatible with PR #13: `admin.pile.list` remains an authenticated cross-station inventory read and does not trigger map synchronization or simulation.

## Map implementation status and dependencies

- Contract status: documented on branch `docs/map-server-pile-simulation-contract` for review.
- Not implemented: Schema v0.4 migration, server Tencent adapter, cache/audit tables, stable pile generator, simulation timer, new Socket handlers, and client adaptation.
- Current Schema remains v0.3; this documentation change is not v0.4 runtime evidence.
- The separate A-S2-01 client-side Tencent prototype is implementation reference only. It is not part of this contract PR and must later be replaced by the server Socket boundary.
- Required implementation order: Schema v0.4 migration → server map adapter/cache/audit → pile generator/simulator → protocol/database/TCP tests → A client adaptation → optional C audit display.

## Active tasks and risks

- A: complete A-S1-04 coordinated regression, then replace direct Tencent calls after B implements the accepted map handlers; retain explicit cache/Mock/offline labels.
- B: implement the accepted map contract without blocking reservation/charging transactions; keep slow HTTP and database work off the Socket event-loop thread.
- C: keep `admin.pile.list` field-compatible and optionally consume `admin.map.audit.list`; no C runtime change is required by this contract PR.
- S2 intelligent analysis remains a separate chain: data preparation → model contract → prediction/recommendation/warning → service adaptation → client/dashboard display → integrated validation.
- External API quota, network, permission, and malformed-response failures require cache/Mock degradation or stable 1400–1408 errors.
- The previously disclosed Tencent credential must be rotated before demonstration or release and must never be committed.

## Validation boundary for this PR

- This is a documentation-only contract change: no server handler, user-client code, schema, migration, timer, runtime database, or generated build output is included.
- Validation covers cross-document operation/status/error consistency, PR #13 compatibility, Markdown/link inspection, `git diff --check`, changed-file scope review, and credential scanning.
- Runtime qmake6, database, and TCP results belong to later implementation PRs and must not be fabricated here.
