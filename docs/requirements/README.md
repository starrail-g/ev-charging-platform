# 需求矩阵追溯源（仓库版）

> 本文件是角色 C 需求矩阵的**仓库追溯源**，正式 `.xlsx` 模板发布后以此为准套用格式。
> 状态取值：`已确认` / `已删除` / `第二阶段` / `待外部确认`（标注等待对象）。
> 更新规则：每次需求评审、代码或架构变化后同步本表；删除的条目保留一行说明原因。
> 计划实现日期列（2026-09-02 新增）：消歧"已确认=需求确认态 ≠ 已实现"；已实现条目在验收证据中标注日期。

## 1. 角色 C 需求追溯表

| 编号 | 需求描述 | 页面/材料落点 | 责任人 | 依赖 | 计划实现日期 | 验收证据 | 状态 |
|---|---|---|---|---|---|---|---|
| C-S1-001 | 管理员登录页面（输入校验、错误提示、成功跳转） | `apps/admin-client/src/pages/loginpage.*` + `src/data/*` | C | B：认证接口草案 | 9/2 | Qt Test `tst_loginflow` 通过（9/2 已实现：成功/密码错误 1100/服务不可用/空输入 3 组全部通过，Totals 6 passed；空输入断言 Repository 未被调用） | 已确认 |
| C-S1-002 | 主窗口导航：登录页↔业务页切换，业务页未登录不可进入 | `apps/admin-client/src/app/mainwindow.*` | C | 无 | 9/1 | 启动冒烟测试通过（9/1 骨架 + 9/2 防回退断言，4 场景绿） | 已确认 |
| C-S1-003 | 概览页：营收摘要、桩状态摘要、站点利用率摘要、更新时间 | `apps/admin-client/src/pages/overviewpage.*` | C | B：统计字段口径 | 9/3 | Mock 数据三态显示（正常/空/失败） | 已确认 |
| C-S1-004 | 桩列表页：状态查询、筛选、刷新 | `apps/admin-client/src/pages/pilepage.*` | C | B：桩状态枚举 | 9/4 | `tst_mockrepository` 通过 | 已确认 |
| C-S1-005 | 桩远程重启（第一阶段为服务端确认后的状态模拟） | `apps/admin-client/src/pages/pilepage.*` | C | B：操作接口 | 9/5 | 状态冲突测试通过 | 已确认 |
| C-S1-006 | 站点查询和管理页 | `apps/admin-client/src/pages/stationpage.*` | C | B：站点字段 | 9/4 | Mock 查询演示 | 已确认 |
| C-S1-007 | 用户查询和冻结/解冻（模拟确认） | `apps/admin-client/src/pages/userpage.*` | C | B：用户字段 | 9/5 | 冲突测试通过 | 已确认 |
| C-S1-008 | 加载中、空数据、接口失败、无权限等可见状态 | 全部页面 + `src/widgets/statestack.*` | C | 无 | 9/2 组件初版 / 9/3 页面接入 | 四态组件已建并接入概览页（9/2）；三态手工验证 + 截图 | 已确认 |
| C-S1-009 | Repository 抽象 + Mock/Socket 双实现，页面不建 Socket 不写 SQL | `apps/admin-client/src/data/*` | C | 无 | 9/2 抽象+Mock / 9/6 Socket | `AdminRepository` 抽象 + `MockAdminRepository` 已建（9/2），`MockAdminRepository` 对接登录 | 已确认 |
| C-S1-010 | 大屏近 7 日营收趋势图 | `dashboard/index.html` + `js/app.js` | C | B：营收统计字段 | 9/3 | 本地 HTTP 200 + 图表有数据 | 已确认 |
| C-S1-011 | 大屏桩状态分布图 | 同上 | C | B：桩状态枚举 | 9/3 | 同上 | 已确认 |
| C-S1-012 | 大屏站点利用率排行图 | 同上 | C | B：利用率口径 | 9/3 | `admin.station.list[].utilization`，固定 `7d` UTC 口径 | 已确认 |
| C-S1-013 | 大屏更新时间与数据来源说明 | `dashboard/index.html` | C | 无 | 9/3 | 页面可见 | 已确认 |
| C-S1-014 | 大屏离线能力：本地 ECharts、不依赖 CDN/外网 | `dashboard/vendor/echarts.min.js` | C | 无 | 9/3 | 断网验证 | 已确认 |
| C-S1-015 | 大屏离线备用入口（无 fetch、无 CDN） | `dashboard/offline.html` + `offline-data.js` | C | 无 | 9/4 | 断网演示 | 已确认 |
| C-S1-016 | 可追溯演示数据 JSON，与管理端 Mock 同一口径 | `dashboard/data/demo-dashboard.json` | C | B：字段确认 | 9/3 | 两入口数据一致（口径对齐 `src/data/mockdataset.*`） | 已确认 |
| C-S1-017 | Qt Test：登录流程测试（成功/密码错误/服务不可用/空输入） | `apps/admin-client/tests/loginflow/tst_loginflow.cpp` | C | 无 | 9/2 | 测试通过（9/2 已实现：4 场景全部通过，Totals 6 passed；空输入含 Repository 未调用断言） | 已确认 |
| C-S1-018 | Qt Test：Mock Repository 测试 | `apps/admin-client/tests/tst_mockrepository.cpp` | C | 无 | 9/3 | 测试通过 | 已确认 |
| C-S1-019 | Qt Test：状态映射测试（未知状态不崩溃） | `apps/admin-client/tests/tst_statusmapping.cpp` | C | B：状态枚举 | 9/3 | 测试通过（`parsePileStatus` 未知值返回 Unknown 已设计） | 已确认 |
| C-S1-020 | 接口闸门记录（9/7 18:00） | `docs/meetings/interface-gate-2026-09-07.md` | C | B：三接口可运行 | 9/7 | 闸门结果文档 | 待外部确认（B） |
| C-S1-021 | 冒烟与联调步骤文档 | `tests/integration/role-c-smoke-test.md` | C | 无 | 9/8 | 文档 + 实际执行 | 已确认 |
| C-S1-022 | 回归结果记录 | `tests/integration/role-c-regression.md` | C | 无 | 9/9 | 文档 | 已确认 |
| C-S1-023 | 缺陷日志（编号 C-S1-xxx 起） | `docs/release/defect-log.md` | C | 无 | 9/2 首条登记 | 每缺陷一行记录（9/2 已登记 C-S1-001/002，责任人 B） | 已确认 |
| C-S1-024 | 第一阶段发布清单 | `docs/release/stage1-checklist.md` | C | 无 | 9/9 | 逐项打勾 | 已确认 |
| C-S1-025 | 干净 Ubuntu 环境构建+启动验证 | README 验证命令 | C | 虚拟机环境 | 9/8 | 构建日志记录 | 已确认 |
| C-S1-026 | 需求矩阵（xlsx） | 课堂派模板（未到） | C | 模板 | 9/10 | 模板套用后上传 | 待外部确认（课堂派） |
| C-S1-027 | 概要设计说明书（docx） | 课堂派模板（未到） | C | 模板 | 9/10 | 模板套用后上传 | 待外部确认（课堂派） |
| C-S1-028 | 测试用例文档（docx） | 课堂派模板（未到）；源：`docs/requirements/test-cases-login.md` | C | 模板 | 9/10 | 模板套用后上传（登录 4 场景源已建 9/2） | 待外部确认（课堂派） |
| C-S1-029 | 团队贡献度 | 课堂派模板（未到）；源：`superpowers/plans/2026-09-01-contribution.md`（过程区） | C（汇总），A/B 提供证据 | 模板 | 9/10 | 模板套用后上传（C 侧证据 9/2 已登记） | 待外部确认（课堂派） |
| C-S1-030 | 项目路演 PPT（10 分钟） | 课堂派模板（未到） | C（汇总），A 主讲 | 模板 | 9/10 | 答辩计时通过 | 待外部确认（课堂派） |
| C-S1-031 | 个人/小组源码包、录屏 | 发布包 | C（核对），个人自备 | 无 | 9/10 | 上传后复核 | 已确认 |
| C-S1-032 | `current.md` 与仓库文档同步维护 | 仓库根目录 | C | 无 | 持续 | 每次代码变化后更新 | 已确认 |

