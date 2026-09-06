# 项目协议与协作规则汇总

更新时间：2026-09-02
适用仓库：`starrail-g/ev-charging-platform`
适用分支：所有成员分支、Pull Request 和答辩交付分支

本文将仓库中的工作指令、协作约定、构建协议、Socket/数据库技术契约和当前状态整理为一份可快速查阅的本地记录。它不替代原始文件；发生冲突时，应回到原始文件和最新团队决定核对。

## 1. 规则来源与优先级

| 优先级 | 来源 | 作用 |
|---|---|---|
| 1 | 最新的团队明确决定和用户确认 | 覆盖旧的未决记录，例如“Qt/C++ 必须使用 qmake6，不能使用 CMake”。 |
| 2 | `AGENTS.md` | 仓库开发、架构、安全、文档同步和验证的强制工作指令。 |
| 3 | `current.md` | 当前阶段、任务状态、依赖、证据和未决事项的活跃事实源。代码或架构调整必须同步它。 |
| 4 | `docs/requirements/requirements-matrix.md` | 产品范围、优先级、阶段、负责人和验收基线的唯一业务矩阵。 |
| 5 | `docs/architecture/protocol.md`、`docs/architecture/database.md` | Socket v1 和 SQLite 数据/事务的技术契约。 |
| 6 | `CONTRIBUTING.md` | 分支、提交、Push、PR、Review 和敏感信息处理流程。 |
| 7 | `docs/meetings/` 中的评审和基线记录 | 历史证据和问题追踪；不能覆盖更新后的协议或当前状态。 |

`repository-baseline-2026-09-01-v2.md` 是对话前提的历史摘要；其中关于“CMake/qmake 尚未决定”的内容已被 2026-09-02 构建协议和 `current.md` 覆盖。

## 2. 项目边界与数据流

```text
用户端 Qt（A） ─┐
管理端 Qt（C） ─┼─ Socket/统一协议 ─> server（B） ─> database layer ─> SQLite
Web 大屏（C） ──┘
地图服务或 Mock（A，外部边界）
```

| 模块 | 必须负责 | 明确不负责 |
|---|---|---|
| `apps/user-client` | 用户登录/注册、站点和电桩展示、地址与导航、预约、充电、计费、结算、资料和钱包 UI | 服务端持久化、SQL、管理员功能 |
| `apps/admin-client` | 管理员登录、营收/统计、桩状态、站点和用户管理 UI | 核心业务规则、直接操作 SQLite |
| `server` | Socket 接入、认证、业务校验、状态转换、订单/桩服务、统计查询和并发控制 | Qt 页面绘制 |
| `libs/protocol` | 消息结构、序列化、帧边界、错误码 | 业务规则和 SQL |
| `libs/database` / `database` | SQLite 连接、Schema、迁移、种子、查询和事务封装 | UI 和 Socket 会话管理 |
| `dashboard` | ECharts 统计展示、固定 JSON/SQLite 导入或读取 | 交易写入和核心状态修改 |
| `ml` | S2 的负荷/空闲桩预测、低拥堵推荐、负荷预警及模型服务边界 | S1 基本充电闭环 |

用户端和管理端不得各自定义另一套字段、状态或报文。协议/字段有变更时，必须检查生产者和消费者、更新文档与样例，并验证受影响链路。

## 3. Git/GitHub 协作协议

1. 不直接在 `main` 开发，不直接向 `main` Push；每个任务使用独立分支并通过 Pull Request 合并。
2. 分支采用 `feature/`、`fix/`、`docs/`、`test/`、`refactor/` 或 `chore/` 加简短英文描述；一个分支尽量只处理一个明确问题。
3. 提交前依次检查 `git status`、任务文件范围和 `git diff --check`；提交信息应说明模块和动作，并附最小验证命令。
4. 禁止 `git push --force`。不得提交密码、腾讯地图 API Key、Token、私钥、运行数据库、日志或构建产物。
5. 代码、公共接口、Socket 协议、数据库结构或架构变更必须同步相关文档；代码/架构变更必须维护根目录 `current.md`，并压缩掉无价值的旧内容。
6. AI 生成的代码由提交者负责理解和测试；通常由成员确认后再 Push/合并。本次若用户明确授权代理执行 Push，只能推送任务分支，仍不得 Force Push 或直接改 `main`。

