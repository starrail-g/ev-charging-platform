# Current Project State

## Baseline and fixed decisions

- Project: 东软电动汽车充电桩应用管理平台。
- Current GitHub `main`: `005d6e8` (PR #19 merged; includes the server-side Tencent Maps integration and final POI pagination fix).
- Stage I deadline: 2026-09-10 24:00. Stage II deadline: 2026-09-17 24:00. Personal report deadline: 2026-09-18 24:00.
- Formal requirements source: `docs/requirements/requirements-matrix.md`; role plans: `docs/role-a-delivery-plan.md`, `docs/role-b-map-service-plan.md`, and `docs/role-c-delivery-plan.md`.
- All Qt/C++ build, test, acceptance and release paths use `qmake6`; CMake is forbidden. Build output remains outside the repository.
- A owns `apps/user-client`; B owns server/protocol/database and server-side Tencent Maps; C owns admin client/dashboard. Clients never access runtime SQLite directly.

## Current implementation

- B PR #19 is merged to `main`: Schema v0.4 migration, deterministic map Mock, Tencent WebService adapter, station import and pile generation, map cache/audit, route and station handlers, simulator gateway, provider pagination drain, and related qmake6/fake/live tests are present. Slow upstream/database work still runs in the Socket connection thread; worker isolation, cache-miss coalescing, retention cleanup and production mTLS simulator transport remain open B work.
- A-S1-01 requirements baseline, A-S1-02 Mock user flow and A-S1-03 existing business Socket adapter are complete. A-S1-04 final cross-module regression and release evidence remain pending.
- PR #17 (`feature/user-client-server-map-v3`) carries A's server-owned map client integration. Production flow is `UserWindow -> ServerMapService -> Protocol v1 -> B server -> Tencent/cache/audit`; the client does not read, send, store or display `TENCENT_MAP_KEY`.
- `ServerMapService` maps `map.station.search` and `map.route.plan`, validates coordinates, distance, duration, polyline, `data_source` and structured `warning`, and keeps Socket failures visible. Explicit Mock mode remains deterministic and offline-capable; it is not an implicit Socket fallback.
- User-client map rendering is local/offline `QWebEngineView` content with server-returned markers and geometry. Unmatched POIs remain location-only and cannot enter reservation, charging or billing flows. Business prices, pile counts/status and orders remain from `IUserService`.
- The legacy direct `TencentMapService` is retained only for isolated adapter tests and is excluded from the production application `.pro` path.
- User-client UI uses the admin day-theme tokens and a resizable 21:38 mobile ratio. Socket mode hides Mock/demo hints; server-side `server_mock` is shown only as degraded server data.

## PR #17 validation evidence

- Environment used for prior client evidence: Ubuntu 22.04 VM, Qt 6.2.4, `qmake6`; build directories were outside the repository.
- User-client application qmake build: PASS. Existing user-client QtTest: `11 passed, 0 failed, 4 skipped` (skips require a live B service).
- Map adapter/WebEngine tests: `11 passed, 0 failed, 1 skipped`; the skip is the live server gate because Tencent calls are server-owned.
- Server-map protocol tests: `5 passed, 0 failed`. The mock-enabled runtime integration previously passed address resolution, station search, driving and walking route mapping against the PR #19 server implementation.
- PR #19 final server evidence: qmake6 server build, database schema tests, fake Tencent HTTP tests, map Socket regression, simulator gateway and Python pile-simulator tests passed; final POI pagination coverage drains two upstream pages before server-side paging.
- Required checks before pushing this update: `git diff --check`, credential scan, qmake6 user-client build, user-client QtTest, map-service tests and server-map protocol tests. Do not record a live Tencent result without a fresh server-side key run.

## Open work and risks

- A: run the user client against the merged `main` server, refresh clean-environment and GUI evidence, then complete A-S1-04/A-S2-03. Smart-analysis display remains pending the B/C contract.
- B: move slow map/database work to a bounded worker strategy, add retention cleanup, cache-miss coalescing and production simulator mTLS/scheduler work.
- C: complete remaining admin/dashboard release evidence and final integrated regression.
- Tencent credentials stay in ignored local configuration. Never commit keys, tokens, passwords, complete key-bearing URLs, runtime databases, logs or build output.

## Recent history

- 2026-09-09: PR #19 merged as `005d6e8`; final `8ec4a97` drains Tencent POI provider pages and adds two-page coverage.
- 2026-09-09: PR #17 user-client map integration aligned to PR #19 response metadata, server Mock warning `1410`, route validation and Socket-only credential boundary.
- 2026-09-09: user-client Socket/Mock isolation, day-theme UI and resizable 21:38 window were validated on qmake6.

## Collaboration and security rules

- Work on task branches and deliver through PRs targeting `main`; do not force-push or merge from this task.
- Any code or architecture change must update this file and the relevant formal document, removing stale claims.
- Before pushing run `git status --short`, `git diff --check`, credential scanning and affected qmake6 builds/tests.