## 2. 五份材料章节清单（骨架）

> 正式模板到达前，以下骨架作为内容源；模板到达后只做格式适配、命名复核和缺项补齐，不重写内容。

### 2.1 需求矩阵（xlsx）

| 章节/列 | 内容 |
|---|---|
| 需求编号 | C-S1-xxx（与追溯表一致） |
| 模块 | 管理端 / 大屏 / 测试 / 发布 |
| 功能描述 | 一句话需求 |
| 优先级 | P0（阻塞）/ P1（重要）/ P2（次要） |
| 验收标准 | 可验证的证据描述 |
| 状态 | 确认 / 删除 / 第二阶段 / 待外部确认 |
| 计划实现日期 | 排期日期（消歧"已确认"与"已实现"） |

### 2.2 概要设计说明书（docx）

1. 引言（目的、范围、术语）
2. 系统边界与总体架构（Qt 客户端 ↔ Socket ↔ 服务端 ↔ SQLite；Web 大屏读取统计）
3. 管理端设计（页面层、Repository 抽象、Mock/Socket 双实现、数据方向）
4. Web 大屏设计（三图表、本地 ECharts、离线备用入口、数据口径）
5. 接口依赖（登录、概览、桩状态；冻结状态与降级规则）
6. 异常处理设计（加载中/空/失败/断连/状态冲突）
7. 测试策略（Qt Test、冒烟、回归）
8. 部署与启动（环境要求、构建命令、端口）