## 4. qmake6 构建强制协议

`docs/meetings/build-system-protocol-2026-09-02.md` 已将 `qmake6` 定为整个 Qt/C++ 项目的唯一权威构建、测试、验收和发布路径。

- Qt 模块必须提供可由 `qmake6` 读取的 `.pro` 文件；多模块项目使用 `TEMPLATE = subdirs` 或明确的 `.pro` 依赖组织。
- `cmake`、CMakeLists、CMake preset、CMake 构建目录和 CMake 命令不得作为构建或验收证据。旧 CMake 文件属于非权威历史尝试，清理时单独处理。
- 构建前必须记录 `qmake6 --version`、Qt/编译器版本、实际 `.pro` 配置命令、`make` 命令和测试/启动结果。
- 单元测试、协议测试、服务端测试和用户端测试都必须通过对应 `.pro` 生成 Makefile 后执行；不能因本机缺少 qmake6 而改用 CMake，应转到 Ubuntu/VM 验证并如实记录环境限制。
- 干净验证应使用新的 `build/qmake6/<module>` 目录；构建目录、Makefile 和 `.qmake.stash` 不得提交。

当前检出分支可见的 qmake 工程包括：

- `apps/user-client/user-client.pro`
- `apps/user-client/tests/user-client-tests.pro`
- `libs/protocol/tests/tests.pro`
- `server/server.pro`

`apps/admin-client` 当前检出内容只有 README 占位，管理端 `.pro` 和实现仍以 C 的后续 PR/合并版本为准。

## 5. Socket Protocol v1

### 5.1 传输与帧

- TCP 默认地址为 `127.0.0.1:45454`，可由配置覆盖。
- 一个连接可承载多个帧；客户端不能把一次 `readyRead`/`recv` 当作一个完整消息，必须处理半包和粘包。
- 帧格式为四字节无符号大端 payload 长度，后跟 UTF-8 JSON payload；长度不包含四字节前缀。
- payload 最大 1 MiB；零长度、超限或非法帧返回错误并关闭当前连接，不能影响其他连接。
- 顶层 envelope 必须包含 `v`、`id`、`type`、`payload`；`v=1`，`id` 为 1–64 字符字符串，`payload` 为空时使用 `{}`。
- 服务端按接收顺序处理同一连接的请求；响应回显请求 `id`。状态变更请求不得用不同 ID 重试。

示例：

```json
{"v":1,"id":"user-42","type":"health","payload":{}}
```

### 5.2 错误码

| Code | 名称 | 用途 |
|---:|---|---|
| 0 | `OK` | 成功 |
| 1000 | `INVALID_FRAME` | 长度前缀、帧边界或完整性错误 |
| 1001 | `INVALID_JSON` | payload 不是合法 JSON 对象 |
| 1002 | `INVALID_REQUEST` | envelope、payload 或操作名无效 |
| 1003 | `UNSUPPORTED_VERSION` | 协议版本不支持 |
| 1100 | `UNAUTHORIZED` | 认证缺失、失败或权限不足 |
| 1200 | `NOT_FOUND` | 资源不存在 |
| 1201 | `CONFLICT` | 状态冲突、重复活动订单或占桩冲突 |
| 1202 | `INSUFFICIENT_BALANCE` | 余额不足以结算 |
| 1300 | `DATABASE_ERROR` | 持久化失败，相关事务必须回滚 |
| 1500 | `INTERNAL_ERROR` | 未预期服务端错误 |

客户端按数值 `code` 分支处理，不得依赖可变化的 `message` 文本。

### 5.3 v1 操作范围

基础诊断：`health`、`echo`。用户端：`user.login`、`user.profile.get/update`、`wallet.recharge`、`station.list`、`pile.list`、`order.active.get`、`reservation.create/confirm/cancel`、`charging.start/stop/settle`。管理端：`admin.login`、`admin.statistics.get`、`admin.station.list/create`、`admin.pile.restart`、`admin.user.list`、`admin.user.status.set`。

