# Current Project State

## Project and stage

- Project: 东软电动汽车充电桩应用管理平台。
- Current stage: 第一阶段最小闭环开发；真实截止时间为 2026-09-10 24:00。第二阶段截止 2026-09-17 24:00，个人报告截止 2026-09-18 24:00。
- This file was updated for `A-S1-02` and `C-T-C1.1` on 2026-09-02, and refreshed on 2026-09-03 for the PR #6 second-round review state. The requirements source of truth is `docs/requirements/requirements-matrix.md`.

## Architecture and boundaries

- `apps/user-client` (A): Qt user UI, session state, station/pile discovery, navigation entry, reservation–charging–billing–settlement interaction, profile and wallet. It never accesses runtime SQLite directly.
- `apps/admin-client` and `dashboard` (C): management UI and ECharts presentation. They consume server/provided data and do not define database or Socket rules.
- `server`, `libs/protocol`, `libs/database`, and `database` (B): Socket, authentication, business/state validation, transactions, concurrency and SQLite persistence.
- `ml` (B/C, S2): 1/6/24-hour load and idle-pile/peak prediction, low-congestion recommendation, load warning, and a callable model-service boundary.
- Mandatory build protocol: all Qt/C++ modules must use `qmake6`; CMake is forbidden as a build, test, acceptance, or release path. See `docs/meetings/build-system-protocol-2026-09-02.md`.

Target flow: Qt clients/dashboard → unified protocol or data interface → server → database layer → SQLite.

## Current status

- `A-S1-01`: **已完成**. Requirements traceability, stage boundaries, dependencies and public-task assignments are recorded in `docs/requirements/requirements-matrix.md` and the project task records.
- `A-S1-02`: Mock baseline is implemented in the submitted user-client sources. Only `MockUserService` is present; no `SocketUserService` or user-client protocol adapter is included.
- B backend baseline is merged into `main` (`3b8cf78` via merge `3c31826`): SQLite schema v0.2, deterministic seed/migration, protocol v1 framing/envelope/error codes, and health/echo diagnostic server are available. Database-backed business handlers, runtime data-access integration, and full endpoint tests remain to be completed before end-to-end closure.
- C unified day/night UI milestone `T-C1.1` is delivered as PR #6 (`feature/unified-ui`): Qt admin unified UI (design tokens → theme/status pulse → shell/login → overview/topology → pile/station/user pages) plus the offline-ready ECharts dashboard (local server + demo data + resilient map surface + 3-breakpoint layout + anomaly focus). Mock end-to-end demo path is repeatable on Windows and Ubuntu VM; real Socket integration still awaits the 9/7 18:00 interface gate.
- `T-C1.1` review fixes (2026-09-03, on `feature/unified-ui`) — **已提交并推送到 PR #6 head**（`090404c` fix(dashboard)、`3cda566` + `08e988c` fix(admin-ui)、`8510c8a` ci(ui)；分支顶 `7174284`，GitHub Actions `ui.yml` 最新 run 33707803847 绿）：Web — 1366×768 compact layout now uses explicit grid placement (metric strip spans full row above map-left / right-rail, single chart tab 214px, no horizontal scroll, mobile returns to natural flow); ECharts palette resolved from CSS tokens before hitting canvas (axis text no longer falls back to black; pixel probe: 0% near-black in visible charts); map timeout fallback race closed (MapSurface owns container lifecycle, AbortSignal cancels late SDK completion, renderers never clear the shared container); anomaly focus lifecycle unified through one `deactivateFocus` exit (close button re-click, Esc, click on non-alert station all consistent; closing then re-clicking the same alert reopens immediately); Tencent marker clicks flow through the same `onStationActivate` as topology nodes; six page states (loading/content/empty/error/offline/stale) rendered in a real state region with retry/refresh actions, `?state=empty|error|offline|stale` demo injection always carries the "演示状态注入" label, stale timestamp guard in dashboard-state. Qt — `PileStatus::Unknown` merged into the actual state model (parse/protocol/display round-trip); status tags are now glyph+text composites (`StatusGlyphWidget`: CheckCircle/ClockDashed/BoltDot/WarningTriangle/LinkOff/QuestionDiamond) keeping QLabel[state=…] QSS coloring; station topology mounts real `StatusPulseWidget` halos on Charging/Fault stations driven by generated motion tokens and is keyboard-reachable (StrongFocus, arrows, Enter/Space, focus ring); CI workflow `.github/workflows/ui.yml` added (web assets + node/python tests, Ubuntu qt6-base-dev qmake6 build + three offscreen test binaries).
- Qt 管理端侧边栏导航的垂直节奏改由 `QListWidget::spacing(12)` 单点控制；导航项为 52px 高、仅保留 10px 横向外边距，避免 QSS 上下 margin 与列表 spacing 叠加造成选项拥挤或文字越界。
- PR #6 二轮评审（P1-01/P1-02 必修、P2-01/P2-02 建议）修复已本地实现并验证（Web `node --test` 35/35，Qt offscreen `tst_ui` 20 / `tst_launchsmoke` 6 / `tst_loginflow` 7），**尚未提交/推送（待用户验收后 commit）**：P1-01 MapSurface 并发挂载竞态 → mount 递增 generation token、新挂载 abort 旧在途在线挂载、异步完成后代次不匹配返回 `superseded`（app.js 旧渲染流程静默退出，不更新 badge/图表）；P1-02 `data-station-id` SVG 属性注入 → `svgEscape(String(station.id))` + 恶意 id 回归测试（新 `topology-map-renderer.test.mjs`）；P2-01 金额浮点格式化 → `formatYuanCents` 提升为 `ev` 共享纯函数（整数分、千分位、负号前置 `-¥0.01`），userpage 余额列与 pilepage 单价列改走整数路径，tst_ui 锁定正/零/负/大额用例；P2-02 即本文件状态刷新。
- 本地工作树另有一批 **9/3 晚实现、未提交** 的管理端缺口（A-04 桩累计次数/时长列、A-07 用户注册时间列、A-06 站在线率列、A-02 近 30 日营收卡；demo.json ⇔ mockdataset 双端同值，tst_ui/data-adapter 测试锁常量）——与二轮评论修复同在工作树，按任务边界分批提交（评审修复批次先行）。

