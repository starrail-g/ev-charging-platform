# 角色 C 交付与协作计划（管理端 + 大屏）

> 版本：v1.0（2026-09-02）
> 角色 C：管理端界面（Qt Widgets）+ Web 实时态势大屏（ECharts）+ 测试与发布材料
> 本文档合并自两份执行计划：90h 交付总表（阶段容量与逐日主题）+ 统一 UI 实施计划（当前执行的细化路线）
> 配套：`docs/architecture/admin-client.md`（架构边界）、`docs/requirements/README.md`（需求矩阵）、`docs/api/README.md`（接口清单与冻结状态）

## 1. 范围与交付物

### 1.1 Qt 管理端（`apps/admin-client`）

- 管理员登录（协议 `admin.login`，按错误码 1100 UNAUTHORIZED 分支）→ 业务区五页：概览 / 充电桩 / 充电站 / 用户管理；退出登录清凭据并锁定业务区
- 数据层架构：页面 → `AdminRepository` 抽象（异步接口：login / fetchOverview / dataSourceName）→ Mock 实现（阶段一，500ms 模拟网络延迟）/ Socket 适配层（联调期，9/6 起）
- 页面状态四态：加载 / 空 / 错误 / 正常；空态由数据层 `hasData` 显式声明，不从数值反推
- 概览指标：近 7 日营收、桩五态分布（idle/reserved/charging/fault/offline）、站点平均利用率、更新时间（UTC）
- 关键落点：`src/app/mainwindow.*`、`src/pages/*`（login/overview/pile/station/user）、`src/data/adminrepository.h` + `mockadminrepository.*` + `socketadminrepository.*`、`src/models/adminmodels.h`、`tests/`（launchsmoke / loginflow / mockrepository / statusmapping / ui）

### 1.2 ECharts 大屏（`dashboard`）

- 基础版：近 7 日营收趋势、桩状态分布、站点利用率排行（本地 ECharts + 本地 HTTP 服务，断网可演示）
- 统一 UI 扩展版：充电网络实时态势主屏、经营效率板块、统一地图表面（真实地图优先 / 离线 SVG 拓扑兜底）
- 演示数据与 Qt Mock 同一口径（金额整数分、UTC ISO-8601、桩状态协议五态），可追溯、可重复

### 1.3 过程、测试与发布材料

- 需求矩阵（`docs/requirements/README.md`）、概要设计、测试用例、贡献度、PPT 五份；课堂派模板到达前以上述 Markdown 为内容源，模板未知字段不虚构
- QtTest 套件 + 集成冒烟/回归步骤（`tests/integration/`）；缺陷日志（`docs/release/defect-log.md`，编号 C-S1-xxx）
- 9/10 答辩录屏、9/18 两份个人报告

## 2. 阶段容量与逐日主题

容量：阶段一 9/1–9/9 每天 10h（08:30–12:00 / 13:30–17:30 / 19:00–21:30）；9/10 答辩日不再排开发；阶段二 9/11–9/17 每天 10h；9/18 个人报告（10h 上限）。
阶段一工时分布：管理端 30h / 大屏 12h / 联调测试 12h / 过程材料 10h / 发布提交 14h / 缓冲 12h = 90h。

### 2.1 阶段一逐日主题

| 日期 | 主题 | 状态（9/2 收工核对） |
|---|---|---|
| 9/1 | 冻结执行口径并建立可构建起点 | ✅ 主体完成（qmake 决策、双区边界、五页空壳、T-C0.1） |
| 9/2 | 登录、导航和材料责任映射 | ✅ 主体完成（T-C0.2、两轮 review 修复已推送） |
| 9/3 | 管理数据模型、概览与大屏骨架 | 🔄 部分提前完成（adminmodels / Mock 三组数据 / 概览四态）；剩余并入统一 UI 路线执行 |
| 9/4 | 需求评审、业务列表和图表闭环 | 待办 |
| 9/5 | 管理操作、离线备用大屏和说明 | 待办 |
| 9/6 | 完成管理端 MVP，Socket 适配层替换 | 待办（依赖 B 接口可用性） |
| 9/7 | 环境验证与 18:00 接口闸门 | 待办 |
| 9/8 | 按闸门结果联调，形成发布候选版 | 待办 |
| 9/9 | 冻结、回归、录屏和打包 | 待办 |
| 9/10 | 答辩与提交日 | 待办 |

### 2.2 阶段二逐日主题（简表）

