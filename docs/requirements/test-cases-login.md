# 测试用例：登录流程（TC-C-S1-001 ~ 004）

> 对应需求：C-S1-001（管理员登录）。正式 docx 模板（课堂派）到达前以此文件为内容源。
> 契约依据：`docs/architecture/protocol.md` —— `admin.login`(username, password) → `admin.login.result`:
> `admin` + 8 小时有效 `token`；除登录外的所有 `admin.*` 请求均须携带该 token；
> 错误分支按 code 不按 message：1100 UNAUTHORIZED。
> 自动化实现：`apps/admin-client/tests/loginflow/tst_loginflow.cpp`（Qt Test）。
> 执行状态更新时机：每次代码变更后跑 `tst_loginflow` 更新。

## 前置条件（全部用例）

- admin-client 已构建（`apps/admin-client/README.md` 构建命令）
- 数据层为 `MockAdminRepository`（第一阶段默认）：固定账号 `admin` / `123456`（对齐 `database/seeds/dev.sql`）

## TC-C-S1-001 登录成功进入业务区（正常）

| 项 | 内容 |
|---|---|
| 场景 | 正常 |
| 步骤 | 1) 启动 admin-client；2) 账号输入 `admin`；3) 密码输入 `123456`；4) 点击"登 录" |
| 预期结果 | 进入业务区；导航栏可用；状态栏显示"已登录（Mock 演示）· 概览"；概览页显示 Mock 摘要 |
| 自动化 | `tst_loginflow::loginSuccessEntersBusinessArea` |
| 状态 | 通过（2026-09-02） |

## TC-C-S1-002 密码错误提示且保持锁定（异常）

| 项 | 内容 |
|---|---|
| 场景 | 异常（认证失败） |
| 步骤 | 1) 启动 admin-client；2) 账号输入 `admin`；3) 密码输入错误值（如 `wrong-pass`）；4) 点击"登 录" |
| 预期结果 | 停留在登录页；错误标签可见，文本"账号或密码错误"（错误码 1100 分支）；导航仍不可用 |
| 自动化 | `tst_loginflow::wrongPasswordShowsUnauthorized` |
| 状态 | 通过（2026-09-02） |

## TC-C-S1-003 空账号/空密码拦截（边界）

| 项 | 内容 |
|---|---|
| 场景 | 边界（输入校验） |
| 步骤 | 1) 启动 admin-client；2) 依次尝试三种输入：账号留空+密码留空 / 全空格账号+正确密码 / 正确账号+密码留空；3) 每次点击"登 录" |
| 预期结果 | 不发起登录（Repository 不被调用，login 调用计数为 0）；错误标签可见，文本"请输入账号和密码"；停留登录页 |
| 自动化 | `tst_loginflow::emptyInputShowsValidationErrorAndSkipsRepository`（三组输入 + 调用计数断言） |
| 状态 | 通过（2026-09-02，独立测试 + Repository 未调用断言） |

## TC-C-S1-004 服务不可用提示（异常/联调）

| 项 | 内容 |
|---|---|
| 场景 | 异常（传输层） |
| 步骤 | 1) 以 ServiceUnavailable 模式启动（`MockAdminRepository::LoginMode::ServiceUnavailable`）；2) 输入正确凭据；3) 点击"登 录" |
| 预期结果 | 停留登录页；错误标签文本"服务不可用，请稍后重试"；不按 message 文本分支 |
| 自动化 | `tst_loginflow::serviceUnavailableShowsNetworkError` |
| 状态 | 通过（2026-09-02） |

## 防回退补充断言（附属于 C-S1-002）

登出后（点击"退出登录"）：导航禁用；即使编程切换导航行，主栈仍停留登录页（`mainStack` currentIndex=0）；重新登录需再次走认证。
自动化：`tst_launchsmoke::businessAreaLockedUntilLogin`（2026-09-02 扩展）。
