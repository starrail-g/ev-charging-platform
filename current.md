# Current Project State

## Project and stage

- Project: 东软电动汽车充电桩应用管理平台。
- Current stage: 第一阶段最小闭环开发；真实截止时间为 2026-09-10 24:00。第二阶段截止 2026-09-17 24:00，个人报告截止 2026-09-18 24:00。
- This file was updated for the `A-S1-03` PR #9 P1 follow-up on 2026-09-04. The requirements source of truth is `docs/requirements/requirements-matrix.md`.

## Architecture and boundaries

- `apps/user-client` (A): Qt user UI, session state, station/pile discovery, navigation entry, reservation–charging–billing–settlement interaction, profile and wallet. It never accesses runtime SQLite directly.
- `apps/admin-client` and `dashboard` (C): management UI and ECharts presentation. They consume server/provided data and do not define database or Socket rules.
- `server`, `libs/protocol`, `libs/database`, and `database` (B): Socket, authentication, business/state validation, transactions, concurrency and SQLite persistence.
- `ml` (B/C, S2): 1/6/24-hour load and idle-pile/peak prediction, low-congestion recommendation, load warning, and a callable model-service boundary.
- Mandatory build protocol: all Qt/C++ modules must use `qmake6`; CMake is forbidden as a build, test, acceptance, or release path. See `docs/meetings/build-system-protocol-2026-09-02.md`.

Target flow: Qt clients/dashboard → unified protocol or data interface → server → database layer → SQLite.

## Current status

- `A-S1-01`: **已完成**. Requirements traceability, stage boundaries, dependencies and public-task assignments are recorded in `docs/requirements/requirements-matrix.md` and the project task records.
- `A-S1-02`: Mock baseline remains the default runtime path and has been validated. `SocketUserService` now covers the complete B PR #4 user contract; A-S1-03 is complete pending the separate A-S1-04 cross-module regression.
- Mock 地图页面的驾车/步行下拉框采用深色圆角样式：默认项为黑底白字，当前选中项为白底黑字。
- B PR #4 supplies the Schema v0.3 database/protocol baseline. PR #8 is merged in `origin/main` at `994e5ff`, restoring the unified admin/dashboard UI and its review fixes; A does not modify B/C implementation code.
- The 2026-09-04 final-decision addendum in `docs/meetings/protocol-summary-2026-09-02.md` overrides the older stop-release/frozen wording; `docs/architecture/protocol.md` and `SocketUserService` are aligned to it.
- C admin/dashboard work and cross-module testing remain in progress; no C task is marked complete by this update.

## A-S1-02 delivered scope

- User-window navigation with a 420×760 mobile-style layout, centralized `SessionManager`, login/logout and two-step registration validation.
- Deterministic Mock station/pile query with loading, empty, unavailable, timeout and service-error feedback; station cards show dynamic idle/total counts and pile details show type, power, status and price.
- Adapter-only order flow: create/reserve, start charging, stop charging, settle, cancel reservation, current-order status and newest-first completed history with completion time, station address and amount.
- Mock/offline navigation route with explicit Mock labeling and local-only `TENCENT_MAP_KEY` configuration placeholder. No real key is stored in source, documentation or Git.
- Profile nickname/avatar and wallet Mock operations; no UI code contains SQL or direct SQLite access.
- Regression fix for early `orderSummary_` access; the label is constructed before login refresh can run.
- Review fixes applied: all business methods reject empty user IDs; profile/avatar changes persist in Mock; registration is reachable from login; route mode is passed to the Mock service and coordinates are range/finite checked. Login behavior follows the documented phone-only Mock flow.
- Monetary DTOs use integer cents (`walletBalanceCents`, `priceCentsPerKwh`, `amountCents`). Mock reservation now returns `PendingReservation` and requires `confirmReservation`; settlement checks balance, deducts cents atomically on success, and leaves the order pending on insufficient balance.
- DTO uses protocol-aligned `UserStatus` (`active`/`frozen`) and `Offline` pile state. Mock external IDs remain strings in the UI model; `SocketUserService` converts numeric-looking IDs at the wire boundary and maps B's canonical station/pile/order fields and status values in one adapter.
- Socket/Protocol status: v1 length-prefix framing, UTF-8 JSON envelopes, timeout/connection handling, numeric error propagation, phone-only `user.login`, station/pile, active/history order, reservation, charging and settlement operations are implemented. Socket calls run through QtConcurrent and return through `QFutureWatcher`, so network waits do not block the GUI event loop. State-changing operations retain their UUID after timeout/disconnect/error and reuse it for the same operation/payload until a successful response; `pending_reservation` can be retried or cancelled from the charging page. `user.profile.get/update` and `wallet.recharge` map to integer-cent DTOs; frozen status 1101, stop-release and insufficient-balance responses are translated at the adapter boundary.