对象 ID 按协议约定使用整数；请求 ID 仍是字符串。金额统一为整数分（`*_cents`），时间统一为 UTC ISO-8601，例如 `2026-09-01T10:15:00Z`。用户对象至少包含 `id`、`phone`、`nickname`、`balance_cents` 和 `status`。

### 5.4 状态与失败规则

- 桩状态：`idle`、`reserved`、`charging`、`fault`、`offline`。
- 订单状态：`pending_reservation`、`reserved`、`charging`、`pending_settlement`、`completed`、`cancelled`、`exception`。
- 创建预约在一个事务内将订单置为 `pending_reservation`、闲置桩置为 `reserved`；确认只将订单置为 `reserved`。
- 开始充电、取消预约必须原子更新订单和桩；停止后进入 `pending_settlement`，结算后才进入 `completed`。
- 同一用户不能存在多个活动订单，同一桩不能被多个活动订单占用；冻结用户不能创建新订单或开始充电。
- 结算必须原子更新订单、桩、钱包余额和钱包流水；余额不足或任何数据库错误都不能留下部分更新。
- 相同请求 `id` 的停止/结算可幂等重放；请求 ID 持久化和重复提交策略必须由数据库/服务层完成后才能作为完整能力交付。
- v1 尚未定义独立会话 Token；在明确增加会话设计前，服务端校验请求 payload 中的凭据、用户 ID 和管理员上下文。

当前协议文档仍保留“服务端初始只实现 `health`/`echo`、无直接 SQLite 依赖”的历史说明。它不能替代 B 最新 PR/合并代码的实测结果；完成业务联调时必须同步协议文档、`current.md` 和可复现证据。

## 6. SQLite 数据库与事务协议

### 6.1 数据和实体

数据库设计文档定义 Schema v0.2，所有连接启用 `PRAGMA foreign_keys = ON`，时间使用 UTC ISO-8601 文本，金额使用整数分。核心表为：

- `users`：手机号、资料、余额和 `active/frozen` 状态；
- `administrators`：管理员认证和角色，种子账号仅用于本地演示；
- `stations`：站点身份、地址、经纬度和营业状态；
- `charging_piles`：站内唯一桩编号、功率、价格和桩状态；
- `charging_orders`：预约、充电、计费和结算生命周期；
- `wallet_transactions`：只追加的钱包流水；
- `pile_restart_logs`：远程重启审计记录。

`station_pile_status` 和 `revenue_daily` 是面向管理端/大屏的只读视图；营收按 `settled_at` 聚合，充电结束时间 `ended_at` 用于运营指标。

### 6.2 约束与状态转换

- 活动订单唯一索引防止同一用户或同一桩重复占用；服务层仍需先查询并把约束异常转换为稳定业务错误。
- 只有 `idle` 桩可被新预约或直接充电选择；`reserved`、`fault`、`offline` 对其他用户不可用。
- `pending_settlement` 必须有 `ended_at`；`completed` 必须有开始、结束、结算时间以及有效金额。
- 完成订单必须匹配一条金额、用户一致的负向 `charge` 钱包流水；触发器和结算事务共同保护该关系。
- 写事务使用 `BEGIN IMMEDIATE`、busy timeout 和明确回滚；锁冲突、约束失败和数据库失败要转换为可见错误。

### 6.3 必须保持的事务边界

1. 登录/自动注册：按手机号查询或创建用户，提交后返回稳定 ID。
2. 充值：校验正金额，写入带幂等键的充值流水并更新余额；重复键应返回重放/不重复扣款结果。
3. 创建/确认预约和开始充电：校验用户状态、未完成订单和桩状态，在一个写事务内完成相关状态变化。
4. 停止/结算：依据订单保存的费率和能量计算金额；余额不足不改变订单、桩或钱包；成功时写扣款流水、扣余额、完成订单并释放桩。
5. 冻结/解冻：单事务更新用户状态；冻结不取消已有订单，但禁止新的开始操作。
6. 远程重启：校验管理员权限和桩状态，始终记录重启审计；被拒绝时不改变桩状态。