## A-S1-02 delivered scope

- User-window navigation with a 420×760 mobile-style layout, centralized `SessionManager`, login/logout and two-step registration validation.
- Deterministic Mock station/pile query with loading, empty, unavailable, timeout and service-error feedback; station cards show dynamic idle/total counts and pile details show type, power, status and price.
- Mock-only order flow: create/reserve, start charging, stop charging, settle, cancel reservation, current-order status and newest-first completed history with completion time, station address and amount.
- Mock/offline navigation route with explicit Mock labeling and local-only `TENCENT_MAP_KEY` configuration placeholder. No real key is stored in source, documentation or Git.
- Profile nickname/avatar and wallet Mock operations; no UI code contains SQL or direct SQLite access.
- Regression fix for early `orderSummary_` access; the label is constructed before login refresh can run.
- Review fixes applied: all business methods reject empty user IDs; profile/avatar changes persist in Mock; registration is reachable from login; route mode is passed to the Mock service and coordinates are range/finite checked. Login behavior follows the documented phone-only Mock flow.
- Monetary DTOs use integer cents (`walletBalanceCents`, `priceCentsPerKwh`, `amountCents`). Mock reservation now returns `PendingReservation` and requires `confirmReservation`; settlement checks balance, deducts cents atomically on success, and leaves the order pending on insufficient balance.
- DTO includes a temporary `UserStatus` and `Offline` pile state. Mock external IDs remain strings for the current demo; no wire-ID conversion or Socket mapping is present in the submitted user-client sources.
- Socket/Protocol status: no user-client `SocketUserService`, frame codec integration, request/response mapping, or protocol operation implementation is included in this PR. A-S1-03 remains pending until B freezes the contract and a separate adapter implementation is added and verified.

