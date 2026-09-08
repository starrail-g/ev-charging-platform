# 管理端（admin-client）架构说明

> 对应需求：C-S1-001 ~ C-S1-009、C-S1-017 ~ C-S1-019。
> 状态：**骨架设计，随实现校正**。本文件与 `docs/requirements/README.md` 追溯表同步维护。

## 1. 边界

- 管理端是 **Linux + Qt Widgets 的 PC 管理应用**，只负责管理员侧展示与交互。
- 不在此模块实现：服务端核心、数据库访问、Socket 协议定义（归 `server`、`libs/database`、`libs/protocol`）。
- 页面类（Widget）**不得**直接创建 Socket、执行 SQL 或持有业务统计逻辑；数据一律经 Repository 抽象获取。

## 2. 分层与数据方向

``` text
┌────────────────────────────────────────────────┐
│ 页面层 (src/pages)                             │
│  login / overview / revenue / pile / station   │
│  / user                                        │
│  只做：输入校验、状态表现、交互反馈            │
└───────────────┬────────────────────────────────┘
                │ 调用（不感知数据来源）
┌───────────────▼────────────────────────────────┐
│ 数据层 (src/data)                              │
│  AdminRepository       —— 抽象接口             │
│  MockAdminRepository   —— 固定演示数据         │
│  SocketAdminRepository —— 正式 Socket 适配     │
└───────────────┬────────────────────────────────┘
                │ Socket（仅 Socket 实现持有）
                ▼
        libs/protocol（共享协议定义，不复制常量）
```

- 页面只依赖 `AdminRepository` 抽象；运行时通过工厂或配置注入 Mock/Socket 实现。
- `SocketAdminRepository` 只做协议适配与状态映射，协议常量从 `libs/protocol` 引用。

## 3. 页面清单

| 页面 | 类 | 职责 | 对应需求 |
|---|---|---|---|
| 登录页 | `LoginPage` | 账号/密码输入校验、提交中状态、错误提示、成功跳转 | C-S1-001 |
| 概览页 | `OverviewPage` | 营收摘要（第四张卡 = `RevenueMetricCard` 近 7/近 30 日融合卡，详情入口带当前范围跳销售业绩页）、桩状态摘要、站点利用率摘要、更新时间 | C-S1-003 |
| 销售业绩页 | `RevenuePage` | 近 7/30 日营收：两张合计卡 + Full 趋势图 + 每日营收表 + 选中范围更新时间；数据来自同一 `fetchOverview` 摘要，范围切换只重渲染、不新增请求 | A-02（7/30 日部分）/ C-S1-003 |
| 桩管理页 | `PilePage` | 桩列表、状态筛选、刷新、重启模拟 | C-S1-004/005 |
| 站点管理页 | `StationPage` | 站点查询和管理 | C-S1-006 |
| 用户管理页 | `UserPage` | 用户查询、冻结/解冻 | C-S1-007 |

主窗口 `MainWindow`（`src/app/`）：主导航、登录↔业务页切换、未登录禁止进入业务页、退出登录。2026-09-08 起导航为五项（概览/销售业绩/充电桩/充电站/用户管理），页面索引用命名枚举 `PageIndex`（`OverviewIndex`…`UserIndex`，替换裸数字）；登出时对概览/销售页调用 `invalidatePendingLoads()` 作废在途请求（旧 generation 迟到回包丢弃）。

### 3.1 营收组件职责（2026-09-08 新增，feature/admin-revenue 本地产物）

| 组件 | 文件 | 职责 |
|---|---|---|
| `RevenueMetricCard` | `src/widgets/revenuemetriccard.*` | 概览第四张营收融合卡（对象名 `revenueCard` 承接原样式与自动化定位）：近 7/近 30 两行金额可点击切换（透明热区 `revenue7dButton`/`revenue30dButton` 承担点击/键盘/tooltip/焦点环），200ms 字号动画（`QVariantAnimation` 单点驱动，只动字号/透明度、不给金额插值），内嵌 Mini 趋势图；选中序列不可用 → 「趋势暂不可用」+ 重试；向外发 `rangeChanged(days)`/`detailsRequested(days)`/`retryRequested()` |
| `RevenueChartWidget` | `src/widgets/revenuechartwidget.*` | 共用 QChart 折线组件（QChartView，Mini/Full 两模式），数据由调用方传入 `ev::RevenueSeries`，**不发起任何网络请求**；Mini = 低透明度背景层折线 + 手绘轻量网格（轴对象隐藏、plotArea≈视口）+ 前景金额回调；Full = Y 自 0、UTC 儒略日稀疏日期轴、hover 精确金额；非法序列由调用方先判 `available` 再 `setSeries`，`clearSeries` 一次清空 |
| `RevenuePage` | `src/pages/revenuepage.*` | 销售业绩页：两张合计 `MetricCard`（近 7/近 30 日）+ Full 趋势图 + 「每日营收」只读表（UTC 日期 + 营收元）+ 选中范围更新时间；repository 由 `MainWindow` 注入；同一 `OverviewResult` 渲染，7/30 切换不新增请求；`setRange(days)` 供概览详情入口预选范围；支持加载中/空（暂无营收统计数据）/接口错误+重试/序列坏（摘要照常、图表区提示）/零营收五态 |

数据方向不变：三个组件都不建 Socket、不写 SQL、不持有统计计算；只经 `AdminRepository` 抽象取数（Mock 或 Socket），页面从 `OverviewStats.revenue7dSeries/revenue30dSeries` 取完整序列渲染。视觉层级决策见 `docs/ui/README.md` §6.1。

## 4. 统一状态表现

所有数据页面支持四态：

| 状态 | 表现 |
|---|---|
| 加载中 | 禁用操作 + 加载提示（不阻塞界面线程） |
| 正常 | 表格/指标展示 |
| 空数据 | 明确"无数据"提示，不显示空表 |
| 失败 | 错误信息 + 重试入口，不崩溃 |

## 5. 模拟操作边界（第一阶段）

- "桩重启""冻结/解冻"为**服务端确认后的状态模拟**：Mock 实现返回固定结果；不描述为真实硬件控制。
- `SocketAdminRepository` 已接入真实管理员接口；默认仍可通过工厂切换到 Mock 演示。其
  `fetchPiles` 使用 `admin.pile.list` 的 `next_after_id` 游标页顺序聚合全部站点（含 inactive）桩；
  每个 wire 响应保持在协议 1 MiB 上限内，任一页失败时整页报错。
- `fetchOverview`（2026-09-08 扩展）：对 `7d`/`30d` 各发一次 `admin.statistics.get`，各自保留完整
  `RevenueSeries` 与各自 updatedAt（两快照时间可能不同，不宣称同快照）；序列经 socketparse 严格解析，
  坏序列仅令该序列 `available=false`（营收卡重试入口），不清除正常摘要。

## 6. 测试

| 测试 | 覆盖 |
|---|---|
| `tests/tst_loginflow.cpp` | 登录成功、密码错误、服务不可用 |
| `tests/tst_mockrepository.cpp` | 登录/概览/桩/站点/用户查询、正常/空/错误三组场景 |
| `tests/tst_statusmapping.cpp` | 状态枚举映射，未知状态显示"未知"且不崩溃 |
| 启动冒烟测试 | 构造 QApplication + 最小主窗口，无异常退出 |

## 7. 与其它模块的接口依赖

见 `docs/api/README.md`：登录、概览、桩状态为第一阶段核心三接口；字段、错误码、冻结状态以 B 的草案为准，未确认项记录为风险。
