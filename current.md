# Current Project State

## Baseline and fixed decisions

- Project: 东软电动汽车充电桩应用管理平台。
- Current GitHub `main` baseline: `f5af4a128d2676860fe1e39944ba0e6f617a4300` (PR #16 merged after PR #15; includes PR #11, #12, #13, #15 and #16 work).
- Stage I deadline: 2026-09-10 24:00. Stage II deadline: 2026-09-17 24:00. Personal report deadline: 2026-09-18 24:00.
- Formal requirements source: `docs/requirements/requirements-matrix.md`; role plans: `docs/role-a-delivery-plan.md` and `docs/role-c-delivery-plan.md`.
- All Qt/C++ build, test and acceptance paths use `qmake6`; CMake is forbidden. Build directories stay outside the repository.
- A owns `apps/user-client`; B owns server/protocol/database and the server-side Tencent Maps contract; C owns admin client/dashboard. Clients never access runtime SQLite directly.

## Task status

- `A-S1-01` requirements baseline: complete.
- `A-S1-02` deterministic Qt Widgets Mock user flow: complete.
- `A-S1-03` Socket Protocol v1 user adapter: complete in the merged baseline for the existing user business operations; final cross-module evidence remains part of `A-S1-04`.
- `A-S1-04` coordinated server/user/admin regression and clean-environment delivery: pending.
- `A-S2-01` server-owned map integration: client-side adaptation is implemented in this worktree; B's server runtime, Tencent credential handling, cache/audit, Schema v0.4 and pile simulation remain pending.
- User-client UI follow-up: Socket and Mock modes are strictly isolated, the visual system now matches the admin day theme, and the window preserves a resizable 21:38 mobile ratio. VM regression evidence is pending.
- `A-S2-02` analysis result presentation and `A-S2-03` final regression/materials: pending the frozen B/C contracts and final integrated build.

## User-client map boundary

- Production map flow is `UserWindow -> ServerMapService -> Socket Protocol v1 -> B server -> Tencent WebService/cache/audit`.
- The client uses `map.station.search` for address/coordinate station discovery and `map.route.plan` for routes. Requests carry numeric `user_id`, normalized origin, bounded radius/page size, numeric `station_id`, and distinct `driving`/`walking` modes.
- `ServerMapService` maps server coordinates, distance, duration, polyline, `data_source` and warning fields. Socket mode does not fall back to local Mock data; service failures stay visible. The explicit Mock launch mode remains deterministic and offline-capable.
- Socket mode hides demo hints and Mock-only labels. Contract-level `server_mock` values remain parseable but are shown as redacted server-side backup data.
- The user client reuses the admin day-theme palette and component styling. Its initial 420 x 760 window can scale between 315 x 570 and 840 x 1520 while preserving the 21:38 ratio.
- Business station price, pile count/status and order data continue to come from `IUserService`; Tencent POIs are location context only. Unmatched POIs cannot enter reservation or charging flows.
- The legacy `TencentMapService` remains isolated for adapter tests and is excluded from the production qmake application path. The client does not read, store, print or inject `TENCENT_MAP_KEY`.
- `MapWebView` is a local/offline rendering surface for server-returned geometry and explicit fallback status; it is not a client-side Tencent credential holder.

## Evidence from Ubuntu VM

Environment: Ubuntu 22.04 VM, Qt 6.2.4, `qmake6`; all build directories were outside the repository.

Representative commands:

```bash
qmake6 ~/ev-charging-platform/apps/user-client/user-client.pro
make -j2
qmake6 ~/ev-charging-platform/apps/user-client/tests/user-client-tests.pro
make -j2
QT_QPA_PLATFORM=offscreen ./ev-user-client-tests -txt
```

- User-client application qmake build: PASS.
- Existing user-client QtTest: `11 passed, 0 failed, 4 skipped`; skipped cases require a live B service.
- Server map adapter fake-Protocol test: `4 passed, 0 failed, 0 skipped`.
- Map adapter/WebEngine test: `10 passed, 0 failed, 1 skipped`; WebEngine offline smoke passed, real Tencent integration is intentionally skipped because Tencent calls are server-owned.
- Application startup under `QT_QPA_PLATFORM=offscreen` and WebEngine no-sandbox flags remained alive for the smoke window; no crash was observed.
- UI regression in a clean VM snapshot: qmake6 app build PASS; user tests `11 passed, 0 failed, 4 skipped`; map tests `11 passed, 0 failed, 1 skipped`; server-map tests `4 passed, 0 failed`. Socket login window showed the day theme with demo/Mock hints hidden. X11 resize checks measured 315 x 570 and 399 x 722, preserving the 21:38 ratio.
- Local `git diff --check`: PASS. No real key, local `.env`, runtime database, log, Makefile or build output is part of the intended commit.

## Open work and risks

- B must implement and expose `map.station.search`/`map.route.plan`, server-side Tencent calls, 30-day redacted audit records, station persistence, Schema v0.4 migration and authoritative pile simulation according to `docs/architecture/map-service-protocol.md`.
- A must run real Socket integration once B's handlers and port are available; current fake-server tests prove client framing and response mapping only, not real Tencent success.
- C and B still provide final admin/dashboard and cross-module regression evidence for `A-S1-04`.
- Real Tencent credentials stay in ignored local configuration owned by B. Never commit keys, tokens, passwords, complete key-bearing URLs, screenshots or logs.

## Collaboration rules

- Work on task branches and deliver through Pull Requests targeting `main`; do not force-push or merge from this task.
- Any code or architecture change must update this file and the relevant formal document while removing stale claims.
- Before pushing run `git status --short`, `git diff --check`, credential scanning and the relevant qmake6 builds/tests.