9/11 反馈入库和修复排序 → 9/12 集中修复 P0/P1 → 9/13 真实接口覆盖和回归 → 9/14 数据集和大屏增量 → 9/15 冻结前回归 → 9/16 材料、录屏和发布候选 → 9/17 最终回归和提交 → 9/18 个人报告×2。

## 3. 当前执行路线（2026-09-02 定：统一 UI 实施）

顺序：PR #2 合入 main → 从最新 main 开 `feature/unified-ui` → 按下列 12 任务执行（每任务 RED→GREEN→REFACTOR + 聚焦 commit，每里程碑一 PR，commit 前经 C 审查）。

| # | 任务 | 端 |
|---|---|---|
| 1 | `design-tokens.json` 唯一视觉令牌源 + 对比度门禁 | 双端 |
| 2 | 跨平台 qmake 校验 hook + 安全本地配置（config/local.env） | 工程 |
| 3 | Qt 主题加载、五态映射、状态脉冲组件 | Qt |
| 4 | Qt 壳层与登录页视觉层级重构 | Qt |
| 5 | Qt 概览指标卡、站点态势图与异常联动 | Qt |
| 6 | Qt 充电桩 / 充电站 / 用户工作页 | Qt |
| 7 | 可离线运行的 Web 基础 + 安全配置服务 | Web |
| 8 | Web 演示数据、五态映射与数据适配器 | Web |
| 9 | 统一地图表面（真实地图优先 / 离线 SVG 拓扑兜底） | Web |
| 10 | Web「充电网络实时态势」主屏与经营效率板块 | Web |
| 11 | 双向呼吸动画、响应式布局与无障碍降级 | 双端 |
| 12 | 双端集成、离线答辩演练与交付文档 | 双端 |

技术约束：

- Qt 保持 Widgets + qmake + Repository 边界；Web 使用原生 HTML/CSS/ES modules + 本地 ECharts 6.1.0（离线）
- 视觉：昼夜双主题；五态语义色；正文对比度 ≥4.5、态势图线条 ≥3.0；生成文件随源码提交但不得手改（只改 tokens/模板后重新生成）
- 安全：`config/local.env` 与真实地图密钥永不进入仓库、日志、截图或测试快照
- 构建产物一律在仓库外构建目录，不入库

## 4. 对 A/B 的依赖与协作节点

| 节点 | 依赖 | C 侧动作 |
|---|---|---|
| 9/4 需求评审 | B 冻结 admin / statistics / pile / station / user 字段、金额单位、时间格式、错误码 | 已完成；统计趋势使用 `statistics.revenue_daily[*].revenue_cents`，真实 Socket 适配仍待联调 |
| 9/4 前 | B 修复 P0 迁移原子性（C-S1-001）、P1 坏帧（C-S1-002） | 复验脚本已就绪：迁移失败保持旧版本 / 同批好帧保留；通过后关闭缺陷条目 |
| 9/7 18:00 | 接口闸门：登录 / 概览 / 桩状态可运行 | 预判 `statistics.get` 可能不可用；若未过闸门，按规则申请 A 书面批准 Mock 降级，材料不冒充真实联调 |
| 阶段二 | Socket 真实联调 | 依 B 服务端进度推进；`statistics.get` 若排至 9/11 后，概览先按 Mock + 风险标注 |

## 5. 当前进度状态（证据可追溯）

| 项 | 状态 | 证据 |
|---|---|---|
| T-C0.1 工程骨架（qmake + 五页 + 冒烟） | 完成 | PR #2，分支 `feature/member-c-admin-client-foundation` |
| T-C0.2 登录认证收紧 | 完成 | commit a24cbed |
| review 第一轮 4 条（异步登录 / 登出清凭据 / hasData / 数据来源标识） | 完成 | commit a27cc11 |
| review 第二轮 2 条（requestId 防旧回调竞态 / 概览数据链路并入 Repository） | 完成 | commit 3d81a2b（含 500ms Mock 模拟延迟） |
| 双平台测试 | 通过 | launchsmoke Totals 6 / loginflow Totals 7（Windows + Ubuntu 虚拟机逐字一致） |
| PR #2 合入 | 待执行 | mergeable = MERGEABLE |

## 6. 验收与退出条件

- **阶段一（9/10）**：管理端登录 → 概览/桩/站/用户 Mock 全链路可重复演示；大屏离线可运行；过程材料可追溯到提交；闸门有明确结论与材料
- **阶段二（9/17）**：真实 Socket 接口覆盖（或已批准的降级）；回归全绿；录屏与发布候选就绪
- **每日固定动作**：git 状态敏感检查 → 构建/测试记录 → current.md 压缩更新 → 缺陷日志 → 单成果提交 → 三行同步 A/B
