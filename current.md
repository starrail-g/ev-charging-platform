# Current Project State

## Project and stage

- Project: 东软电动汽车充电桩应用管理平台。
- Current stage: 第二阶段智能分析与真实数据链路已完成首个可复现闭环，进入发布环境复核；第一阶段最小闭环保持可回归。第二阶段截止 2026-09-17 24:00，个人报告截止 2026-09-18 24:00。
- This file was refreshed on 2026-09-14 after PR #20 and PR #21 were merged into `main` (`0b2576d`). The requirements source of truth is `docs/requirements/requirements-matrix.md`. 根目录的项目说明书 `.doc`、需求矩阵 `.xls` 和 `三人分工.md` 仅为本地参考文件，不上传、不提交；仓库内 `docs/` Markdown 才是正式项目材料。

## Architecture and boundaries

- `apps/user-client` (A): Qt user UI, session state, station/pile discovery, navigation entry, reservation–charging–billing–settlement interaction, profile and wallet. It never accesses runtime SQLite directly.
- `apps/admin-client` and `dashboard` (C): management UI and ECharts presentation. They consume server/provided data and do not define database or Socket rules.
- `server`, `libs/protocol`, `libs/database`, and `database` (B): Socket, authentication, business/state validation, transactions, concurrency and SQLite persistence. B now owns Schema v0.4, deterministic map Mock/cache/audit, production Tencent WebService HTTP adapter, station import, pile generation, and the internal authoritative simulator gateway; private mTLS transport remains pending.
- `ml` (B/C, S2): 1/6/24-hour load and idle-pile/peak prediction, low-congestion recommendation, load warning, and a callable model-service boundary.
- Mandatory build protocol: all Qt/C++ modules must use `qmake6`; CMake is forbidden as a build, test, acceptance, or release path. See `docs/meetings/build-system-protocol-2026-09-02.md`.
- B provides SQLite schema v0.4 (including transactional v0.3 upgrade), deterministic seed/migration, protocol v1 framing/envelope/error codes, and server handlers for login, profile read/update, wallet recharge, station/pile queries, active/history orders, reservation, charging and settlement. Lifecycle writes use `BEGIN IMMEDIATE`, request-ID replay records, frozen-user checks, direct idle-pile charging and settlement rollback paths; imported/simulated station snapshots and pile status events are persisted.
- A user client is a deterministic Qt Widgets client with an opt-in real `SocketUserService`; Mock/offline remains an explicit fallback for fault rehearsal after the real Socket path is verified.
- A's PR #17 map path uses `ServerMapService` for server-owned `map.station.search` and `map.route.plan`; the client never receives `TENCENT_MAP_KEY`. Text-address station discovery sends one address-origin station request and consumes `resolved_origin`; text-address routing sends one address-origin route request directly, so an empty nearby-POI result cannot block route planning.
- User-client base-map rendering now has a separate deployment boundary: optional `TENCENT_MAP_JS_KEY` loads Tencent JavaScript API GL only for visualization, while POI/geocoding/route data and the WebService `TENCENT_MAP_KEY` remain server-owned. Live/cache server results no longer force `MapWebView` back to its offline grid, and route geometry drives viewport center/zoom when present.
- Address-origin POI rendering preserves the server's degraded `tencent_stale`/`server_mock` warning when the final station-count status is displayed.
- The legacy direct-Tencent `map-service-tests.pro` target now implements the two post-PR #17 `IMapService` address overloads with real geocode-then-query compatibility logic, and its coordinate calls are explicit `GeoCoordinate` values. It remains opt-in and isolated; production user-client map traffic uses `ServerMapService`, with `server-map-service-tests` as the primary map adapter gate.
- C admin client has a qmake shell, repository boundary, login flow, management actions and a verified `SocketAdminRepository`/`socketparse` adapter; `EV_ADMIN_DATA_SOURCE=socket` is the final presentation path and Mock remains an explicit offline fallback. The phase-1 batch is merged to `main` through PR #11; PR #13 subsequently merged the full-scope, cursor-paged `admin.pile.list` implementation. 2026-09-08 起的销售业绩（近 7/30 日营收）特性在 `feature/admin-revenue` 分支上实现（详见「2026-09-08 管理端销售业绩」节）。
- The clean-database server path can load `EV_DATABASE_SEED_PATH` once during initial creation; existing databases are not reseeded.
- Development seed stations 1 and 2 now use Shenzhen demo metadata and coordinates so the seeded inventory and Tencent-imported simulated stations share one map region. Their stable IDs, piles, orders and regression contracts are retained; this is a metadata migration rather than a cascading inventory deletion.
- The corrected PR #14 contract is documented in `docs/architecture/map-service-protocol.md`; it preserves the current `admin.pile.list` cursor/1 MiB contract and adds bounded paging, explicit map idempotency, canonical map errors, and an independent cloud `pile-simulator` proposal boundary. The deterministic `EV_MAP_SERVER_MOCK=1` handlers, production `HttpTencentClient`, map-only cache with live SQLite aggregation, audit pagination, Schema v0.4, pile generator, and internal gateway are implemented. Asynchronous worker isolation, cache-miss coalescing, retention cleanup, and private mTLS transport remain open.
- 2026-09-09 follow-up fixed the Tencent place-search boundary to the official `nearby(...)` syntax, normalized valid single-point zero-length routes to two protocol points, and audited route preflight/polyline failures. Real Tencent POI and route integration passed with a locally injected key; the key is not retained.

## Current status

- 2026-09-12：已根据新增课程说明、`计划概览` 和 `04.数据集最终版/` 形成第二阶段计划与实现指南：`docs/release/stage2-plan-2026-09.md`、`docs/release/stage2-implementation-guide.md`。S2 重点为 PySpark 数据质量发现/清洗、ODS→DWD→DWS→ADS、Flask+ECharts 分析展示及 1/6/24 小时负荷/空闲桩预测、低拥堵推荐和负荷预警。数据分析口径已登记：订单样本日期为脱敏 `0014/0015`、费用大量为 0，电池遥测 `record_time` 无有效时间轴且无站点外键，必须先做质量隔离和相对日期归一化。
- 2026-09-14：负责人确认二阶段分析主数据必须符合第一阶段 SQLite Schema v0.4，数据可由团队自行确定性生成；生成器从 schema/seed 构造合法业务库，质量问题只注入入库前原始事件层，清洗后的 SQLite 通过约束、触发器、外键和 `revenue_daily` 校验。完整默认批次已生成（120 用户、12 站点、96 桩、25,000 完成订单），SQLite SHA-256 为 `0199caafcf3698e7eee0b7404d96c83f66d1112f2d4f4a6215d36b96f7b77d1a`。
- 2026-09-14：S2 数据链路已实现并验证：ODS→DWD→DWS→ADS 输出 25,005/11,435/90 行（日营收），质量隔离有效 24,997 行、无效 8 行；Spark MLlib 时间顺序训练 9,160/2,275 行，MAE 55.83、RMSE 74.12；1/6/24 小时预测、低拥堵推荐和历史 P95 预警 JSON 已生成，Flask 分析及 `/api/v1/dashboard/snapshot` 接口契约冒烟通过。ECharts 大屏主数据现按运行时地址读取 Schema v0.4 SQLite 快照，预测/推荐/预警读取 ADS 产物；服务不可用时显示降级状态而不阻断基础指标。运行产物全部位于仓库外，未写入业务 SQLite。
- 2026-09-14：Dashboard 新增“二阶段大数据分析工作台”，通过八个交互标签切换机器学习预测、用户分群、设备运行、订单趋势、能源画像、收益趋势、站点排行和评价/服务代理指标；快照端新增 `analytics` 结构，评价数据缺失时明确标注代理口径，不伪造评分。

