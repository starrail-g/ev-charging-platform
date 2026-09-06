# Current Project State

## Project and stage

- Project: 东软电动汽车充电桩应用管理平台。
- Current stage: 第一阶段最小闭环开发；真实截止时间为 2026-09-10 24:00。第二阶段截止 2026-09-17 24:00，个人报告截止 2026-09-18 24:00。
- This file was updated on 2026-09-06 while converging `main` (PR #9) into the C phase-1 branch (PR #11). The requirements source of truth is `docs/requirements/requirements-matrix.md`. 根目录的项目说明书 `.doc`、需求矩阵 `.xls` 和 `三人分工.md` 仅为本地参考文件，不上传、不提交；仓库内 `docs/` Markdown 才是正式项目材料。

## Architecture and boundaries

- `apps/user-client` (A): Qt user UI, session state, station/pile discovery, navigation entry, reservation–charging–billing–settlement interaction, profile and wallet. It never accesses runtime SQLite directly.
- `apps/admin-client` and `dashboard` (C): management UI and ECharts presentation. They consume server/provided data and do not define database or Socket rules.
- `server`, `libs/protocol`, `libs/database`, and `database` (B): Socket, authentication, business/state validation, transactions, concurrency and SQLite persistence.
- `ml` (B/C, S2): 1/6/24-hour load and idle-pile/peak prediction, low-congestion recommendation, load warning, and a callable model-service boundary.
- Mandatory build protocol: all Qt/C++ modules must use `qmake6`; CMake is forbidden as a build, test, acceptance, or release path. See `docs/meetings/build-system-protocol-2026-09-02.md`.
- B provides SQLite schema v0.3, deterministic seed/migration, protocol v1 framing/envelope/error codes, and server handlers for login, profile read/update, wallet recharge, station/pile queries, active/history orders, reservation, charging and settlement. Lifecycle writes use `BEGIN IMMEDIATE`, request-ID replay records, frozen-user checks, direct idle-pile charging and settlement rollback paths.
- A user client is a deterministic Qt Widgets + Mock implementation with an opt-in real `SocketUserService` (see A-S1-03 below). A retains Mock/offline fallback until the real Socket adapter is verified end-to-end.
- C admin client has a qmake shell, repository boundary, Mock data source, login flow and overview states, the 9/4 management action batch (C-S1-005 pile restart / C-S1-007 user freeze-unfreeze), and a local Socket adapter layer (`SocketAdminRepository` + `socketparse`, fake-server tested on Windows and the Ubuntu VM; Mock remains the default via `EV_ADMIN_DATA_SOURCE` until the gate). All of it is committed on `feature/member-c-phase1-mvp` as PR #11 (open).
- The clean-database server path can load `EV_DATABASE_SEED_PATH` once during initial creation; existing databases are not reseeded.

## Current status

- `A-S1-01` (需求矩阵/边界/任务记录)、`A-S1-02` (Mock baseline + `SocketUserService` 覆盖 B PR #4 用户契约) 已完成；`A-S1-03` 真实 Socket 适配已随 PR #9 合入 `main`（`e577baa`，2026-09-05/06），含 P1 修复：UI 线程 Socket 异步化（QtConcurrent + generation 防旧回包）、mutation 请求 ID 跨可重试失败保留、`pending_reservation` 恢复、免密手机号登录与注册入口移除。`A-S1-04` 跨模块最终回归待进行。A 用户端 Mock 地图页面深色圆角下拉样式沿用。
- B PR #4 提供 Schema v0.3 数据库/协议基线（已在 `main`）；PR #8（`994e5ff`）恢复统一 admin/dashboard UI 及其评审修复（A-02/A-04/A-06/A-07、P2-01、契约映射文档 `cfbb282`）。B 的 admin.* API 已随 **PR #12 合入 `main`（`3d015f7`，2026-09-06）**：`admin.login` 发放进程内 8h 会话 token（`600c657` 起保护 admin API），**除 admin.login 外所有 admin.* 请求必须携带 token**，mutation（station.create/pile.restart/user.status.set）额外携带并校验 `administrator_id` 与 token 主体一致；统计/利用率/重启语义由 `server/tests/admin.py` 覆盖。
- The 2026-09-04 final-decision addendum in `docs/meetings/protocol-summary-2026-09-02.md` overrides the older stop-release/frozen wording; `docs/architecture/protocol.md`, A's `SocketUserService` and C's Mock are aligned to it.
- C phase-1 批（管理操作 + Socket 适配 + Task-12 交付文档）以 16 commits 提交于 `feature/member-c-phase1-mvp`，PR #11 open（2026-09-06，CI 4/4 绿）。**2026-09-06 评审（PR #11 comments, A）：服务端 token 契约（PR #12）与 C 的 administrator_id-only 适配不匹配 → C 已完成 token 适配（LoginResult.token / buildPayload 附 token、mutation 附 administrator_id / 1100 清会话），fake-server 测试 15/15 + 真实 main 服务端联调冒烟全链路 PASS（login→overview 双 range→fan-out→冻结→重启），改动待推（本地未提交，commit 边界经 C 审查后 push）。**
- 9/7 18:00 接口闸门以登录/概览/桩状态/动作为准；管理端已具备 token 契约适配，闸门当天以 main 服务端真联调验证。

## A-S1-02 delivered scope

- User-window navigation with a 420×760 mobile-style layout, centralized `SessionManager`, phone-only login/logout and 11-digit ASCII phone validation.
- Deterministic Mock station/pile query with loading, empty, unavailable, timeout and service-error feedback; station cards show dynamic idle/total counts and pile details show type, power, status and price.
- Adapter-only order flow: create/reserve, start charging, stop charging, settle, cancel reservation, current-order status and newest-first completed history with completion time, station address and amount.
- Mock/offline navigation route with explicit Mock labeling and local-only `TENCENT_MAP_KEY` configuration placeholder. No real key is stored in source, documentation or Git.
- Profile nickname/avatar and wallet Mock operations; no UI code contains SQL or direct SQLite access.
- Review fixes applied: all business methods reject empty user IDs; profile/avatar changes persist in Mock; route mode is passed to the Mock service and coordinates are range/finite checked. Login behavior follows the documented phone-only Mock flow.
- Monetary DTOs use integer cents (`walletBalanceCents`, `priceCentsPerKwh`, `amountCents`). Mock reservation now returns `PendingReservation` and requires `confirmReservation`; settlement checks balance, deducts cents atomically on success, and leaves the order pending on insufficient balance.
- DTO uses protocol-aligned `UserStatus` (`active`/`frozen`) and `Offline` pile state. Mock external IDs remain strings in the UI model; `SocketUserService` converts numeric-looking IDs at the wire boundary and maps B's canonical station/pile/order fields and status values in one adapter.

## A-S1-03 (PR #9, merged `e577baa`)

- Socket/Protocol status: v1 length-prefix framing, UTF-8 JSON envelopes, timeout/connection handling, numeric error propagation, phone-only `user.login`, station/pile, active/history order, reservation, charging and settlement operations are implemented and async (QtConcurrent + `QFutureWatcher`, no GUI-thread network waits). State-changing operations retain their UUID after timeout/disconnect/error and reuse it for the same operation/payload until a successful response; `pending_reservation` can be retried or cancelled from the charging page. `user.profile.get/update` and `wallet.recharge` map to integer-cent DTOs; frozen status 1101, stop-release and insufficient-balance responses are translated at the adapter boundary.
- PR #9 P1 follow-up removes standalone registration (phone-only auto-registration) and preserves auth generations so late responses after logout/account switch are discarded.

## Validation and evidence

- Ubuntu VM qmake6 (Qt 6.2.4) application, server and QtTest builds pass; user-client QtTest suite is green (Socket integration cases run with `EV_RUN_SOCKET_INTEGRATION=1` against a real server). GUI startup remains a desktop/VM manual check.
- C phase-1 batch is green on Windows and Ubuntu VM identically: tst_ui 24 / tst_launchsmoke 6 / tst_loginflow 7 / tst_socketparse 9 / tst_socketadapter 14 (PR #11 CI qt×2 + web×2 pass). Web dashboard: node 35 + serve `--check` green.
- C-S1-001/002 复验通过并关闭（迁移原子性三场景 / 同批坏帧保留好帧），见 `docs/release/defect-log.md`。
- Before each commit/PR, scan tracked content for credentials and inspect `git diff --check`; only placeholders may appear in `config/example.env`.

## Dependencies and TODO

- `A-S1-04`: coordinated final regression, GUI evidence and clean-environment delivery (2026-09-07 gate and 09-10 integration deadline).
- C: push the PR #11 token-alignment fix after C's review, then 9/6–9/7 socket wiring smoke against B's server (already locally green on the real main server); 9/7 17:00 environment-config test and 18:00 interface gate (docs/meetings/interface-gate-2026-09-07.md); 9/8–9/10 release materials and clean-environment evidence (docs/release/stage1-checklist.md).
- B (owned): admin.* handlers are merged on `main` via PR #12 (`3d015f7`); if any further contract drift appears at the gate, coordinate through B/A — C does not drive it.
- Open technical item: move slow database work off the Socket event-loop thread, or define a bounded worker/lock strategy (B-owned).
- S2 intelligent-analysis chain: data preparation → model-service contract → predictions/recommendation/warning → B service adaptation → C display → integrated validation. It must not block the S1 basic charging loop.

## Collaboration and security rules

- Work on task branches and deliver through Pull Requests; do not push directly to `main` or force-push.
- Any code or architecture change must update this file and the relevant design/API document, keeping only current, actionable information.
- All Qt/C++ build and test evidence must use `qmake6`; CMake is not an accepted project path.
- Never commit Tencent Maps keys, passwords, tokens, private keys, runtime databases, logs or generated build output. Real map credentials stay in ignored local configuration.

## Recent history

- B Schema v0.3 protocol/database foundation and profile/wallet endpoints are merged; its smoke and concurrency suites cover transaction rollback, replay, lifecycle, frozen policy and completed-order history. The pile-uniqueness migration `002_v0.2_to_v0.3.sql` handles already-deployed v0.2 databases (C re-verified 2026-09-04).
- A user-client Mock baseline and opt-in Socket adapter are implemented; PR #9 (P1 follow-up) merged 2026-09-05 as `e577baa`.
- PR #8 (`994e5ff`, 2026-09-04) restored the unified admin/dashboard UI (reverting PR #7's rollback of PR #6) plus the A-02/A-04/A-06/A-07 gaps, P2-01 cleanup and the AdminRepository contract-to-wire mapping doc.
- B answered the Q1–Q7 contract-freeze items (revenue_daily series / seven-day time-weighted station utilization / restart state safety) and merged the admin.* handlers with token sessions via PR #12 (`3d015f7`, 2026-09-06); C aligned the Socket adapter accordingly (token + mutation administrator_id, dual-range fetchOverview, restart semantics identical to C's Mock) and verified against the real main server (2026-09-06 live smoke: login→overview→fan-out→freeze→restart all PASS).
- C phase-1 delivery (PR #11) is committed on `feature/member-c-phase1-mvp` (management actions, Socket adapter, Task-12 docs, Q1–Q7 freeze ledger, defect closures, release templates); the token-alignment fix is pending C's review and push, then the 09-07 gate.
- `docs/role-a-delivery-plan.md` records A's phase-I/II dependencies, acceptance gates and delivery list; `docs/role-c-delivery-plan.md` does the same for C.
- A-S1-01/02/03 已完成（含 PR #9 P1 修复与 Socket 真实适配）；后续 A 任务包括联调测试、腾讯地图导航优化、智能分析结果展示和最终 qmake6 交付；不得将 Mock 或适配器构建通过误记为真实闭环完成。

## Async/session and permission safeguards (user client)

- `SessionManager::generation()` is an authentication generation: it changes only when `beginSession()` establishes a different identity or `clear()` logs out. Profile, avatar and wallet refreshes use `updateUser()`/local field updates and do not invalidate concurrent requests.
- `runService()` captures the auth generation and user ID, so callbacks after logout/account switching are discarded; station/pile request generations still reject older query results, and pile callbacks also verify the selected station ID.
- Frozen users may read data and perform reservation cancellation, charging stop and settlement, but UI controls for reservation creation/confirmation, charging start/direct start and wallet recharge are disabled.
- An optional discard callback restores transient UI state such as the recharge button when an in-flight request is invalidated.
- Compatibility baseline: `origin/main` `e577baa` is merged into `feature/admin-api`; A's user-client additions are retained and B's administrator/database implementation is intentionally preserved because the mainline merge had removed those files.
