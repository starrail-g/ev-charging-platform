# 同学 B 后端工作状态复核

> 复核日期：2026-09-01
> 复核人：项目统筹复核
> 仓库：<https://github.com/starrail-g/ev-charging-platform>

## 1. 复核结论

当前能够在本地 Git 中核验的同学 B 提交为 `a8e1db9f4a347b04ff24aac37e68eb7e9b588fc3`，分支为 `feature/member-b-backend-foundation`。

用户提供的后续提交 `1dc1340328650e8f339e99f9f59e203b50d4584b` 尚未进入本地 Git 对象库；本次尝试从 GitHub 获取该提交时发生远程连接/缓存失败。因此，不能仅依据 PR 页面地址确认该提交已经成功修改，也不能把其中声称的修复标记为已完成。待远程提交可获取后，需要重新执行 schema、协议、服务端和报告复核。

基于 `a8e1db9` 的当前可核验结论：

- B 已完成后端基础设计和诊断骨架；
- SQLite schema、确定性 seed、Socket v1 文档和 `health`/`echo` 服务端可以作为并行开发基线；
- `B-S1-01` 和 `B-S1-02` 仍不能标记为完成；
- 数据库访问层、运行时事务、数据库业务处理器和真实业务接口仍未形成端到端闭环。

## 2. 已核验的文件和报告

### 2.1 `docs/development-plan.md`

记录了 B 负责的服务端、数据库、Socket、事务和后端质量边界，以及阶段 0 至阶段 3 的计划。文档明确阶段 I 的目标是“请求 → 校验 → SQLite → 响应”，并要求 A/C 先使用契约或 Mock 并行开发，再进行真实联调。

### 2.2 `docs/requirements/backend-traceability.md`

提供 BE-01 至 BE-12 的需求追踪，覆盖：

- 手机号登录/自动注册；
- 用户资料、余额和充值；
- 站点和充电桩查询；
- 未完成订单拦截；
- 预约、充电、计费和结算；
- 管理员登录、统计、远程重启、站点管理和用户冻结；
- Socket 稳定通信及异常可见性。

同时列出 BE-T01 至 BE-T06 异常用例，包括非法手机号、重复订单、非空闲桩、余额不足、数据库写入失败和非法帧/断连。

### 2.3 `docs/architecture/database.md`

定义了数据库 v0.1 的基本口径：

- 时间使用 UTC ISO-8601 文本；
- 金额使用整数分，不使用浮点元；
- 每个连接启用外键；
- 用户、管理员、站点、充电桩、订单、钱包流水和重启日志为核心实体；
- 通过活动订单唯一索引防止同一用户或同一充电桩重复占用；
- 开始充电、结束并结算、充值、冻结/解冻和远程重启应使用明确事务边界。

该文档属于设计约定，不能替代尚未实现的 `libs/database` 数据访问层和运行时事务代码。

### 2.4 `docs/architecture/protocol.md`

定义了 Socket protocol v1：

- TCP 传输；
- 四字节大端长度前缀；
- UTF-8 JSON envelope；
- `v`、`id`、`type`、`payload` 四个公共字段；
- 请求 ID 用于关联响应；
- 统一错误码，包括 `UNAUTHORIZED`、`NOT_FOUND`、`CONFLICT`、`INSUFFICIENT_BALANCE`、`DATABASE_ERROR` 等；
- 预留用户端、管理端和订单业务操作名。

协议文档同时明确：当前服务端只实现 `health` 和 `echo`，其余业务操作需要数据库服务接入后实现。

### 2.5 `database/schema/schema.sql` 和 `database/seeds/dev.sql`

已提供 SQLite schema v0.1 和确定性演示数据，包含：

- `users`；
- `administrators`；
- `stations`；
- `charging_piles`；
- `charging_orders`；
- `wallet_transactions`；
- `pile_restart_logs`；
- `station_pile_status` 和 `revenue_daily` 视图。

种子账号为本地演示用途，真实环境不得沿用默认密码设计。

### 2.6 `libs/protocol/` 和 `server/`

已提供：

- Qt/C++ 协议消息结构；
- 增量帧编码/解码；
- 协议测试工程；
- Qt TCP 服务端诊断骨架；
- `health` 和 `echo` 请求处理；
- TCP smoke 脚本；
- 服务端默认地址 `127.0.0.1:45454` 和环境变量覆盖方式。

