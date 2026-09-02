# Current Project State

## Overview

Project: EV Charging Platform / 电动汽车充电桩应用管理平台

The project implements the application management platform defined by
the project requirements.

Required product areas include:

-   Qt user client
-   Linux + Qt PC management application
-   server-side communication/business processing
-   SQLite-backed persistent data
-   ECharts Web big-data dashboard
-   intelligent charging-load analysis and forecasting

Current stage: **阶段一开发中（角色 C：统一 UI 视觉令牌已落地，管理端主题基础开发中）**。

No production feature should currently be treated as complete unless
verified in the repository.

## Status

Completed project setup work:

-   project requirements have been obtained and reviewed
-   monorepo skeleton has been created
-   basic Git/GitHub collaboration guidance has been prepared
-   project-level Codex instructions are defined in `AGENTS.md`
-   persistent project state is tracked in this file

Current implementation state (2026-09-02):

-   统一 UI 已冻结为“A 运营调度台构图 + 原始昼夜电网配色 + 11 秒闭合呼吸极光”；
    `libs/common/ui/design-tokens.json` 成为昼/夜主题、五态色与动效时长的唯一来源，
    Python 生成器输出 Qt QSS/C++ 与 Web CSS/JS，并对五态 4.5:1、拓扑线 3:1 执行门禁
-   workspace boundary fixed: this repository keeps only GitHub project
    artifacts; local plans/specs live in a sibling `superpowers` folder
    and generated builds live in a sibling `build` folder
-   **build system decided: qmake（CMake 不维护）**；admin-client 可构建
-   **B 的 PR #1 已合入 origin/main（T-B0.1/0.2/0.3）**：协议 v1 冻结（信封/错误码/操作名/
    桩五态/金额分/UTC 时间）、database schema v0.2 + seed（admin/123456）、
    migration 原子性与帧解码 P0/P1 已修（C 复验计划 9/5-6）
-   admin-client 登录 TDD 完成（9/2）：`AdminRepository` 抽象 + `MockAdminRepository`
    （admin/123456，对齐 B seed）+ `tst_loginflow` 四场景（成功/密码错误 1100/服务不可用/
    空输入 3 组）Totals 6 passed 全绿；空输入断言 Repository 未被调用；
    `onLoginSuccess` 已收紧 private（无绕过入口）；登录错误按协议码分支（1100/网络错误）
-   admin-client 四态组件 `StateStack`（加载/空/错误/正常）+ 概览页 Mock 摘要接入（9/2 初版）
-   adminmodels（桩/站/用户/概览字段按 schema.sql 自拟，待 9/4 对齐 B）+ `mockdataset` 三组
    Mock 数据（正常/空/错误，金额分、UTC、五态）
-   冒烟测试 3 个测试函数（窗口创建/登录锁定与防回退/概览四态）Totals 5 passed；
    登录测试 Totals 6 passed；`git diff --check` 通过
-   材料：需求矩阵 +"计划实现日期"列；测试用例登录 4 场景（test-cases-login.md）；
    缺陷日志登记 C-S1-001(P0)/002(P1) 责任人 B；B 待办清单已生成（待转发）
-   角色 C 需求追溯表（C-S1-001~032）建于 `docs/requirements/README.md`
-   架构文档 `docs/architecture/admin-client.md`（页面层/Repository/Mock-Socket 双实现）
-   Socket protocol v1 已冻结（信封/错误码/操作名/桩五态/金额分/UTC 时间）；
    对象字段级冻结待 9/4 评审（B）；9/7 18:00 接口闸门确认可运行范围
-   CI 非当前优先级

## Architecture

Current target module layout:

``` text
apps/user-client       Qt user client
apps/admin-client      Qt PC management application

server                 Socket/server business layer

libs/common            shared C++ utilities/types
libs/protocol          shared communication protocol
libs/database          database access layer

database               schema, migrations, seed data

dashboard              ECharts Web dashboard

ml                     intelligent analysis subsystem

docs                    project design/documentation
tests/integration       cross-module integration tests
```

Current target data flow for networked application features:

``` text
User/Admin Qt Client
        |
        | Socket
        v
      Server
        |
        v
 Database Layer
        |
        v
      SQLite
```

This separation is a project architecture decision for the current
implementation plan, not a statement that the requirements document
fixes the exact process layout.

Important architectural details are still pending design.

## TODO

High priority（9/2 完成项已勾）:

-   [x] 统一 UI Task 1：唯一视觉令牌源、四端生成文件与对比度/漂移测试
-   [x] 统一 UI Task 2：qmake 跨平台 token 漂移 pre-link 校验、QSS 资源接入、
    `config/local.env` 密钥隔离与 dashboard 示例配置
-   [x] 统一 UI Task 3：Qt 日班主题启动加载、协议五态标签、充电/故障低幅状态脉冲，
    支持 `EV_UI_REDUCED_MOTION=1` 与测试显式停用动画
-   [x] 统一 UI Task 4：Qt 三段式运营壳层与聚焦登录卡片，补齐产品/会话上下文、
    控件可访问名称，以及 hover/focus/disabled/alert 状态
-   [x] 登录 TDD：tst_loginflow 四场景（成功/密码错误/服务不可用/空输入 3 组）
    6 passed 全绿，空输入断言 Repository 未调用（9/2）
