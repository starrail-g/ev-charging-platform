# Current Project State

## Project and stage

- Project: 东软电动汽车充电桩应用管理平台。
- Current stage: 第一阶段最小闭环开发；真实截止时间为 2026-09-10 24:00。第二阶段截止 2026-09-17 24:00，个人报告截止 2026-09-18 24:00。
- This file was refreshed on 2026-09-09 while implementing the B map-service plan on the PR #13 baseline (`main` `f3bee707`). The requirements source of truth is `docs/requirements/requirements-matrix.md`. 根目录的项目说明书 `.doc`、需求矩阵 `.xls` 和 `三人分工.md` 仅为本地参考文件，不上传、不提交；仓库内 `docs/` Markdown 才是正式项目材料。

## Architecture and boundaries

- `apps/user-client` (A): Qt user UI, session state, station/pile discovery, navigation entry, reservation–charging–billing–settlement interaction, profile and wallet. It never accesses runtime SQLite directly.
- `apps/admin-client` and `dashboard` (C): management UI and ECharts presentation. They consume server/provided data and do not define database or Socket rules.
- `server`, `libs/protocol`, `libs/database`, and `database` (B): Socket, authentication, business/state validation, transactions, concurrency and SQLite persistence. B now owns Schema v0.4, deterministic map Mock/cache/audit, production Tencent WebService HTTP adapter, station import, pile generation, and the internal authoritative simulator gateway; private mTLS transport remains pending.
- `ml` (B/C, S2): 1/6/24-hour load and idle-pile/peak prediction, low-congestion recommendation, load warning, and a callable model-service boundary.
- Mandatory build protocol: all Qt/C++ modules must use `qmake6`; CMake is forbidden as a build, test, acceptance, or release path. See `docs/meetings/build-system-protocol-2026-09-02.md`.
- B provides SQLite schema v0.4 (including transactional v0.3 upgrade), deterministic seed/migration, protocol v1 framing/envelope/error codes, and server handlers for login, profile read/update, wallet recharge, station/pile queries, active/history orders, reservation, charging and settlement. Lifecycle writes use `BEGIN IMMEDIATE`, request-ID replay records, frozen-user checks, direct idle-pile charging and settlement rollback paths; imported/simulated station snapshots and pile status events are persisted.
- A user client is a deterministic Qt Widgets + Mock implementation with an opt-in real `SocketUserService` (see A-S1-03 below). A retains Mock/offline fallback until the real Socket adapter is verified end-to-end.
- C admin client has a qmake shell, repository boundary, Mock data source, login flow and overview states, the 9/4 management action batch (C-S1-005 pile restart / C-S1-007 user freeze-unfreeze), and a local Socket adapter layer (`SocketAdminRepository` + `socketparse`, fake-server tested on Windows and the Ubuntu VM; Mock remains the default via `EV_ADMIN_DATA_SOURCE` until the gate). The phase-1 batch is merged to `main` through PR #11; PR #13 subsequently merged the full-scope, cursor-paged `admin.pile.list` implementation.
- The clean-database server path can load `EV_DATABASE_SEED_PATH` once during initial creation; existing databases are not reseeded.
- The corrected PR #14 contract is documented in `docs/architecture/map-service-protocol.md`; it preserves the current `admin.pile.list` cursor/1 MiB contract and adds bounded paging, explicit map idempotency, canonical map errors, and an independent cloud `pile-simulator` proposal boundary. The deterministic `EV_MAP_SERVER_MOCK=1` handlers, production `HttpTencentClient`, map-only cache with live SQLite aggregation, audit pagination, Schema v0.4, pile generator, and internal gateway are implemented. Asynchronous worker isolation, cache-miss coalescing, retention cleanup, and private mTLS transport remain open.
- 2026-09-09 follow-up fixed the Tencent place-search boundary to the official `nearby(...)` syntax, normalized valid single-point zero-length routes to two protocol points, and audited route preflight/polyline failures. Real Tencent POI and route integration passed with a locally injected key; the key is not retained.

## Current status

