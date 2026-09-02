# Current Project State

## Project and stage

- Project: 东软电动汽车充电桩应用管理平台。
- Current stage: 第一阶段最小闭环开发；真实截止时间为 2026-09-10 24:00。第二阶段截止 2026-09-17 24:00，个人报告截止 2026-09-18 24:00。
- This file was updated for `A-S1-02` on 2026-09-02. The requirements source of truth is `docs/requirements/requirements-matrix.md`.

## Architecture and boundaries

- `apps/user-client` (A): Qt user UI, session state, station/pile discovery, navigation entry, reservation–charging–billing–settlement interaction, profile and wallet. It never accesses runtime SQLite directly.
- `apps/admin-client` and `dashboard` (C): management UI and ECharts presentation. They consume server/provided data and do not define database or Socket rules.
- `server`, `libs/protocol`, `libs/database`, and `database` (B): Socket, authentication, business/state validation, transactions, concurrency and SQLite persistence.
- `ml` (B/C, S2): 1/6/24-hour load and idle-pile/peak prediction, low-congestion recommendation, load warning, and a callable model-service boundary.
- Mandatory build protocol: all Qt/C++ modules must use `qmake6`; CMake is forbidden as a build, test, acceptance, or release path. See `docs/meetings/build-system-protocol-2026-09-02.md`.

Target flow: Qt clients/dashboard → unified protocol or data interface → server → database layer → SQLite.

## Current status

- `A-S1-01`: **已完成**. Requirements traceability, stage boundaries, dependencies and public-task assignments are recorded in `docs/requirements/requirements-matrix.md` and the project task records.
- `A-S1-02`: **Mock baseline implemented and prepared for upload**. The Qt Widgets client, adapter boundary, deterministic data, order flow, error paths and tests are present. Real Tencent Maps address geocoding/basic routing and replacement of the Mock adapter by B's Socket client remain S1 integration work.
- B backend baseline is merged into `main` (`3b8cf78` via merge `3c31826`): SQLite schema v0.2, deterministic seed/migration, protocol v1 framing/envelope/error codes, and health/echo diagnostic server are available. Database-backed business handlers, runtime data-access integration, and full endpoint tests remain to be completed before end-to-end closure.
- C admin/dashboard work and cross-module testing remain in progress; no C task is marked complete by this update.

## A-S1-02 delivered scope

- User-window navigation with a 420×760 mobile-style layout, centralized `SessionManager`, login/logout and two-step registration validation.
- Deterministic Mock station/pile query with loading, empty, unavailable, timeout and service-error feedback; station cards show dynamic idle/total counts and pile details show type, power, status and price.
- Adapter-only order flow: create/reserve, start charging, stop charging, settle, cancel reservation, current-order status and newest-first completed history with completion time, station address and amount.
- Mock/offline navigation route with explicit Mock labeling and local-only `TENCENT_MAP_KEY` configuration placeholder. No real key is stored in source, documentation or Git.
- Profile nickname/avatar and wallet Mock operations; no UI code contains SQL or direct SQLite access.
- Regression fix for early `orderSummary_` access; the label is constructed before login refresh can run.
- Review fixes applied: all business methods reject empty user IDs; login validates required password and minimum length; profile/avatar changes persist in Mock; registration is reachable from login; route mode is passed to the adapter and coordinates are range/finite checked.
- Monetary DTOs use integer cents (`walletBalanceCents`, `priceCentsPerKwh`, `amountCents`). Mock reservation now returns `PendingReservation` and requires `confirmReservation`; settlement checks balance, deducts cents atomically on success, and leaves the order pending on insufficient balance.
- DTO includes a temporary `UserStatus` and `Offline` pile state; external IDs remain adapter-owned strings until B freezes numeric ID fields, so page code does not depend on that provisional mapping.

## Validation and evidence

- Ubuntu VM qmake6 evidence: `qmake6 --version` reports Qt 6.2.4; clean application and QtTest builds **PASS**; QtTest **PASS** with 6 cases; GUI startup binary is produced. SSH test execution uses `QT_QPA_PLATFORM=offscreen` because no display is attached.
- This Windows host has no local qmake6 or SQLite CLI; qmake6 verification is performed in the Ubuntu VM. The real GUI/Socket/database end-to-end path is still pending.
- Before each commit/PR, scan tracked content for credentials and inspect `git diff --check`; only placeholders may appear in `config/example.env`.

## Dependencies and TODO

- `A-S1-02` real integration: implement a B-compatible `IUserService` adapter using the frozen protocol, preserving the Mock/offline switch and mapping server enums/errors without changing page code.
- `A-S1-02` navigation: add Tencent Maps geocoding and basic driving/walking route display from local configuration; retain explicit Mock/offline fallback and record failure/Key-missing evidence.
- Detailed user-client requirements and Tencent Maps investigation are recorded in `docs/ui/user-client-detailed-requirements.md`, including the Linux + Qt baseline, acceptance flow, API probe command, Key-safety rules and GitHub reference projects. Real POI fields are not yet treated as business prices/pile counts/statuses; those remain B/Mock data until verified.
- `B-S1-01`/`B-S1-02`: finish runtime database access, transaction-backed login/station/pile/order handlers and stable request/response samples before client replacement.
- `C-S1-01`/`C-S1-02`/`C-S1-03`: finish admin pages, dashboard data path, clean-build and cross-module evidence. End-to-end closure requires A, B and C paths plus abnormal-case tests.
- S2 intelligent-analysis chain: data preparation → model-service contract → predictions/recommendation/warning → B service adaptation → C display → integrated validation. It must not block the S1 basic charging loop.

## Collaboration and security rules

- Work on task branches and deliver through Pull Requests; do not push directly to `main` or force-push.
- Any code or architecture change must update this file and the relevant design/API document, keeping only current, actionable information.
- All Qt/C++ build and test evidence must use `qmake6`; CMake is not an accepted project path.
- Never commit Tencent Maps keys, passwords, tokens, private keys, runtime databases, logs or generated build output. Real map credentials stay in ignored local configuration.

## Recent history

- `A-S1-01` requirements baseline and repository/task records completed.
- B schema/protocol foundation merged to `main`.
- A user-client Mock baseline implemented, tested on Ubuntu VM, and prepared on branch `member-a-user-client` for PR review.
- User-client detailed requirements file added; Tencent Maps POI/API probe and similar-project research are the next integration step.