Schema 新建使用 `database/schema/schema.sql`，v0.1 升级使用 `database/migrations/001_v0.1_to_v0.2.sql`。演示种子为 `database/seeds/dev.sql`，应可重复执行且不改变确定性结果。迁移前备份旧库，服务停止后执行，失败必须回滚并通过 `PRAGMA foreign_key_check`。

## 7. 安全、外部服务和验收

- 腾讯地图 Key 仅从本地未跟踪配置或环境变量注入；可以有 Mock/离线导航，但必须明确标注，Key 不能进入源码、Markdown、日志、截图、发布包或 Git 历史。
- 输入、权限、超时、断连、空数据和数据库失败都要有用户可读且脱敏的错误；错误不能泄露 Key、密码、数据库路径、堆栈或隐私。
- GUI 线程不能阻塞网络、数据库或预测任务；跨线程使用信号/槽等明确机制，后台线程不得直接更新 Widget，关闭时不得访问已销毁对象。
- S1 最小闭环是：登录/地址 → 站点和空闲桩 → Socket 请求 → 服务端校验 → SQLite 事务 → 客户端反馈 → 管理端/大屏读取同一快照。S2 智能分析为必做但不阻塞 S1，包含 1/6/24 小时预测、低拥堵推荐、负荷预警和模型服务边界。
- 每个成员的完成结论必须有可复现证据：qmake6 版本/命令、测试输出、启动步骤、提交 SHA、变更范围和已知限制。只有 Mock 时必须标明待替换接口，不能当作真实联调完成。

## 8. 当前执行清单

- [x] A-S1-01 需求基线和任务分工已完成。
- [x] A 用户端 Mock 基线已按 qmake6 工程构建并通过 Ubuntu VM QtTest；真实 Socket/SQLite 联调仍待 B 接口稳定后进行。
- [x] qmake6 强制协议已写入 `AGENTS.md`、`current.md` 和构建协议记录。
- [ ] B 需要提供并验证完整业务处理器、事务/幂等、请求响应样例和端到端异常测试。
- [ ] C 需要完成管理端、大屏、qmake6 工程和跨模块干净环境证据。
- [ ] A/B/C 需要完成真实 Socket/SQLite 联调、同一数据快照验证和第一阶段演示。
- [ ] 所有协议或数据库变更继续同步 `current.md` 及对应架构/API 文档。

## 9. 原始文件索引

- 工作指令：`AGENTS.md`
- Git 协作：`CONTRIBUTING.md`
- 活跃状态：`current.md`
- 产品矩阵：`docs/requirements/requirements-matrix.md`
- Socket 契约：`docs/architecture/protocol.md`
- 数据库契约：`docs/architecture/database.md`
- qmake6 强制协议：`docs/meetings/build-system-protocol-2026-09-02.md`
- 历史基线和评审：`docs/meetings/repository-baseline-2026-09-01-v2.md`、`b-backend-status-review-2026-09-01.md`、`b-pr1-review-2026-09-01.md`、`c-pr2-review-2026-09-01.md`

## 2026-09-04 最终决策附录

本附录覆盖本文中与 PR#4 最新实现冲突的旧表述，作为 A/C 联调时的最终口径：

- 冻结用户仍可登录并返回 `status=frozen`；`reservation.create`、`reservation.confirm`、`charging.start`（直充与预约）和 `wallet.recharge` 统一返回 `1101 ACCOUNT_FROZEN`。资料查询/更新、订单查询、预约取消、停止充电和结算放行。
- 幂等请求命中 `request_records` 后优先回放原结果，再执行冻结检查；冻结只拦截未命中的新请求。
- `charging.stop` 在同一事务内将订单置为 `pending_settlement` 并立即释放电桩为 `idle`；`charging.settle` 只负责金额计算、钱包扣款、流水、订单完成和计数，不再修改电桩状态。余额不足返回 `1202`，订单保持 `pending_settlement`，电桩保持 `idle`。
- `admin.user.list` 的用户对象应带 `active_order_status`；管理员接口仍属于后续实现范围。

A 端适配器和 `docs/architecture/protocol.md` 已按本附录对齐。