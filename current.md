# Current Project State

## Project and stage

- Project: 东软电动汽车充电桩应用管理平台。
- Current stage: 第一阶段最小闭环开发；真实截止时间为 2026-09-10 24:00。第二阶段截止 2026-09-17 24:00，个人报告截止 2026-09-18 24:00。
- This file was last refreshed on 2026-09-08 (PR #13 is now merged to `main` as `f3bee707`; map-service contract and B plan refreshed; late: C 的销售业绩近 7/30 日营收特性已提交至 `feature/admin-revenue`，文档随批落盘，见「2026-09-08 管理端销售业绩」节；分支随后 rebase 到当前 `main`); it was previously updated while processing PR #11 review round 3 (restart button state / Mock same-state semantics / inactive-station scope decision / has_data). The requirements source of truth is `docs/requirements/requirements-matrix.md`. 根目录的项目说明书 `.doc`、需求矩阵 `.xls` 和 `三人分工.md` 仅为本地参考文件，不上传、不提交；仓库内 `docs/` Markdown 才是正式项目材料。

## Architecture and boundaries

- `apps/user-client` (A): Qt user UI, session state, station/pile discovery, navigation entry, reservation–charging–billing–settlement interaction, profile and wallet. It never accesses runtime SQLite directly.
- `apps/admin-client` and `dashboard` (C): management UI and ECharts presentation. They consume server/provided data and do not define database or Socket rules.
- `server`, `libs/protocol`, `libs/database`, and `database` (B): Socket, authentication, business/state validation, transactions, concurrency and SQLite persistence. B also owns the pending server-side Tencent Maps contract, Schema v0.4, map cache/audit, station import, pile generation, and the authoritative simulator gateway.
- `ml` (B/C, S2): 1/6/24-hour load and idle-pile/peak prediction, low-congestion recommendation, load warning, and a callable model-service boundary.
- Mandatory build protocol: all Qt/C++ modules must use `qmake6`; CMake is forbidden as a build, test, acceptance, or release path. See `docs/meetings/build-system-protocol-2026-09-02.md`.
- B provides SQLite schema v0.3, deterministic seed/migration, protocol v1 framing/envelope/error codes, and server handlers for login, profile read/update, wallet recharge, station/pile queries, active/history orders, reservation, charging and settlement. Lifecycle writes use `BEGIN IMMEDIATE`, request-ID replay records, frozen-user checks, direct idle-pile charging and settlement rollback paths.
- A user client is a deterministic Qt Widgets + Mock implementation with an opt-in real `SocketUserService` (see A-S1-03 below). A retains Mock/offline fallback until the real Socket adapter is verified end-to-end.
- C admin client has a qmake shell, repository boundary, Mock data source, login flow and overview states, the 9/4 management action batch (C-S1-005 pile restart / C-S1-007 user freeze-unfreeze), and a local Socket adapter layer (`SocketAdminRepository` + `socketparse`, fake-server tested on Windows and the Ubuntu VM; Mock remains the default via `EV_ADMIN_DATA_SOURCE` until the gate). The phase-1 batch is merged to `main` through PR #11; PR #13 subsequently merged the full-scope, cursor-paged `admin.pile.list` implementation. 2026-09-08 起的销售业绩（近 7/30 日营收）特性在 `feature/admin-revenue` 分支上实现（详见「2026-09-08 管理端销售业绩」节）。
- The clean-database server path can load `EV_DATABASE_SEED_PATH` once during initial creation; existing databases are not reseeded.
- The corrected PR #14 contract is documented in `docs/architecture/map-service-protocol.md`; it preserves the current `admin.pile.list` cursor/1 MiB contract and adds bounded paging, explicit map idempotency, canonical map errors, and an independent cloud `pile-simulator` proposal boundary. Review follow-up now requires map caches to exclude business pile snapshots, distinguishes same-tick transport retry from new-tick stale-conflict recomputation, and fixes cross-language deterministic generator vectors. It is documentation only; no map runtime or Schema v0.4 code exists yet.

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

- Ubuntu qmake6 (Qt 6.2.4) server build and real-server `admin.py`/`smoke.py`/`concurrency.py` regression pass; admin-client QtTest build and suites are green: `tst_ui` 24, `tst_launchsmoke` 6, `tst_loginflow` 7, `tst_socketparse` 10, `tst_socketadapter` 16 (including cursor-page aggregation and 1 MiB oversized-row handling). User-client QtTest coverage and GUI startup remain separate desktop/VM checks.
- C phase-1 batch is green on Windows and Ubuntu VM identically: tst_ui 24 / tst_launchsmoke 6 / tst_loginflow 7 / tst_socketparse 10 / tst_socketadapter 16 (9/7 review round 3: restart button state / Mock same-state semantics / admin.pile.list full-scope fetchPiles / has_data mapping; sa/sp counts updated after new cases). Web dashboard: node 35 + serve `--check` green.
- **feature/admin-revenue（PR #16，2026-09-09 刷新）Windows 实测**：tst_ui 34 / tst_launchsmoke 6 / tst_loginflow 7 / tst_socketparse 12 / tst_socketadapter 19（改造前基线 24/6/7/10/16；销售业绩特性新增 9+2+3 例 + ¥ 回归 1 例，见 `tests/integration/role-c-regression.md` §5）。Ubuntu VM QtTest 复跑与真实服务端 Socket 联调**待验证**，本地产物不冒充已合入/已联调。
- C-S1-001/002 复验通过并关闭（迁移原子性三场景 / 同批坏帧保留好帧），见 `docs/release/defect-log.md`。
- Before each commit/PR, scan tracked content for credentials and inspect `git diff --check`; only placeholders may appear in `config/example.env`.

## Dependencies and TODO

- `A-S1-04`: coordinated final regression, GUI evidence and clean-environment delivery (2026-09-07 gate and 09-10 integration deadline).
- C: PR #11 三轮评审修复和 PR #13 的 `admin.pile.list` 修复已合入当前 `main`（`f3bee707`）；剩余 = 9/8–9/10 release materials and clean-environment evidence（`docs/release/stage1-checklist.md`）。
- B (owned): 原 admin.* handlers 通过 PR #12 合入 `main`，全量桩库存 `admin.pile.list` 通过 PR #13 合入当前 `main`；后续 B 负责地图契约、Schema v0.4、缓存审计、站点导入、模拟器网关和云端模拟器集成。
- C: `feature/admin-revenue`（PR #16，销售业绩近 7/30 日营收）待办 = 评审修复与双平台全量回归、真实服务端 Socket 联调营收 7d/30d 双请求；完成并评审后再定合入方式。
- Open technical item: move slow database work off the Socket event-loop thread, or define a bounded worker/lock strategy (B-owned).
- B map work is planned in `docs/role-b-map-service-plan.md`: protocol/size guard → v0.4 migration → Tencent/cache/audit → station/pile import → map handlers → simulator gateway/cloud simulator → end-to-end validation. The cloud simulator must never write SQLite directly; the server remains the sole business-state writer.
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
- B answered the Q1–Q7 contract-freeze items (revenue_daily series / seven-day time-weighted station utilization / restart state safety), merged the original admin.* handlers with token sessions via PR #12 (`3d015f7`), and merged the 1 MiB-safe cursor-paged full-scope `admin.pile.list` through PR #13 (`f3bee707`). C's Socket adapter consumes the merged contract (token + mutation administrator_id, dual-range fetchOverview, cursor-page aggregation, restart semantics identical to C's Mock).
- C phase-1 delivery (PR #11) is committed on `feature/member-c-phase1-mvp` (management actions, Socket adapter, Task-12 docs, Q1–Q7 freeze ledger, defect closures, release templates); the token-alignment fix was pushed as `ec09270` (2026-09-06 21:48), and the review round-2 doc fixes land in the same PR ahead of the 09-07 gate.
- `docs/role-a-delivery-plan.md` records A's phase-I/II dependencies, acceptance gates and delivery list; `docs/role-c-delivery-plan.md` does the same for C.
- A-S1-01/02/03 已完成（含 PR #9 P1 修复与 Socket 真实适配）；后续 A 任务包括联调测试、腾讯地图导航优化、智能分析结果展示和最终 qmake6 交付；不得将 Mock 或适配器构建通过误记为真实闭环完成。

## Async/session and permission safeguards (user client)

- `SessionManager::generation()` is an authentication generation: it changes only when `beginSession()` establishes a different identity or `clear()` logs out. Profile, avatar and wallet refreshes use `updateUser()`/local field updates and do not invalidate concurrent requests.
- `runService()` captures the auth generation and user ID, so callbacks after logout/account switching are discarded; station/pile request generations still reject older query results, and pile callbacks also verify the selected station ID.
- Frozen users may read data and perform reservation cancellation, charging stop and settlement, but UI controls for reservation creation/confirmation, charging start/direct start and wallet recharge are disabled.
- An optional discard callback restores transient UI state such as the recharge button when an in-flight request is invalidated.
- Compatibility baseline: current remote `main` is `94d8f21` (PR #15 merge); it includes PR #11, PR #12, PR #13, and PR #15. The map-service documents in PR #15 target that baseline; Schema v0.4 and map runtime remain pending. PR #16 (`feature/admin-revenue`) is rebased onto `94d8f21`.