- `A-S1-01` (需求矩阵/边界/任务记录)、`A-S1-02` (Mock baseline + `SocketUserService` 覆盖 B PR #4 用户契约) 已完成；`A-S1-03` 真实 Socket 适配已随 PR #9 合入 `main`（`e577baa`，2026-09-05/06），含 P1 修复：UI 线程 Socket 异步化（QtConcurrent + generation 防旧回包）、mutation 请求 ID 跨可重试失败保留、`pending_reservation` 恢复、免密手机号登录与注册入口移除。`A-S1-04` 跨模块最终回归待进行。A 用户端 Mock 地图页面深色圆角下拉样式沿用。
- B PR #4 提供 Schema v0.3 数据库/协议基线（已在 `main`）；PR #8（`994e5ff`）恢复统一 admin/dashboard UI 及其评审修复（A-02/A-04/A-06/A-07、P2-01、契约映射文档 `cfbb282`）。B 的原 admin.* API 已随 **PR #12 合入 `main`（`3d015f7`，2026-09-06）**：`admin.login` 发放进程内 8h 会话 token（`600c657` 起保护 admin API），**除 admin.login 外所有 admin.* 请求必须携带 token**，mutation（station.create/pile.restart/user.status.set）额外携带并校验 `administrator_id` 与 token 主体一致；统计/利用率/重启语义由 `server/tests/admin.py` 覆盖。PR #13 已合入当前 `main`（`f3bee707`），提供 `admin.pile.list` 全量库存：以 `after_id`/`next_after_id` 游标分页，服务端按实际 JSON envelope 保护 1 MiB 帧上限，管理端聚合各页。
- The 2026-09-04 final-decision addendum in `docs/meetings/protocol-summary-2026-09-02.md` overrides the older stop-release/frozen wording; `docs/architecture/protocol.md`, A's `SocketUserService` and C's Mock are aligned to it.
- C phase-1 批（管理操作 + Socket 适配 + Task-12 交付文档）已通过 PR #11 合入 `main`。**评审轮次：①（9/6，A）服务端 token 契约（PR #12）与 administrator_id-only 适配不匹配 → 已修并推送（`fc9d28c` merge main + `ec09270` token 适配，9/6 21:48；LoginResult.token / buildPayload 附 token、mutation 附 administrator_id / 1100 清会话；fake-server sa 15/15 + 真实 main 服务端冒烟，逐字证据见 docs/requirements/current.md §2）；②（9/7，A）文档问题（sa 计数、README 环境变量用法、证据外部路径与闸门预跑记录）→ 本批 docs commit 修正；③（9/7，B 三连）重启按钮态/Mock 同态语义（`f62ee97`）、inactive 站 fan-out 与 has_data（`a7706b3`）、口径 A 全量桩视图（初版 `admin.pile.list` 后续已修为 1 MiB 安全的游标分页聚合；sp 10 / sa 16）。**
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

## Pile simulator baseline

- 责任边界：`services/pile-simulator/pile_simulator.py` 是独立、SQLite-free 的确定性 proposal producer；它只读取 gateway 提供的 station/pile snapshot（本地演示也可从 stdin 读取），不持有数据库路径，不直接写业务状态。服务端 `server/src/map/simulation_gateway.*` 与 `Database::applySimulationProposal` 才是权威校验、事务写入和状态机入口。
- 生成链路：`DeterministicPlanner` 按 `station_id`/`pile_id` 排序，只处理 active station、匹配 seed、`simulated=true`、无 `pending_reservation|reserved|charging|pending_settlement` 未完订单且状态为 `idle|fault|offline` 的桩；用 SHA-256 + PCG32（seed/tick/pile）计算状态转换，输出 `simulator_id`、`seed_id`、`tick_id`、`expected_versions` 和 changes proposal。
- 服务端原子语义：gateway 在 `BEGIN IMMEDIATE` 中校验 simulator/seed、station snapshot version、桩当前状态、simulated 标志、活动订单和最小 idle（默认 `EV_PILE_SIMULATION_MIN_IDLE=1`）；成功时更新桩状态与 `status_source='simulation'`、写入 `pile_status_events`、每个受影响站点只递增一次 `simulation_state.snapshot_version`。同 `(simulator_id,tick_id)` 重放返回原结果；指纹变化、旧版本、非法/占用桩或违反最小 idle 返回 conflict/rejected，不能覆盖 `reserved`/`charging`。
- 持久化对象：v0.4 的 `charging_piles.simulated/status_source/status_updated_at`、`pile_status_events`、`simulation_state` 和 `simulation_tick_records` 支撑资格、快照版本、审计和 tick 幂等；站点首次地图导入时由固定 seed 生成 4–12 个模拟桩，并保证 active station 至少一个 idle 桩。
- 当前实现边界：Python 端目前是一次性 planner/CLI 和最小 length-prefixed gateway client；尚未实现调度循环、快照拉取/冲突后重算、网络重试状态机、heartbeat/last-seen/失败率指标或生产 mTLS listener。`SimulationGateway` 虽编入 `server/server.pro`，但当前 `server/src/main.cpp` 未把它挂到公共 TCP dispatcher；现有 gateway 验证通过直接 C++ 测试调用，不能误称为已上线的云端端到端链路。文档中的 `EV_PILE_SIMULATION_ENABLED`/interval 是目标配置，运行时尚无 scheduler；`EV_PILE_SIMULATION_MIN_IDLE` 是当前 gateway 实际读取的保护项。
- 已验证：`services/pile-simulator` 的 Python unittest 3/3 通过（生成、transition、planner 稳定性向量）；`qmake6` Qt 6.2.4 配置 `server/tests/simulation_gateway.pro`，`make -j2` 后运行 `simulation-gateway-test database/schema/schema.sql` 通过，覆盖首次 tick、幂等重放、状态事件不重复、stale/fingerprint conflict 和 fresh no-op。

## Validation and evidence

- Ubuntu qmake6 (Qt 6.2.4) server build and real-server `admin.py`/`smoke.py`/`concurrency.py` regression pass on the pre-map baseline; the fixed server build, `server/tests/tencent_client.pro` fake HTTP test, real Tencent `map_live.py`, `server/tests/map.py` mock/cache/audit regression, `server/tests/simulation_gateway.pro` direct gateway test, protocol tests, database schema tests, and Python simulator tests pass. Admin-client QtTest suites remain green (`tst_ui` 24, `tst_launchsmoke` 6, `tst_loginflow` 7, `tst_socketparse` 10, `tst_socketadapter` 16). User-client GUI evidence remains a separate desktop/VM check.
- C phase-1 batch is green on Windows and Ubuntu VM identically: tst_ui 24 / tst_launchsmoke 6 / tst_loginflow 7 / tst_socketparse 10 / tst_socketadapter 16 (9/7 review round 3: restart button state / Mock same-state semantics / admin.pile.list full-scope fetchPiles / has_data mapping; sa/sp counts updated after new cases). Web dashboard: node 35 + serve `--check` green.
- C-S1-001/002 复验通过并关闭（迁移原子性三场景 / 同批坏帧保留好帧），见 `docs/release/defect-log.md`。
- Before each commit/PR, scan tracked content for credentials and inspect `git diff --check`; only placeholders may appear in `config/example.env`.

## Dependencies and TODO

- `A-S1-04`: coordinated final regression, GUI evidence and clean-environment delivery (2026-09-07 gate and 09-10 integration deadline).
- C: PR #11 三轮评审修复和 PR #13 的 `admin.pile.list` 修复已合入当前 `main`（`f3bee707`）；剩余 = 9/8–9/10 release materials and clean-environment evidence（`docs/release/stage1-checklist.md`）。
- B (owned): 原 admin.* handlers 通过 PR #12 合入 `main`，全量桩库存 `admin.pile.list` 通过 PR #13 合入当前 `main`；当前分支已落地地图契约的 v0.4/Mock/cache/audit/import/generator/gateway 和生产 Tencent HTTP adapter。官方 key 已完成端点烟测但当前配额耗尽；后续优先级是配额恢复后的真实 POI/路线联调、私有 mTLS listener、异步 worker、并发 miss 合并、清理任务和最终 A/C 联调。
- Open technical item: move slow database work off the Socket event-loop thread, or define a bounded worker/lock strategy (B-owned).
- Official Tencent key smoke check now reaches the upstream endpoint: geocoding and driving route both succeed with `tencent_live`; the POI search endpoint independently returns provider status 121 (daily quota exhausted), mapped to `MAP_QUOTA_EXCEEDED` (1404). Fake HTTP and full production-selection integration pass.
- B map work is tracked in `docs/role-b-map-service-plan.md`: protocol/size guard → v0.4 migration → map Mock/cache/audit → station/pile import → handlers → simulator gateway/cloud simulator → validation. The cloud simulator never writes SQLite directly; the server remains the sole business-state writer.
- S2 intelligent-analysis chain: data preparation → model-service contract → predictions/recommendation/warning → B service adaptation → C display → integrated validation. It must not block the S1 basic charging loop.

## Collaboration and security rules

- Work on task branches and deliver through Pull Requests; do not push directly to `main` or force-push.
- Any code or architecture change must update this file and the relevant design/API document, keeping only current, actionable information.
- All Qt/C++ build and test evidence must use `qmake6`; CMake is not an accepted project path.
- Never commit Tencent Maps keys, passwords, tokens, private keys, runtime databases, logs or generated build output. Real map credentials stay in ignored local configuration.

## Recent history

- 2026-09-09：在 `feature/b-map-service` 基于 main `f3bee707` 落地 Schema v0.4、确定性地图 Mock/cache/audit、站点导入与桩生成、模拟器内部 gateway；新增地图 Socket 回归和 gateway qmake6 测试。普通缓存命中重新聚合 SQLite 桩快照，request replay 保留历史响应；审计分页游标固定为 `created_at + id`。
- 2026-09-09：接入生产 `HttpTencentClient`：服务端运行时读取 `TENCENT_MAP_KEY`，调用官方 geocoder/POI/driving/walking WebService，统一错误映射，路线分钟→秒和 polyline 解码；新增 fake HTTP qmake6 单测和生产模式 `map_live.py` 端到端测试。真实 key 未写入仓库。
- 2026-09-09：真实联调发现 POI `boundary=circle(...)` 不被腾讯接受（status 348）；已改为官方 `nearby(...)`，补充单点路线兼容及路线失败审计，修复后真实 `map_live.py` 通过。
- 2026-09-09：重新用用户提供的运行时 key 联调官方接口：地址解析和驾车路线已返回真实结果，POI 接口仍受腾讯 status 121 日额度限制；确认服务端不再走 Mock，错误正确返回 1404。
- B Schema v0.3 protocol/database foundation and profile/wallet endpoints are merged; its smoke and concurrency suites cover transaction rollback, replay, lifecycle, frozen policy and completed-order history. The pile-uniqueness migration `002_v0.2_to_v0.3.sql` handles already-deployed v0.2 databases (C re-verified 2026-09-04).
- A user-client Mock baseline and opt-in Socket adapter are implemented; PR #9 (P1 follow-up) merged 2026-09-05 as `e577baa`.
- PR #8 (`994e5ff`, 2026-09-04) restored the unified admin/dashboard UI (reverting PR #7's rollback of PR #6) plus the A-02/A-04/A-06/A-07 gaps, P2-01 cleanup and the AdminRepository contract-to-wire mapping doc.
- B answered the Q1–Q7 contract-freeze items (revenue_daily series / seven-day time-weighted station utilization / restart state safety), merged the original admin.* handlers with token sessions via PR #12 (`3d015f7`), and merged the 1 MiB-safe cursor-paged full-scope `admin.pile.list` through PR #13 (`f3bee707`). C's Socket adapter consumes the merged contract (token + mutation administrator_id, dual-range fetchOverview, cursor-page aggregation, restart semantics identical to C's Mock).
- C phase-1 delivery (PR #11) is committed on `feature/member-c-phase1-mvp` (management actions, Socket adapter, Task-12 docs, Q1–Q7 freeze ledger, defect closures, release templates); the token-alignment fix was pushed as `ec09270` (2026-09-06 21:48), and the review round-2 doc fixes land in the same PR ahead of the 09-07 gate.
- `docs/role-a-delivery-plan.md` records A's phase-I/II dependencies, acceptance gates and delivery list; `docs/role-c-delivery-plan.md` does the same for C.
- A-S1-01/02/03 已完成（含 PR #9 P1 修复与 Socket 真实适配）；后续 A 任务包括联调测试、腾讯地图导航优化、智能分析结果展示和最终 qmake6 交付；不得将 Mock 或适配器构建通过误记为真实闭环完成。

## Async/session and permission safeguards (user client)

- `SessionManager::generation()` is an authentication generation: it changes only when `beginSession()` establishes a different identity or `clear()` logs out. Profile, avatar and wallet refreshes use `updateUser()`/local field updates and do not invalidate concurrent requests.
- `runService()` captures the auth generation and user ID, so callbacks after logout/account switching are discarded; station/pile request generations still reject older query results, and pile callbacks also verify the selected station ID.
- Frozen users may read data and perform reservation cancellation, charging stop and settlement, but UI controls for reservation creation/confirmation, charging start/direct start and wallet recharge are disabled.
- An optional discard callback restores transient UI state such as the recharge button when an in-flight request is invalidated.
- Compatibility baseline: current remote `main` is `f3bee707`; it includes PR #11, PR #12, and PR #13. This feature branch targets that baseline and contains the production Tencent adapter and documentation changes described above; do not claim private mTLS or async worker isolation until separately implemented.