## Validation and evidence

- Ubuntu VM qmake6 (Qt 6.2.4) application, server and QtTest builds pass. The user-client suite reports 12 passed, 0 failed and 0 skipped against a fresh `origin/main` source baseline, including lost-response request-ID reuse, Socket lifecycle, profile/wallet and frozen-policy tests. GUI startup remains a desktop/VM manual check.
- This Windows host has no local qmake6 or SQLite CLI; qmake6 verification is performed in the Ubuntu VM. Runtime SQLite is never accessed directly by the user client.
- Before each commit/PR, scan tracked content for credentials and inspect `git diff --check`; only placeholders may appear in `config/example.env`.

## Dependencies and TODO

- `A-S1-03` is complete against the B PR #4 Schema v0.3 contract on current `origin/main` `994e5ff` (PR #8 UI baseline included); A-S1-04 remains for coordinated final regression, GUI evidence and clean-environment delivery. `IUserService` now exposes both reservation and direct charging start paths. Keep future field/status changes inside `SocketUserService` and preserve the Mock/offline switch. The PR #9 P1 follow-up removes GUI-thread Socket waits, makes pending reservations recoverable, and preserves mutation request IDs across retryable failures.
- PR#4 解锁清单已完成；冻结策略、事务扣款、并发/幂等、协议样例、种子数据和后端证据均由 B smoke/concurrency 覆盖。
- PR#4 精简修改清单保留为评审记录，不再作为未完成阻塞项。
- `A-S1-02` navigation: add Tencent Maps geocoding and basic driving/walking route display from local configuration; retain explicit Mock/offline fallback and record failure/Key-missing evidence.
- Detailed user-client requirements and Tencent Maps investigation are recorded in `docs/ui/user-client-detailed-requirements.md`, including the Linux + Qt baseline, acceptance flow, API probe command, Key-safety rules and GitHub reference projects. Real POI fields are not yet treated as business prices/pile counts/statuses; those remain B/Mock data until verified.
- `C-S1-01`/`C-S1-02`/`C-S1-03`: finish admin pages, dashboard data path, clean-build and cross-module evidence. End-to-end closure requires A, B and C paths plus abnormal-case tests.
- S2 intelligent-analysis chain: data preparation → model-service contract → predictions/recommendation/warning → B service adaptation → C display → integrated validation. It must not block the S1 basic charging loop.

## Collaboration and security rules

- Work on task branches and deliver through Pull Requests; do not push directly to `main` or force-push.
- Any code or architecture change must update this file and the relevant design/API document, keeping only current, actionable information.
- All Qt/C++ build and test evidence must use `qmake6`; CMake is not an accepted project path.
- Never commit Tencent Maps keys, passwords, tokens, private keys, runtime databases, logs or generated build output. Real map credentials stay in ignored local configuration.

## Recent history

- B Schema v0.3 protocol/database foundation and profile/wallet endpoints are merged; its smoke and concurrency suites cover transaction rollback, replay, lifecycle, frozen policy and completed-order history.
- A user-client Mock baseline and opt-in Socket adapter are implemented; the PR #9 P1 follow-up makes Socket UI calls asynchronous, preserves mutation request IDs across retryable failures and exposes recovery for `pending_reservation`.
- PR #8 restored the unified admin/dashboard UI plus the A-02/A-04/A-06/A-07 and amount-format review fixes on the current main line.
- `docs/role-a-delivery-plan.md` records A's phase-I/II dependencies, acceptance gates and delivery list.
- `A-S1-01`、`A-S1-02`、`A-S1-03` 已完成；A-S1-04 待进行跨模块最终回归和交付验证。
- 后续 A 任务包括联调测试、腾讯地图导航优化、智能分析结果展示和最终 qmake6 交付；不得将 Mock 或适配器构建通过误记为真实闭环完成。

## Async callback validity (PR #9 follow-up)

- `runService()` captures `SessionManager::generation()` and the active user ID; callbacks are discarded after logout or any session replacement, preventing stale responses from reading or restoring cleared session state.
- Station queries use a monotonically increasing request generation. Pile queries additionally require both the latest generation and the station ID captured when the request started. Socket mode hides the standalone registration entry because v1 supports phone-only login with first-login auto-creation.