- `A-S1-01` (需求矩阵/边界/任务记录)、`A-S1-02` (Mock baseline + `SocketUserService` 覆盖 B PR #4 用户契约) 已完成；`A-S1-03` 真实 Socket 适配已随 PR #9 合入 `main`（`e577baa`，2026-09-05/06），含 P1 修复：UI 线程 Socket 异步化（QtConcurrent + generation 防旧回包）、mutation 请求 ID 跨可重试失败保留、`pending_reservation` 恢复、免密手机号登录与注册入口移除。`A-S1-04` 跨模块最终回归待进行。A 用户端 Mock 地图页面深色圆角下拉样式沿用。
- 2026-09-10 user-client map display fix is implemented on `user-client-final-polish`: `EV_MAP_SERVER_MOCK=0` runtime Socket probes returned `tencent_live` Shenzhen POIs and a valid two-point driving polyline; the prior missing base map was caused by `loadTencent()` being hard-disabled and live server sources being forced back to offline rendering. Source and ignored `build/final` binary are updated; final GUI acceptance still requires a runtime JavaScript API GL key.
- B PR #4 提供 Schema v0.3 protocol/database 基线；PR #8（`994e5ff`）恢复统一 admin/dashboard UI 及其评审修复。PR #12、PR #13 已合入，分别提供 admin 会话/管理接口和 1 MiB 安全的全量 `admin.pile.list` 游标分页；PR #19 提供 Schema v0.4 地图服务、缓存/审计、站点导入、桩生成和模拟器网关，PR #20 已将持久桩模拟器集群链路合入当前 `main`（`0b2576d`），PR #21 随后修复旧地图适配器测试兼容性。
- The 2026-09-04 final-decision addendum in `docs/meetings/protocol-summary-2026-09-02.md` overrides the older stop-release/frozen wording; `docs/architecture/protocol.md`, A's `SocketUserService` and C's Mock are aligned to it.
- C phase-1 批（管理操作 + Socket 适配 + Task-12 交付文档）已通过 PR #11 合入 `main`。**评审轮次：①（9/6，A）服务端 token 契约（PR #12）与 administrator_id-only 适配不匹配 → 已修并推送（`fc9d28c` merge main + `ec09270` token 适配，9/6 21:48；LoginResult.token / buildPayload 附 token、mutation 附 administrator_id / 1100 清会话；fake-server sa 15/15 + 真实 main 服务端冒烟，逐字证据见 docs/requirements/current.md §2）；②（9/7，A）文档问题（sa 计数、README 环境变量用法、证据外部路径与闸门预跑记录）→ 本批 docs commit 修正；③（9/7，B 三连）重启按钮态/Mock 同态语义（`f62ee97`）、inactive 站 fan-out 与 has_data（`a7706b3`）、口径 A 全量桩视图（初版 `admin.pile.list` 后续已修为 1 MiB 安全的游标分页聚合；sp 10 / sa 16）。**
- 9/7 18:00 接口闸门以登录/概览/桩状态/动作为准；管理端已具备 token 契约适配，闸门当天以 main 服务端真联调验证。

## A-S1-02 delivered scope

- User-window navigation with a 420×760 mobile-style default and responsive resizing (minimum 340×560, no forced aspect ratio), centralized `SessionManager`, phone-only login/logout and 11-digit ASCII phone validation.
- 2026-09-10 user-client UI pass is implemented on `user-client-final-polish`: order/profile pages use scrollable content, station/map lists use expanding or preferred size policies instead of rigid height caps, and shared input/button/combobox tokens provide 40px controls, larger popups and visible focus states. Bottom navigation remains fixed while dense pages scroll.
- Deterministic Mock station/pile query with loading, empty, unavailable, timeout and service-error feedback; station cards show dynamic idle/total counts and pile details show type, power, status and price.
- Adapter-only order flow: create/reserve, start charging, stop charging, settle, cancel reservation, current-order status and newest-first completed history with completion time, station address and amount.
- Mock/offline navigation route with explicit fallback labeling; Socket mode may load the base map with a deployment-only `TENCENT_MAP_JS_KEY`, while the server-owned `TENCENT_MAP_KEY` never enters the client. No real key is stored in source, logs or Git.
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
- 当前实现边界：Python 端已增加 SQLite-free `SimulatorCluster` 常驻循环：复用现有 `EV_SERVER_HOST`/`EV_SERVER_PORT`，注册并接收服务端快照，接收 `simulator.command` 并 ACK，周期提交 `simulator.tick`，断线后重新注册。服务端 `main.cpp` 已将 `simulator.register/snapshot/tick/pile.report/command.result` 接入公共 dispatcher，新增共享会话注册表；现有用户端/管理端消息和配置架构未改动。服务端仍是唯一状态写入者，长期 mTLS、heartbeat 指标和 stale 冲突自动重算尚未实现。
- 2026-09-10 P1 修复：`DeterministicPlanner` 先过滤非 active 或 seed 不匹配站点，再登记 `expected_versions`；因此单个 inactive/不同 seed 站点不会阻断其他匹配站点。新增两条 Python 回归，覆盖 matching active + inactive 与 matching active + different-seed，确认 matching station proposal 可继续被接受。
- 已验证：`services/pile-simulator` 的 Python unittest 6/6 通过（生成、transition、planner、cluster 命令向量及 inactive/different-seed 版本守卫回归）；`qmake6` Qt 6.2.4 配置 `server/tests/simulation_gateway.pro`，`make -j2` 后运行 `simulation-gateway-test database/schema/schema.sql` 通过。另以 `qmake6 server/server.pro` + `make -j2` 构建服务端，用户端主程序 `qmake6 apps/user-client/user-client.pro` + `make -j2` 已通过，`server-map-service-tests` 在 `QT_QPA_PLATFORM=offscreen` 下 6 通过/1 跳过，真实服务端 `smoke.py` 通过。PR #21 后旧 `map-service-tests.pro` 已可由 qmake6 + `make -j2` 构建；安装 `libqt6webenginecore6-bin` 后，`QT_QPA_PLATFORM=offscreen QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu ./ev-map-service-tests -txt` 全部 11 项通过、1 项按 Protocol v1 服务端边界跳过、0 失败，WebEngine 离线冒烟已恢复。

## 2026-09-08 管理端销售业绩（近 7/30 日营收）—— `feature/admin-revenue`（PR #16 open）

- **状态（2026-09-09 刷新）**：PR #16 open，10 commits 已推 GitHub 并 rebase 至当前 `main`（`94d8f21` = PR #15 merge）；feat 批 `8ed93f3`（feat(admin-client): 销售业绩近7/30日营收页与概览营收融合卡（QtCharts），含测试）→ 评审修复批（QCategoryAxis 类目 / clearSeries 清轴 / README 包名 / ¥ 正立）。**评审 Blocking（¥ 布局）修复**：原 setVisible(false) 隐藏 QtCharts 内部标题 item 会触发 VerticalAxis::sizeHint()/updateGeometry() 的 isVisible 闸门（轴宽归零 + 标题几何冻结）→ 修为 `setOpacity(0)` 透明方案（布局照常维护），新增回归 `revenueFullChartKeepsYTitleAnchorAcrossResize`（resize 后标题几何不冻结，RED→GREEN 实测）。
- **范围红线（2026-09-08 用户确认）**：只做近 7/30 日营收；今日/本月/历史总营收、同比/预测/导出、站点筛选、订单级明细均不在本次范围。wire 协议零改动；不修改 server/database/用户端/Web 业务行为（Web 大屏本特性未动）。A-02 视为**部分完成**（7/30 日部分），不标全量通过。
- **UI**：概览第四张营收卡改为 `RevenueMetricCard` 融合卡——近 7/近 30 两行金额可点击切换（透明热区承担点击/键盘/tooltip/焦点环），上方固定大字、下方固定小字，点击释放后交换日期和金额及热区位置；悬停/按下不切换，背景反馈 alpha=0.3；排版使用完整视口，避免局部重绘引起字号变化；新增销售业绩页 `RevenuePage`（两张合计 `MetricCard` + Full 趋势图 + 每日营收表 + 选中范围更新时间），与概览/充电桩/充电站/用户管理并列导航五项（`PageIndex` 命名枚举替换裸数字；navList spacing=18=原 14×1.3 用户指定，垂直滚动条 AsNeeded）；概览卡「详情」入口携带当前 7/30 范围跳销售页并预选该范围；概览演示控件仅 Mock 数据源下显示（Socket 模式隐藏，错误文案统一「接口错误：概览加载失败」）；登出时 overview/revenue 双页 `invalidatePendingLoads()`，旧 generation 迟到回包作废。
- **图表（视觉决策 2026-09-08 用户确认）**：共用 `RevenueChartWidget`（QChart 两模式）——**Mini**（概览卡）：折线 alpha=0.3 `kDayFocusBlue` 的低透明度背景层，轻量坐标网格手绘（结构色 45%~65% 不透明度；Mini 轴对象整体隐藏使 plotArea≈视口，消除隐藏轴占位内缩），Y 轴聚焦数据带（上下各 15% 余量）使折线垂直居中，金额文字经前景回调画在**最上层**（主金额 ~92% 近实色正文、次金额 85% `mutedText`）；**Full**（销售页）：纵轴单位为 `¥`，Y 自 0 真实零基线、横轴为 UTC 儒略日数值（不受本机时区影响）稀疏日期标签（7 日逐日 / 30 日约 6 个含首尾）、hover 按 X 找回 `RevenueDay` 用原整数分显示精确金额（不从浮点反算）。
- **数据层**：`adminmodels.h` 新增 `RevenueDay`/`RevenueSeries`（range/days/totalCents/updatedAt/available/error）与 `OverviewStats.revenue7dSeries/revenue30dSeries`；socketparse 新增严格 `parseRevenueSeries`（range 回声==请求值、条数 7/30、date 为 UTC 日历日连续升序且末日==updated_at 当日、金额非负整数且 ≤ 2^53-1、逐日之和==revenue_cents 合计；任一不符 → 该序列 `available=false` 并给出中文原因，**不补 0、不静默**）；`SocketAdminRepository::fetchOverview` 双请求（7d+30d）各自保留**完整序列**与各自 updatedAt（两 range 快照时间可能不同，**不宣称同快照**；摘要口径仍以 7d 主体为准），序列坏只影响营收区（卡内「趋势暂不可用」+ 重试入口），不清其它摘要；mockdataset 的 30 日序列与 `dashboard/data/demo.json` `revenue30dCents` 逐值一致（和 983840 / 286540 分，由 tst_ui `mockRevenueSeriesAreConsistent` 锁定）。
- **环境**：Windows 本机已装 QtCharts（Qt 6.2.4 MinGW，`aqt -m qtcharts`；头文件 / `qt_lib_charts.pri` / 库三处验证）；src、tests、ui、loginflow 四个 .pro 均 `QT += charts` 并加入新源文件（revenuepage/revenuemetriccard/revenuechartwidget）。GUI 样例截图在仓库外 `D:/work/chargingplatform/build/revenue-snapshot/out/`（不入 git）。
- **测试（Windows 实测，数字逐字）**：tst_ui **34** / tst_launchsmoke **6** / tst_loginflow **7** / tst_socketparse **12** / tst_socketadapter **19**（改造前基线 24/6/7/10/16；ui 34 = 33 + ¥ 标题布局回归）；新用例清单与输出文件见 `tests/integration/role-c-regression.md` §5。
- **验证进度（2026-09-09 目检更新）**：Ubuntu VM 构建✓（09-09 09:24 重建含 ¥ 修复的 admin-client）与 GUI 桌面目检✓（Mock 模式，用户确认 ¥ 正立/布局正常）；VM 五套 QtTest 复跑与真实服务端 Socket 联调（7d/30d 双请求）待收尾。

## 2026-09-09 管理端真实地图渲染（站点态势双模式）—— `feature/admin-client-tencent-map`（未提交）

- **状态三块式**：① 已提交进 PR：无（分支 0 commits，基于 `f5af4a1` = PR #16 merge）；② 仅本地工作树：
  T1–T3 代码与测试 + T5 文档（11 文件：`widgets/staticmapviewport.{h,cpp}`、
  `widgets/staticmapimageprovider.{h,cpp}`、`widgets/stationtopologywidget.{h,cpp}`（双模式改造）、
  `pages/overviewpage.cpp`（接线，页面唯一改动点）、4 个 .pro（src/tests/ui/loginflow 注册 +
  QT += network）、`tests/ui/tst_ui.cpp`（+31 用例）、`docs/ui/README.md` §6.2、
  `docs/architecture/map-service-protocol.md` §1 底图例外补录、`config/example.env`、
  实施计划 `docs/role-c-admin-map-renderer-plan.md` 状态翻已实现）；③ 后续待办：用户审查后按
  commit 边界分批提交（T1→T2→T3→T5 单 PR）、T6 真图
  联调（key+网络、双平台、降级三态冷启动）、附录 B 坐标通道、合入门禁（协议补录经 B 评审）。
  **T4 GUI 冒烟（2026-09-09 VM 目检通过）**：Ubuntu VM 同步 4 commits 整树 + 重建
  admin-client（exe 11:43，staticmap 符号验证在场），bit 桌面 Mock 模式起 GUI（无 key →
  静默拓扑），用户目检概览站点态势拓扑一致性（节点/呼吸/键盘/点击）确认无回归；真图留给 T6。
  **T6 Windows 真图联调（2026-09-09 目检通过）**：server-main 重建（9/8 源码落后 5min 修复）+
  全新库 t6-live-0909.db 起真实服务端（login/station.list 冒烟 OK）→ 附录 B 通道二校准坐标
  （站1 41.714729,123.449597 / 站2 41.805727,123.440030，复核逐字）→ GUI Socket 模式 + key
  冷启。**实证两件事**：①降级三态之三：首次冷启腾讯返回 status 112（宿主出口 IP 已从白名单
  36.110.14.128 漂移为 36.110.14.171——计划 §7 风险第一行应验）→ GUI 自动落拓扑 + 服务降级
  标注（用户目检确认降级路径真实生效）；②用户腾讯控制台补白名单 36.110.14.171 后重启冷启 =
  真图（沈阳静态图 597KB PNG）→ 用户目检真图 + 两站标记贴合通过。**UI 微调（目检反馈，commit
  `17619de`）**：底图透明度 0.4（setOpacity 隔离，仅底图）、真图模式站名纯黑（拓扑保持
  mutedText 原色）、站名字号 11→13px——用户逐项确认；tst_ui 71/71 复跑全绿。
  **T6 VM 侧（2026-09-09 完成）**：guest 桥接网段外网不通（DNS 失败）→ 用户 VMware 界面热切
  NAT（192.168.182.128）→ 腾讯预检 200+PNG（NAT 出口=宿主白名单 IP）；guest server 全新库
  ev-t6-live.db + 坐标校准 + GUI Socket+key 目检通过（与 Windows 同配方、同微调版）。
  **VM 五套暴露并修复一个平台差异 bug（commit `8859570`）**：providerCancelAllStopsDelivery
  在 Ubuntu SIGSEGV（Windows 全绿）——FakeStaticMapServer 延迟回包 singleShot lambda 捕获裸
  QTcpSocket，客户端 abort 断开后 socket 被 deleteLater、400ms 后到期访问悬垂指针（Windows
  断开通知时序晚于回包未现）；修复 = QPointer 守卫。修复后 VM tst_ui 71/71，双平台五套
  71/6/7/12/19 逐字一致，T6 闭环。
- **实施计划**：`docs/role-c-admin-map-renderer-plan.md` v1.3（两轮评审 + 复审修正定稿；T0/T0b
  实证先行，边界决策 D1–D7 已拍板——底图客户端自理、业务数据全走 Socket、key 仅 env、
  无 datum 补偿、D7 精确投影式锁定）。
- **T1 纯函数 core**（`staticmapviewport`）：取景（bbox + 8% 边距、最大适配 zoom ∈ [10,17]、
  单站 zoom14、单轴零跨度不退化单站）、D7 精确投影、稳定指纹（center 5dp 量化）。
  13 用例锁定文档向量一次全绿：校准站对 → zoom12/center、z13 +0.01° → +58.254222/−78.052375px
  （±0.0001）、z12 减半、85.05112878 边界、wide-span 全式 vs 导数近似差 0.123px（实测打印）。
- **T2 图片提供者**（`staticmapimageprovider`）：`MapImageProvider` 抽象（fetch/cancelAll/
  canFetch + Failure 分类）+ `StaticMapImageProvider`（QNAM、5s 传输超时可注入、构造显式收
  key/baseUrl **不读 env**、空 key 空转不建网络对象、URL/key 仅内存禁打印）。实现修正：
  腾讯业务拒绝以 **HTTP 200 + JSON status 121** 返回（T0 实证），成功分支先查配额码再解码图像，
  否则误判 BadImage。6 用例全走本地假 HTTP 服务器（QTcpServer 回环）：URL 形态、空 key 短路、
  403+121 → Quota、500 → Http、静默超时 → Network、坏图 → BadImage、cancelAll 停交付。
- **T3 控件双模式**（`StationTopologyWidget` 改造 + OverviewPage 接线）：净化（setStations 入口
  剔除非法坐标站，stationCount/取景/布局/点击/键盘同源）+ 真图触发状态机（触发点 = 数据到达 /
  resize 32px 尺寸桶，debounce 300ms；指纹缓存仅完全匹配复用；同视图在途复用；目标失效
  generation 立即作废 + cancelAll；失败 30s 时间门 + 注入时钟；QPointer+seq 回调双保险）+ 三种
  降级标注（服务不可用 / 超投影范围 / 区域过大）+ 图例区替换展示。接线 = OverviewPage 构造注入
  `new StaticMapImageProvider(qEnvironmentVariable("TENCENT_STATIC_MAP_KEY"))`（空 key 拓扑
  静默，行为不变）。12 用例覆盖：无 provider 回归锁、投影点=独立期望、点击/键盘复用、
  降级标注、时间门、跨视图缓存不误用、superseded 丢弃、同视图在途复用、极区站保留拓扑、
  resize 桶触发、标注清除、非法坐标净化。
- **测试（Windows 实测，数字逐字）**：五套 = tst_ui **70**（34 + 31 新 + 5 评审回归）/ tst_launchsmoke **6** /
  tst_loginflow **7** / tst_socketparse **12** / tst_socketadapter **19**，全部 `env -u
  TENCENT_STATIC_MAP_KEY`；`EV_UI_REDUCED_MOTION=1` 复跑 tst_ui 70 全绿；零真实网络（widget
  测试注入 Fake、provider 测试指向本地假服务器）。既有拓扑 3 用例零改动回归锁。
- **评审修复（2026-09-09 静态审查 5 条，全部修复 + 回归锁定）**：P1-1 视图失效未停 debounce
  （旧目标在 timer 到期后被重发）→ `discardPendingMapFetch()` 统一失效出口（++seq + cancelAll +
  停 timer + 清待发目标），回归 `debounceStoppedWhenTargetBecomesUnreachable`（清空/超投影两变体，
  修复前 RED）；P1-2 切换区域时旧底图仍作当前可用（A 图承载 B 标记窗口期）→ 目标离开缓存视图即退役
  （`m_mapAvailable=false`），缓存命中改为"指纹匹配即可重新激活"（回切原区域免重拉），失败/替换
  provider 才清缓存（fp+image），回归 `staleImageRetiredWhileSwitchingRegion` + 既有 resize 回桶
  用例锁定；P1-3 `cancelAll` 遍历容器被同步 finished 回调修改（UB）→ 快照 + 先摘除容器再逐个
  abort，回归 `providerCancelAllMultipleInflightSafe`（双在途 + 取消后可继续请求）；P2-1 控件
  <320×240 时拉取的图大于控件 → paint 偏移为负被跳过（图不绘制 + 投影错位）→ 请求尺寸门槛与
  clamp 下限一致（320×240），回归 `smallWidgetStaysTopologyWithoutRequest`；P2-2 transferTimeout
  只在数据停滞时触发（持续滴答慢传输永不超时）→ 增加总时长 deadline 定时器（挂 reply 名下，
  到期 abort → 归 Network），失败分类改按 QNAM 错误码区分传输层/服务端（响应头已收但本地中断 ≠
  Http），回归 `providerTotalDeadlineFiresOnDripTransfer`（假服务器 drip 滴答模式，修复前 RED）。
- **T5 文档（本批）**：docs/ui/README.md §6.2（双模式/降级/key 类型与隔离/D7 公式/触发去重
  竞态/测试计数）；config/example.env `TENCENT_STATIC_MAP_KEY`（WebService 型、IP 白名单、
  配额方案绑定、与 Web JS key 隔离）；map-service-protocol.md §1 底图例外补录（附录 A 文本，
  待 B 评审——合入门禁 2）；本文件本节省。

## Validation and evidence

- Ubuntu qmake6 (Qt 6.2.4) server build and real-server `admin.py`/`smoke.py`/`concurrency.py` regression pass on the pre-map baseline; the fixed server build, `server/tests/tencent_client.pro` fake HTTP test, real Tencent `map_live.py`, `server/tests/map.py` mock/cache/audit regression, `server/tests/simulation_gateway.pro` direct gateway test, protocol tests, database schema tests, and Python simulator tests pass. Admin-client QtTest suites remain green (`tst_ui` 24, `tst_launchsmoke` 6, `tst_loginflow` 7, `tst_socketparse` 10, `tst_socketadapter` 16). User-client GUI evidence remains a separate desktop/VM check.
- C phase-1 batch is green on Windows and Ubuntu VM identically: tst_ui 24 / tst_launchsmoke 6 / tst_loginflow 7 / tst_socketparse 10 / tst_socketadapter 16 (9/7 review round 3: restart button state / Mock same-state semantics / admin.pile.list full-scope fetchPiles / has_data mapping; sa/sp counts updated after new cases). Web dashboard: node 35 + serve `--check` green.
- **feature/admin-revenue（PR #16，2026-09-09 刷新）Windows 实测**：tst_ui 34 / tst_launchsmoke 6 / tst_loginflow 7 / tst_socketparse 12 / tst_socketadapter 19（改造前基线 24/6/7/10/16；销售业绩特性新增 9+2+3 例 + ¥ 回归 1 例，见 `tests/integration/role-c-regression.md` §5）。Ubuntu VM QtTest 复跑与真实服务端 Socket 联调**待验证**，本地产物不冒充已合入/已联调。
- C-S1-001/002 复验通过并关闭（迁移原子性三场景 / 同批坏帧保留好帧），见 `docs/release/defect-log.md`。
- Before each commit/PR, scan tracked content for credentials and inspect `git diff --check`; only placeholders may appear in `config/example.env`.
- User-client validation: Qt 6.2.4 / qmake6 clean build of `apps/user-client/user-client.pro` passed after the responsive UI change; `tests/user-client-tests.pro` passed 11/11 with 4 optional Socket integration cases skipped. The offscreen application startup remained alive without a crash (WebEngine reports the expected missing OpenGL context in offscreen mode). Map tests passed 11, skipped 1 (real-GL skipped because `TENCENT_MAP_JS_KEY` was absent); server-map adapter tests passed 6/6 with the opt-in running-server case skipped. The updated ignored final binary launched under offscreen WebEngine, and the final package SHA-256 manifest passed completely.

## Dependencies and TODO

### 2026-09-15 第二阶段收口快照（三块式）

- ① PR #24 已合入 `main`（合并提交 `1b5d56b`）：包含 analytics 数据链、发布校验、Dashboard 分析模式、ML 链修复、批次覆盖范围及复核修复证据。
- ② 已提交并推送（同分支，head `09c07be`，**5 笔 = A 对 PR #24 的 9 项评审修复批**）：`7766d32` fix(ml) 隔离 NULL 判定（非空布尔 + `NULL_*` 原因码 + 守恒/互斥/原因完备失败即中止）/ `b8fc4cd` fix(ml) 全零预测安全评分 + 四产物原子落盘 / `09f5a5a` fix(analytics) batch_id 越界拒绝（400 `invalid_batch_id`）+ 坏快照结构校验（503 `snapshot_corrupt`）+ 发布覆盖窗口 `available_*`（含 `run_pipeline.sh` 裸环境 pyspark 回退链）/ `d86a686` fix(dashboard) ML 区独立来源标注 + 默认窗口重置（`meta.default_*`）/ `09c07be` docs（`--dws`、计数口径、契约、本文件）。工作树其余仅 `docs/meetings/interface-gate-2026-09-07.md` 遗留修改（非本批）。**VM 全量复跑已完成**（见下证据行）。
- ③ 当前待办：G3 时段曲线已在 `feature/g3-time-of-day-curves` 实现，待 PR #25 审查合入；A 的 PyCharm/录屏证据与贡献度材料仍待完成。
- **G3 评审修正（2026-09-16，PR #25）**：生成器 **1.1.0**；槽内起点按 UTC 工作日/周末小时权重采样，槽位左边界不再包含 `data_end_exclusive`，末端预留持续时间与最长结算延迟。采集缺口按实际区间过滤，新增预约排除已在充电的桩，保留单桩活动订单唯一约束。low + seed `20260914` 未标脏订单每小时日均：工作日凌晨 **1.701**、早高峰 **6.063**（**3.57 倍**，修正前 **0.93 倍**）、晚高峰 **6.602**（**3.88 倍**）；周末早高峰 **3.808**、午间 **6.718**、晚间 **5.962**。合法底稿及未标脏 CSV 的开始/结束/结算均严格在窗口内。
- **G3 本轮验证**：生成器 **19/19**、覆盖窗口 **4/4**、API **24/24**、Dashboard Node **60/60**、Dashboard Python **6/6**；三个 profile 底稿边界、不重叠、活动唯一及缺测窗口通过，Windows/BitDev low 哈希与分布一致。BitDev 已补齐 PySpark **3.4.1**，使用独立检出 `/home/bit/ev-g3-pr25` 的 `8ee7a3c` 完成 low + seed `20260914` 批次 `g3-low-8ee7a3c`：HDFS/Spark/发布 **7/7**、发布验收 **30/30**、六输入逐字节一致、清洗重跑报告一致（`VERIFY_RERUN_PASS`）。五表输入 **23,455 = 保留 16,481 + 去重 80 + 隔离 6,894**；ADS 小时负荷 **15,120** 行，小时/日总电量差低于 **0.000054 Wh**；工作日早/晚高峰电量为凌晨的 **3.51/3.53 倍**。证据保存在仓库外 `../build/g3-review-20260916/`，含 `pipeline.log`、`published-verification.json`、截图与 `VM-DEPLOYMENT.md`；待 PR #25 复审。
- **G3 大屏部署**：BitDev 通过同源 analytics API 提供页面及正式 Spark 发布快照，已解决“业务数据服务地址未配置”；VM 内访问 `http://127.0.0.1:61469/?source=analytics&map=topology`，重启执行 `bash /home/bit/g3-review-20260916/start-analysis.sh`。快照位于 `/home/bit/ev-g3-artifacts`，无需重算即可启动；浏览器返回 200、26 个 canvas、无 JS 错误。当前为 G3 合成数据的批次分析，独立 ML 预测服务尚未配置。
- **评审修复批复跑证据（2026-09-16，VM Ubuntu 22.04 / Spark 3.4.1；证据包 `build/stage2/evidence/pr24-fixes/` + `s2-pr24-evidence.tar.gz`）**：六套全绿——`ml/tests` **22 passed**（含 NULL 判定落盘回归 + 守恒中止单测）、analytics `test_api` **24** / `test_coverage` **4** / `test_generator` **7**、servercfg **4**、node **60/60**；ml 链 25k 全量重跑守恒一致（输入 25,005 = DWD 19,543 + 隔离 5,462，balanced/exclusive/无效无原因=0）；修复反例对照 = 旧版 exit=1 仅 `forecast.json`、新版 exit=0 四产物齐全；新批次 `s2-fix2-20260916`（low + seed 20260914）7/7 步 + `VERIFY_RERUN_PASS`、`verify_release` **30/30 PASS**；发布覆盖窗口实测 2026-06-16→09-15（追加结算日 09-14 由 out_of_coverage 变为 **200**），默认窗口收敛 06-17→09-15 且最早日仍可查询（默认 40,274,815 + 最早日 429,455 = 发布总额 40,704,270），越界 400 / `batch_id=../…` 400 `invalid_batch_id` / 坏结构 503 `snapshot_corrupt` 全部实测。
- 历史复跑证据（2026-09-15，VM Ubuntu 22.04 / Spark 3.4.1）：`ml/tests` **18 passed**（含 3 条 Spark 用例）；全链 25k 回执 DWD 19,543 / 隔离 5,462（守恒且互斥=0）、DWS `station_hourly` 30,142 + `station_day` 1,260、训练 24,430/5,712（MAE 16.30 / RMSE 30.78）；快照与 DWS 逐站严格相等（14/14，avgUtil 0.113）；R2 复审补强：node **55/55**（含分享链接 6 用例）、`/api/*` 代理 VM 实读 200、**F6 整栈验收全绿**（真实服务 + 健康检查，含 ev-server wire 探针）、share-link E2E PASS；证据包在仓库外 `build/stage2/evidence/g2-2026-09-15/{r2,r3,f5}/`。
- 发布批次 `s2-rel-20260916`（low 档，**对应评审修复前 head `812d31f` 冻结代码**；评审修复批的发布证据以上方 `s2-fix2-20260916` 为准）：全链 7/7 步全过、`VERIFY_RERUN_PASS`（同 seed 六输入逐字节一致）；`verify_release` **30/30 PASS**；六套测试全绿（generator 7 / clean 守恒全 ok / warehouse 2 / api 16 / servercfg 4 / node 55）+ `ml/tests` 18 passed；一键启动裸 `/tmp` 首考：四服务健康 5/5 + ev-server wire 探针 rc=0、`active_batch=s2-rel-20260916`；证据包 `build/stage2/evidence/g2-2026-09-15/{r2,r3,r4-rel,f5}/`。

- `A-S1-04`: coordinated final regression, GUI evidence and clean-environment delivery (2026-09-07 gate and 09-10 integration deadline).
- C: PR #11 三轮评审修复和 PR #13 的 `admin.pile.list` 修复已合入当前 `main`；Socket 管理端、销售业绩与地图渲染均已完成本地 qmake6/QtTest 验证，发布材料已同步。（2026-09-15 更正：删去本行原有一处无代码支撑的管理端验证声称；分析入口由 Dashboard 分析工作台承担，管理端不含该入口。）
- B (owned): PR #19 已将地图服务、Schema v0.4、缓存审计、站点导入、模拟器网关和生产 Tencent HTTP adapter 合入当前 `main`；异步 worker、缓存 miss 合并、清理任务和生产 mTLS 仍是开放项。
- C: `feature/admin-revenue`（PR #16，销售业绩近 7/30 日营收）已完成评审修复；后续仅需在目标发布环境复核双平台 GUI 证据。
- B (owned): 当前分支已落地地图契约的 v0.4/Mock/cache/audit/import/generator/gateway 和生产 Tencent HTTP adapter；桩模拟器演示集群已接入公共 TCP dispatcher（注册/快照/命令 ACK/tick），并已兼容 PR #17 的用户端服务端地图适配；后续优先级是 stale 冲突自动重算、私有 mTLS listener、异步 worker、并发 miss 合并、清理任务和最终 A/C 联调。
- Open technical item: move slow database work off the Socket event-loop thread, or define a bounded worker/lock strategy (B-owned).
- Open data decision: Schema v0.4 业务表不包含用户评价/服务评分；当前服务页展示完成率、非取消率、复购率和时长等可追溯指标。若启用独立评价源，评价事实应作为业务库外的可复现分析输入，不回写 Schema v0.4。
- Official Tencent key smoke check now reaches the upstream endpoint: geocoding and driving route both succeed with `tencent_live`; the POI search endpoint independently returns provider status 121 (daily quota exhausted), mapped to `MAP_QUOTA_EXCEEDED` (1404). Fake HTTP and full production-selection integration pass.
- B map work is tracked in `docs/role-b-map-service-plan.md`: protocol/size guard → v0.4 migration → map Mock/cache/audit → station/pile import → handlers → simulator gateway/cloud simulator → validation. The cloud simulator never writes SQLite directly; the server remains the sole business-state writer.
- 2026-09-09 review follow-up: confirmed the P1 page-cache continuation bug with `page_size=1`; cache hits now reuse the cached page's `has_more`/`next_page_token` and re-aggregate only that page's stations. `server/tests/map.py` covers page-1/page-2 cache hits and request replay; qmake6 server build and mock map regression pass. Ubuntu QtCharts is installed as `libqt6charts6-dev` and the admin qmake6 tree now builds with `QT += charts`.
- 2026-09-09 Tencent pagination review follow-up: production POI search now reads Tencent `count` and drains provider pages (`page_index`) before applying server pagination. Fake HTTP and production-selection integration cover 21 records across two upstream pages; mock map cache regression remains green.
- S2 intelligent-analysis chain: data preparation → model-service contract → predictions/recommendation/warning → Flask API → Dashboard and Qt admin summary → integrated validation. The data/model/API/UI chain is runnable; remaining work is production deployment hardening and optional Vue shell only if course acceptance explicitly requires it. It does not block the S1 charging loop.
- Environment note: system `node`/npm 与 `pytest` originally absent; `scripts/setup_stage2_env.sh` uses project-external `/tmp/ev-node` and `/tmp/ev-s2-site` mirrors, with Dashboard Node tests and Python tests executed there.
- Environment update: Node.js 20.18.1/npm 10.8.2 installed from npmmirror under `/tmp/ev-node`; Dashboard Node tests now 35/35 passed. System `node`/npm remains unchanged; use `PATH=/tmp/ev-node/bin:$PATH` or install with the commands in `ml/README.md`.
- 2026-09-14 final build verification: `qmake6 --version` = Qt 6.2.4; clean `/tmp/ev-s2-qmake` builds of `server/server.pro`, `apps/user-client/user-client.pro` and `apps/admin-client/admin-client.pro` succeeded. Admin QtTest suites passed 6/72/7/12/19 (offscreen；2026-09-15 更正：原文该计数与实际不符，已按最近完整复跑更正). Final presentation guide now includes Schema v0.4 data generation, Spark training, Flask services and Dashboard startup commands; see `docs/release/project-demo-guide-2026-09-08.md`.
- 2026-09-14：新增 `ml/service/build_dashboard_snapshot.py` 和 `/api/v1/dashboard/snapshot`，Dashboard 主数据从验证通过的 Schema v0.4 SQLite 快照读取（14 站点、102 桩、30 日营收和小时负荷），`demo.json` 降为离线故障演练入口；`scripts/setup_stage2_env.sh` 提供清华/npmmirror 环境初始化。
- 2026-09-14：以生成的 Schema v0.4 分析库直接启动 `/tmp/ev-s2-qmake/server/ev-server`（端口 45455），`EV_DATABASE_PATH` 同步传给 `server/tests/smoke.py` 后真实 Socket 全流程冒烟通过；最终呈现可让 Qt 用户端和管理端与分析 Dashboard 共享同一数据口径。
- 2026-09-14：扩展 Dashboard 二阶段分析工作台和快照 `analytics` 结构，增加机器学习预测、用户、设备、订单、能源、收益、站点及评价/服务代理指标八个交互视角；新增快照契约测试，Python 10/10（server_config 4 + ui_tokens 6）、Node 48/48 通过（2026-09-15 收口复跑口径；原文计数与实际不符，已更正）。
- 2026-09-15：按二阶段独立工作台方向重构 Dashboard 信息架构：新增侧边导航与 hash 切页（网络总览、智能预测、用户与设备、订单与能源、收益与站点、评价与服务），分析页按相关域共享一页并增加 KPI 摘要条；中小屏自动转为横向导航，保留 ECharts 与 Schema v0.4 数据口径。Node 全量测试 48/48 通过（2026-09-15 收口复跑口径）。
- 2026-09-15：优化二阶段数据真实性与挖掘深度：生成器改为 78% 完成、15% 取消、7% 异常的确定性订单漏斗，业务库/账本约束仍通过；快照新增 RFM 用户分层、设备充电次数 z-score 异常检测、站点高负荷/均衡/低负荷聚类标签，以及订单完成率曲线。Dashboard 增加完成率趋势、方法说明与 KPI，Python 生成回归通过。
- 2026-09-15：修复一键启动复用旧数据导致的“页面未更新”问题：`scripts/start_stage2.sh` 新增 `.stage2-data-version` 指纹和 `EV_S2_REFRESH=1` 强制刷新开关；检测到版本变化时自动重建 Schema v0.4 数据库、ODS、Spark 分层、模型和 ADS。（2026-09-15 R2 复审补强：版本指纹并入 ml 四脚本 sha256——代码变即触发重建；新增 Java/Spark/pyspark 裸环境回退链（ssh/nohup 等非登录环境可直接启动）；服务端口参数化 `EV_*_PORT`；整栈验收（真实服务 + 健康检查）全绿，证据 `build/stage2/evidence/g2-2026-09-15/r3/f6-realstack.log`。）
- 2026-09-15：Dashboard 数据挖掘扩展：总览新增健康评分、z-score 异常、站点聚类、峰谷时段 4 个摘要模块；预测、用户设备、订单能源、收益站点、评价服务 5 个工作台页均扩展为至少 4 个子模块。快照新增订单漏斗/履约时长分布、OLS 营收趋势、站点聚类中心与 Pareto、能耗相关系数、模型验证元数据等可追溯字段；评价事实缺失继续明确显示代理口径。
- 2026-09-15：修正 Dashboard 运行时密钥边界：浏览器只读取独立的 `TENCENT_MAP_JS_KEY`，服务端 WebService `TENCENT_MAP_KEY` 不再注入 `/runtime-config.js`；同时修复 RFM 合法 `recency_days=0` 被错误转换为 999 天的问题，递增 S2 数据版本指纹并补充确定性快照回归，确保旧产物自动重建。

## Collaboration and security rules

- Work on task branches and deliver through Pull Requests; do not push directly to `main` or force-push.
- Any code or architecture change must update this file and the relevant design/API document, keeping only current, actionable information.
- All Qt/C++ build and test evidence must use `qmake6`; CMake is not an accepted project path.
- Never commit Tencent Maps keys, passwords, tokens, private keys, runtime databases, logs or generated build output. Real map credentials stay in ignored local configuration.

## Recent history

- 2026-09-14：统一 Dashboard 正常运行文案为 Schema v0.4/业务快照口径，并将最终呈现指南改为真实 Socket + Flask 链路。（2026-09-15 更正：删去本行原有一处无代码/用例支撑的管理端声称；管理端 QtTest 最近完整复跑为 6/72/7/12/19。）
- 2026-09-14：新增 `scripts/start_stage2.sh` 一键启动脚本，按需准备国内镜像依赖、生成/复用分析产物、构建 Qt 并启动 Socket、Flask、Dashboard 和桌面客户端；三个腾讯 Key 仅保存在被忽略的本地 `config/local.env`，脚本和日志不输出密钥。

- 2026-09-14：完成 S2 首个可复现实现闭环：`ml/data/generate_analysis_dataset.py` 按 Schema v0.4 生成确定性业务库和 ODS，`ml/jobs/` 完成质量报告与 Spark 分层，`ml/models/` 完成 Spark RandomForest 训练、MLlib 推理及预测/推荐/预警产物，`ml/service/app.py` 提供只读 Flask API；`ml/tests` 5/5 通过。修复产物构建对 ADS 精简字段缺少 `device_count` 的兼容问题，并处理 PySpark 3.3.4 与 pandas 2.x 的显式 Row 转换；真实命令和指标写入 `ml/README.md`，Dashboard 已接入分析 API 状态区和结果摘要。

- 2026-09-11：按用户要求生成 B 端纯源码压缩包 `/home/bit/projects/work/build/ev-charging-platform-backend-source-20260911.tar.gz`（SHA-256 `7894225e650ad68da1dc13e94650e884d638e919a3e255ca1698566b149de24e`）。包仅含服务端、Protocol、数据库 Schema/迁移、地图、桩模拟器及必要测试/架构文档，不含客户端、Dashboard、二进制、运行数据库、日志或密钥；基于当前提交 `3c945a0`，已用 qmake6/Qt 6.2.4 构建服务端及后端测试，并通过 Python 桩模拟器/Schema 测试。

- 2026-09-10：优化用户端 UI 响应式布局：默认仍为 420×760 手机式入口，但解除 21:38 强制比例并设置 340×560 最小可用尺寸；订单/个人中心改为可滚动内容，列表取消不必要的最大高度；统一输入框、按钮、区域/路线选择栏的 40px 控件高度、间距、弹出列表、焦点和禁用态；底部导航固定可见。qmake6 主程序构建与用户端 QtTest 11/11 通过。

- 2026-09-10：修复用户端真实地图展示链路：新增独立运行时 `TENCENT_MAP_JS_KEY` 加载腾讯 JavaScript API GL；不再把服务端 `tencent_live/tencent_cache` 结果误判为必须离线展示；真实与离线路线视口均优先按 polyline 范围居中缩放。服务端仍独占 WebService Key 和 POI/地理编码/路线调用。当前运行服务端实测深圳 POI 与驾车路线返回 `tencent_live` 和有效折线；真实底图 GUI 目检待注入 JS Key。
- 2026-09-10：移除演示数据中的沈阳站点范围：`database/seeds/dev.sql` 的站点 1/2 改为深圳演示站，并同步最终构建包 seed；外部运行库同样事务迁移名称、地址和坐标，保留既有桩/订单 ID。运行库 `integrity_check` 与 `foreign_key_check` 通过，22 个站点现均落在深圳 bbox 内；原库备份位于 `/tmp/ev-charging-demo-before-shenyang-removal-20260910.sqlite`。
- 2026-09-09：PR #19 合入 `main` `005d6e8`，包含 Schema v0.4、确定性地图 Mock/cache/audit、站点导入与桩生成、模拟器内部 gateway、生产 Tencent HTTP adapter 及最终 POI 分页修复。
- 2026-09-09：接入生产 `HttpTencentClient`：服务端运行时读取 `TENCENT_MAP_KEY`，调用官方 geocoder/POI/driving/walking WebService，统一错误映射，路线分钟→秒和 polyline 解码；新增 fake HTTP qmake6 单测和生产模式 `map_live.py` 端到端测试。真实 key 未写入仓库。
- 2026-09-09：真实联调发现 POI `boundary=circle(...)` 不被腾讯接受（status 348）；已改为官方 `nearby(...)`，补充单点路线兼容及路线失败审计，修复后真实 `map_live.py` 通过。
- 2026-09-09：重新用用户提供的运行时 key 联调官方接口：地址解析和驾车路线已返回真实结果，POI 接口仍受腾讯 status 121 日额度限制；确认服务端不再走 Mock，错误正确返回 1404。
- 2026-09-09：修复地图分页缓存命中续读：缓存条目按页保存时，命中分支不再重复应用 offset，沿用缓存 continuation；新增 `page_size=1` 二次命中、续读与 replay 回归。
- 2026-09-09：本机安装 Qt 6.2.4 Charts（`libqt6charts6-dev`）；管理端 qmake6 整树构建通过，QtTest 在 offscreen 环境通过 6/72/7/12/19 用例。
- 2026-09-09：修复真实腾讯 POI 上游分页：`HttpTencentClient` 不再固定只请求第 1 页；按 `count` 续拉并在服务端切页，新增两页 fake HTTP 单测及 `map_live.py` 分页端到端覆盖。
- 2026-09-09：修复用户端地址型 POI 查询覆盖服务端降级提示的问题，最终状态保留 warning 与 POI 数量信息。
- 2026-09-10：同步 `origin/main` 至 `83449ab`（PR #17，用户端服务端地图适配）；确认 `simulator.*` 与 `map.station.search/map.route.plan` 无消息冲突。补齐模拟器命令幂等、停止后待结算占用、结算释放通知，并重新通过 server qmake6 构建、用户端服务端地图适配测试、smoke、模拟器 4/4 回归，以及真实预约→`reserve`→取消→`release` 命令镜像冒烟。
- 2026-09-10：确认桩模拟器 P1 评审属实并修复 planner 版本守卫过滤；新增 inactive/不同 seed 站点回归，桩模拟器 Python unittest 达到 6/6。
- 2026-09-10：基于当前完成版 B 端实现生成答辩汇报 PPT `docs/release/B端后端实现与答辩汇报.pptx`，覆盖服务端、Protocol v1、Schema v0.4、事务一致性、地图服务、桩模拟器、测试证据和已知边界；未沿用早期 `presentation-outline.md` 口径。
- 2026-09-10：重新构建最终演示包 `build/final/`；在干净临时目录用 qmake6/Qt 6.2.4 重建 server、admin-client、响应式 user-client、模拟器网关测试、Tencent adapter 测试及管理端/用户端地图测试，用户端服务测试 11 passed、地图 11 passed/1 skipped、服务端地图 6 passed/1 skipped、管理端测试 6/7/12/19/72 全部通过，服务端 smoke 与 Dashboard check 通过。最终包已同步到仓库外 `/home/bit/projects/work/build/ev-charging-platform-final`，用户端新二进制为 778104 bytes，两个包的 SHA-256 清单均已复核。
- 2026-09-10：同步最新 `origin/main` `0b2576d`：PR #20 合入持久桩模拟器集群链路，PR #21 修复 PR #17 遗留的旧地图测试适配器接口兼容问题。当前功能分支与本地 `main` 均已快进到该提交，本地 `current.md` 修改和未跟踪材料保留；旧地图测试 qmake6 构建通过，7/7 个非 WebEngine 功能用例通过，完整 WebEngine 冒烟仍受缺少 Qt 6 `QtWebEngineProcess` 限制。
- 2026-09-10：安装 Ubuntu Qt 6 WebEngine 运行包 `libqt6webenginecore6-bin` 后，旧地图测试在 Qt 6.2.4 下完整通过：11 passed、1 skipped（腾讯真实集成按 Protocol v1 服务端边界跳过）、0 failed；`QtWebEngineProcess` 已从 `/usr/lib/qt6/libexec/QtWebEngineProcess` 正常启动。
- B Schema v0.3 protocol/database foundation and profile/wallet endpoints are merged; its smoke and concurrency suites cover transaction rollback, replay, lifecycle, frozen policy and completed-order history. The pile-uniqueness migration `002_v0.2_to_v0.3.sql` handles already-deployed v0.2 databases (C re-verified 2026-09-04).
- A user-client Mock baseline and opt-in Socket adapter are implemented; PR #9 (P1 follow-up) merged 2026-09-05 as `e577baa`.
- PR #8 (`994e5ff`, 2026-09-04) restored the unified admin/dashboard UI (reverting PR #7's rollback of PR #6) plus the A-02/A-04/A-06/A-07 gaps, P2-01 cleanup and the AdminRepository contract-to-wire mapping doc.
- B answered the Q1–Q7 contract-freeze items (revenue_daily series / seven-day time-weighted station utilization / restart state safety), merged the original admin.* handlers with token sessions via PR #12 (`3d015f7`), and merged the 1 MiB-safe cursor-paged full-scope `admin.pile.list` through PR #13. C's Socket adapter consumes the merged contract (token + mutation administrator_id, dual-range fetchOverview, cursor-page aggregation, restart semantics identical to C's Mock).
- C phase-1 delivery (PR #11) is committed on `feature/member-c-phase1-mvp` (management actions, Socket adapter, Task-12 docs, Q1–Q7 freeze ledger, defect closures, release templates); the token-alignment fix was pushed as `ec09270` (2026-09-06 21:48), and the review round-2 doc fixes land in the same PR ahead of the 09-07 gate.
- `docs/role-a-delivery-plan.md` records A's phase-I/II dependencies, acceptance gates and delivery list; `docs/role-c-delivery-plan.md` does the same for C.
- A-S1-01/02/03 已完成（含 PR #9 P1 修复与 Socket 真实适配）；后续 A 任务包括联调测试、腾讯地图导航优化、智能分析结果展示和最终 qmake6 交付；不得将 Mock 或适配器构建通过误记为真实闭环完成。

## Async/session and permission safeguards (user client)

- `SessionManager::generation()` is an authentication generation: it changes only when `beginSession()` establishes a different identity or `clear()` logs out. Profile, avatar and wallet refreshes use `updateUser()`/local field updates and do not invalidate concurrent requests.
- `runService()` captures the auth generation and user ID, so callbacks after logout/account switching are discarded; station/pile request generations still reject older query results, and pile callbacks also verify the selected station ID.
- Frozen users may read data and perform reservation cancellation, charging stop and settlement, but UI controls for reservation creation/confirmation, charging start/direct start and wallet recharge are disabled.
- An optional discard callback restores transient UI state such as the recharge button when an in-flight request is invalidated.
- Compatibility baseline: current remote `main` is `0b2576d` (PR #20 persistent simulator cluster plus PR #21 legacy map-test compatibility fix, on top of PR #17 and PR #19). The local `feature/pile-simulator-cluster` and `main` refs are synchronized to this commit; do not claim private mTLS or async worker isolation until separately implemented.