## 3. 独立验证结果

使用本地 Git 中的 `a8e1db9` 文件，在内存 SQLite 数据库中执行 schema 和 seed 验证：

| 检查项 | 结果 | 说明 |
|---|---|---|
| schema 执行 | PASS | 空数据库可以建立表、索引和视图 |
| seed 执行 | PASS | 确定性演示数据可以导入 |
| seed 重复执行 | PASS | 重复导入后各表数量不变化 |
| 外键约束 | PASS | 当前连接启用 `PRAGMA foreign_keys = ON` |
| 数据规模 | PASS | 用户 3、管理员 1、站点 2、充电桩 5、订单 2、钱包流水 3、重启日志 1 |
| 桩 `reserved` 状态 | FAIL | `charging_piles.status` 仅包含 `idle`、`charging`、`fault`、`offline`；只有 `charging_orders.status` 支持 `reserved` |
| 订单 `exception` 状态 | FAIL | 当前 schema 的订单状态约束不包含 `exception` |
| 完成订单必须结算 | FAIL | 当前 schema 只保证 `settled_at` 非空时订单为 `completed`，允许 `completed` 订单缺少 `settled_at` |
| `libs/database` | FAIL | B 提交中没有实际数据库访问层文件 |
| 运行时业务处理器 | FAIL | 服务端当前只处理 `health` 和 `echo` |
| Qt/qmake/TCP 复跑 | NOT RUN | 当前 Windows 环境没有 `qmake6`、`cmake` 或 `sqlite3` 命令；B 提交中的构建和 smoke 结果暂记为 B 自报证据 |

## 4. 对 B-S1-01 和 B-S1-02 的状态判断

### B-S1-01：数据库模型、初始化和数据访问层

状态：**进行中**。

已完成：

- schema v0.1；
- 确定性 seed；
- 核心实体、外键、索引和基础 CHECK；
- 数据库架构说明。

仍需完成：

- `libs/database` 连接和查询/写入封装；
- 事务提交、回滚和错误转换；
- 订单 `exception` 是否纳入最终状态模型；
- `completed` 与 `settled_at` 的双向完整性约束或服务层保证；
- `revenue_daily` 按 `ended_at` 还是 `settled_at` 统计的口径确认；
- 业务写入失败、余额不足、并发占桩和重复结算验证。

### B-S1-02：Socket 服务端、协议和核心业务接口

状态：**进行中**。

已完成：

- protocol v1 envelope；
- 四字节大端长度前缀；
- 请求 ID 和统一错误码；
- 协议编解码；
- `health`/`echo` 诊断服务；
- 协议测试工程和 TCP smoke 脚本。

仍需完成：

- `user.login`、`station.list`、`pile.list` 等查询处理器；
- `charging.start`、`charging.stop`、`charging.settle` 业务处理器；
- 管理员登录、统计、站点、桩和用户管理接口；
- 数据库失败到协议错误码的统一转换；
- 请求 ID 幂等/重复提交策略；
- 请求 → 服务端 → SQLite → 响应的真实最小闭环。

## 5. A/C 当前可用边界

- 同学 A 可以继续使用 Mock 完成用户端页面、站点/桩展示、导航入口、订单状态界面和错误状态；不得直接访问 SQLite 或自行冻结未确认的业务字段。
- 同学 C 可以继续使用固定 JSON/种子导出数据完成管理端和大屏页面；不得把 `health`/`echo` 当作管理业务接口。
- A/C 真实接口切换依赖 B 提供数据库业务处理器、最终接口样例、启动命令和成功/错误响应样例。

## 6. 待重新复核事项

当提交 `1dc1340328650e8f339e99f9f59e203b50d4584b` 可以获取后，必须重新检查：

1. 订单 `exception` 和桩/订单状态约束是否真正修改；
2. `completed` 与 `settled_at` 的完整性约束是否双向生效；
3. `revenue_daily` 统计时间口径是否明确并与报告一致；
4. 是否新增 `libs/database` 数据访问层；
5. 是否新增运行时事务和业务处理器；
6. Socket 协议、端口、数据库路径和 `current.md` 是否同步；
7. B 报告中的测试是否有可复现命令和输出证据；
8. 是否出现真实密码、API Key、Token 或临时文件。

在上述复核完成前，不更新 `B-S1-01`、`B-S1-02` 为“已完成”，也不把第一阶段端到端闭环标记为完成。