-   [x] 侧边导航、五页切换、退出登录防回退（9/1 骨架 + 9/2 防回退断言）
-   [x] MockAdminRepository + adminmodels + mockdataset 三组数据（9/2）
-   [ ] 概览页 Mock 指标完善 + 桩/站/用户列表页（9/3-9/5）
-   [ ] ECharts 大屏骨架与 demo JSON（9/3，对齐 mockdataset 口径）
-   [ ] 与 B 确认三接口草案（9/7 18:00 闸门）；B 字段冻结 9/4
-   [ ] SocketAdminRepository 适配层（9/6）

After foundations are stable:

-   [ ] implement required user-client features
-   [ ] implement required admin-management features
-   [ ] establish dashboard data path and ECharts pages
-   [ ] define intelligent-analysis data pipeline
-   [ ] implement 1h / 6h / 24h charging-load forecasting
-   [ ] implement station recommendation and load warning
-   [ ] add integration and regression tests for stable flows

## Known Issues

-   exact Socket protocol and serialization format are undecided (B, gate 9/7 18:00)（admin.login 契约已冻结，其余随 B 字段冻结 9/4）
-   exact SQLite tables, fields, constraints, and relationships are undecided (B)（schema v0.2 已合入，字段最终冻结 9/4）
-   管理端 statistics/admin/pile/station/user 字段未冻结（B，9/4 评审；C 按 schema.sql 自拟）
-   authentication/session behavior beyond the stated product
    requirements needs design
-   charging-order state machine and settlement consistency rules need
    design
-   dashboard-to-backend data interface is undecided
-   ML framework/language and model approach are undecided
-   external Tencent Maps API integration details and development-key
    handling need design
-   课堂派五份材料模板未发布（先以 docs/ 下 Markdown 为内容源）

These are unresolved design items, not implementation defects.

## Decisions

-   use a single monorepo for the complete project
-   organize source code by module/responsibility rather than by
    contributor
-   keep Qt presentation code separate from reusable
    protocol/database/business logic
-   use `server` as the intended central Socket/business integration
    layer
-   keep shared communication contracts under `libs/protocol`
-   keep persistent-data access separated from UI code
-   use `current.md` as concise persistent project state for Codex
    sessions
-   keep global Codex delegation/runtime rules in the user's global
    Codex configuration and global `AGENTS.md`; keep this repository's
    `AGENTS.md` project-specific
-   do not prematurely lock in details that the requirements do not
    specify
-   maintain `current.md` on every code/architecture change and
    compress it regularly so it keeps only currently valuable
    information
-   **2026-09-01: 工作区采用双区边界**：`current.md`、源码和正式项目文档
    保留在 GitHub 仓库；构思、spec、plan、每日进度与构建产物存放在
    仓库同级外层目录（如 `superpowers/`、`build/`）
-   **2026-09-01: admin-client 构建系统选定 qmake（.pro），不维护 CMake**；理由：Ubuntu 验收环境 qmake6 现成、东软教程以 .pro 为主线；唯一构建命令见 `apps/admin-client/README.md`
-   **2026-09-01: 角色 C 管理端采用页面层 + AdminRepository 抽象 + Mock/Socket 双实现**，页面不建 Socket 不写 SQL；9/7 18:00 闸门决定真实联调或经 A 批准 Mock 降级
-   **2026-09-01: UI、主题与整体美术风格列为全项目的一等质量目标**：
    涉及布局、交互、主题或数据可视化的重要设计须先讨论并检索优秀项目与
    设计体系作为参考；跨 Qt 客户端、大屏和分析产物保持统一视觉语言，
    在功能正确、安全、数据真实、可靠和无障碍底线之上追求新颖、美观与辨识度

## Recent History

-   2026-09-02: unified UI Task 1 completed via TDD: shared day/night design tokens,
    deterministic Qt/Web generation, state-color AA gate, and topology-line contrast gate
-   2026-09-02: 登录 TDD 完成（AdminRepository/MockAdminRepository/tst_loginflow 四场景
    Totals 6 passed）；onLoginSuccess 收紧 private；StateStack 四态组件（概览页可切换
    正常/空/错误+重试，冒烟 5 passed）；adminmodels + mockdataset（ok/error 可区分）；
    需求矩阵加计划实现日期列；测试用例登录 4 场景；缺陷日志 C-S1-001/002；路径硬编码清理
-   2026-09-01: separated local plans/ideas/build output from GitHub project artifacts; restored the formal defect log under `docs/release`
-   2026-09-01: admin-client qmake 工程可构建；冒烟测试 4/4 通过；需求追溯表 C-S1-001~032；架构/缺陷/API/UI 文档初始化
-   2026-09-01: established project-wide UI/theme/visual-design principles, including design discussion, Web reference research, cross-surface consistency, complete interaction states, and truthful data visualization
-   2026-08-31: 团队分工确认（A 用户端、B 服务端、C 管理端/大屏/测试发布）；第一阶段截止校正为 9/10
-   reviewed the project requirements and identified the main product
    modules
-   created the initial monorepo directory skeleton
-   prepared a beginner-friendly Git/GitHub collaboration guide
-   established the project-specific `AGENTS.md`
-   initialized `current.md`

Keep this history concise. Compress or replace old entries as the
project progresses rather than appending indefinitely.