### 2.3 测试用例文档（docx）

| 列 | 内容 |
|---|---|
| 用例编号 | TC-C-S1-001 起 |
| 场景 | 正常 / 异常 / 边界 / 联调 |
| 前置条件 | 数据、服务状态 |
| 步骤 | 可执行步骤 |
| 预期结果 | 明确断言 |
| 实际结果 | 执行后填写 |
| 状态 | 通过 / 失败 / 未执行 |
| 证据 | 提交号 / 截图 / 测试输出 |

登录 4 场景可执行用例见 `docs/requirements/test-cases-login.md`（9/2 已建）。

### 2.4 团队贡献度

| 列 | 内容 |
|---|---|
| 成员 | A / B / C |
| 模块与产出 | 本人负责部分 |
| 工时证据 | 分支、提交号、测试记录 |
| 说明 | 以课堂派正式模板为准 |

### 2.5 项目路演 PPT（10 分钟）

1. 封面（项目名、组号、成员）
2. 项目背景与目标（1 页）
3. 系统架构与数据链路（2 页：边界图 + 数据流）
4. 用户端演示要点（A 讲解）
5. 管理端演示（C：登录 → 概览 → 桩/站/用户管理）
6. Web 大屏演示（C：三图表、离线能力）
7. 服务端与数据库（B 讲解）
8. 测试与质量（C：测试结果、缺陷统计）
9. 分工与贡献（三人）
10. 风险、计划与展望（第一阶段完成度 + 第二阶段安排）

## 3. 待外部确认清单

| 事项 | 等待对象 | 最晚时间 | 影响 |
|---|---|---|---|
| 三接口（登录/概览/桩状态）草案交付 | B | 9/7 18:00 闸门 | C 先行 Mock 口径，不阻塞（admin.login 契约已冻结，B PR #1 已合） |
| statistics/admin/pile/station/user 字段冻结 | B | 9/4 需求评审 | 已冻结；`admin.statistics.get` 的 `revenue_daily` 为 7/30 条 UTC 日序列，C/Dashboard 适配器按协议映射 |
| 第一阶段唯一演示链路与答辩顺序 | A | 9/1 内确认 | 影响页面范围冻结 |
| 课堂派五份材料模板 | 课堂派 | 发布即适配 | 不影响骨架建立 |
| 构建系统二选一 | A/B | 9/1 15:30 | 已确认 qmake；见 `apps/admin-client/README.md` |