## Validation and evidence

- qmake6 project files and QtTest sources are present, but this PR does not contain independent qmake6 build, test, or GUI-startup evidence. Re-run the commands in Ubuntu/VM and record the output before marking build verification as PASS.
- This Windows host has no local qmake6 or SQLite CLI; qmake6 verification is performed in the Ubuntu VM. The real GUI/Socket/database end-to-end path is still pending.
- Before each commit/PR, scan tracked content for credentials and inspect `git diff --check`; only placeholders may appear in `config/example.env`.

## Dependencies and TODO

- `A-S1-02` real integration: implement a B-compatible `IUserService` Socket adapter only after the protocol and response fields are frozen; preserve the Mock/offline switch and map server errors without changing page code.
- PR #6 head 待办（2026-09-03 刷新，替代旧 review-fixes 待办——该批已提交推送、CI 绿）：①本地两批未提交工作按任务边界分批 commit 并 push（顺序：二轮评审 P1/P2 修复 → 9/3 晚 4 缺口 A-04/A-07/A-06/A-02），②PR #6 description 刷新，③Task 12 交付文档，④Ubuntu VM qmake6 复跑，⑤9/7 18:00 Socket 闸门联调。
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
- A user-client Mock baseline was added; independent real Socket/SQLite integration and reproducible qmake6 verification remain pending.
- User-client detailed requirements file added; Tencent Maps POI/API probe and similar-project research are the next integration step.
- C `T-C1.1` unified day/night UI (Qt admin + Web dashboard) submitted as PR #6; 29/29 QtTest on Windows and Ubuntu VM, Web 15/15 node tests, offline demo path verified in both environments.
## 同学 A 任务计划

- 新增 `docs/role-a-delivery-plan.md`，记录同学 A 阶段 I/II 任务、依赖、验收标准和交付清单。
- `A-S1-01`、`A-S1-02` 已完成范围仅包括需求追踪和 Mock 用户端；当前没有用户端 Socket/Protocol v1 适配器，真实业务联调仍待后续 A-S1-03。
- 后续 A 任务包括联调测试、腾讯地图导航优化、智能分析结果展示和最终 qmake6 交付；不得将 Mock 或适配器构建通过误记为真实闭环完成。
## Main 合入后的 C 端状态

- `main` 已包含 C 管理端基础工程（qmake 工程、五页导航、`AdminRepository`/`MockAdminRepository`、登录 TDD、概览四态）；`feature/unified-ui`（PR #6）在其上完成统一 UI 与 Web 大屏。
- C 的 T-C1.1 验证证据（双平台，2026-09-02）：
  - Windows（Qt 6.2.4 / qmake / mingw）与 Ubuntu 22.04 VM（qmake6 全新构建）QtTest 均为 29/29 PASS（launchsmoke 6 + loginflow 7 + ui 16）；VM 内 GUI 桌面运行正常。
  - Web node 测试 15/15（Windows + VM）、Python unittest 9/9、`serve.py --check` 与 token `--check` 无漂移；浏览器 1920/1366/900 三档验收无横向滚动、离线拓扑完整。
  - 密钥审计：无真实 `TENCENT_MAP_KEY` 入库；`config/local.env` gitignored。
- C 未完成项（不提前标记完成）：真实 Socket 联调（9/7 18:00 闸门，未过则按 A 批准走 Mock 降级并明确标注）；腾讯地图生产密钥；预测/调度模型接入；交付文档（docs/ui、requirements/current.md 校订）随 T-C1.2 推进。
- C 的字段和接口必须继续对齐 B 的协议 v1、SQLite schema v0.2 和需求矩阵；不在 UI 文档中重新定义协议或数据库规则。
- 阶段 I 仍以 2026-09-10 24:00 为截止；C 的接口对齐、qmake6 构建和联调证据按 `main` 当前计划推进。
