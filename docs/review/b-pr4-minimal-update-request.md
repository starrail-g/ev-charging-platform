# B PR#4 最小修改建议

> 目标：B 完成本清单后，A-S1-03 结束，后续只进行 A-S1-04 联调与回归。
> 当前基准：PR#4 head 7ad915302c28136d8b0e9a8c34363266412c58eb（已完成，本文作为评审记录保留）

## 必须补齐

### 1. 用户接口

实现并接入 Protocol v1：

- user.profile.get
- user.profile.update
- wallet.recharge

要求：

- 字段严格使用 protocol.md 定义。
- user_id 使用 JSON 整数。
- 金额使用 amount_cents 整数分。
- 成功返回 user 或 balance_cents、transaction_id。
- 用户不存在返回 1200。
- 冻结用户对预约创建、预约确认、开始充电和充值返回 1101 ACCOUNT_FROZEN；只读及收尾操作放行。
- 非法参数返回 1002。
- 数据库失败返回 1300。

### 2. 数据库事务

- profile 更新必须持久化 nickname、avatar_path 和 updated_at。
- recharge 必须在同一事务中：
  1. 校验用户状态；
  2. 增加 balance_cents；
  3. 写入 wallet_transactions 的 recharge 流水；
  4. 返回最新余额。
- 充值失败必须完整回滚。
- 重复 request id 或幂等键不得重复充值。
- 余额不能为负数。
- 不得修改已完成订单的 charge 流水约束。

### 3. 已有充电接口回归

保持以下行为不变：

- reservation.create 返回 pending_reservation。
- reservation.confirm 后变为 reserved。
- reservation.cancel 同步释放桩为 idle。
- charging.start/stop 同步更新订单和桩状态。
- charging.settle 原子扣款并写 charge 流水。
- 余额不足返回 1202，订单、余额和流水不变。
- active order 无数据返回 JSON null。
- history 按 settled_at 倒序。
- 金额全部为整数分。

### 4. 协议与错误

- 更新 docs/architecture/protocol.md 和 docs/api/README.md。
- 为 profile、update、recharge 提供请求、成功、失败 JSON 样例。
- 保持四字节大端长度帧、v=1、请求 id 和统一 error 响应。
- 重复 request id、半包/粘包、断连和数据库失败必须有稳定结果。
- 错误信息不得泄露 SQL、数据库路径、堆栈、密码或密钥。

### 5. 测试与交付

PR 中至少提供：

- profile 查询和持久化测试；
- profile 更新失败/冻结用户测试；
- recharge 成功、非法金额、冻结用户测试；
- recharge 重复请求和回滚测试；
- 现有预约—充电—结算回归测试；
- 并发预约、并发结算测试；
- 空数据库初始化和 seed 命令；
- qmake6 构建、server smoke 和端到端脚本。

交付时提供：

- 最新 commit 哈希；
- 服务端启动命令、端口和数据库初始化命令；
- API 样例；
- 测试结果；
- 明确说明是否还有未实现用户端接口。

## 解锁条件（已满足）

以下条件全部满足后，A 才将 A-S1-03 标记完成：

- [x] profile.get 已实现
- [x] profile.update 已实现并持久化
- [x] wallet.recharge 已实现并原子记账
- [x] 余额不足和数据库失败可回滚
- [x] 重复请求不会重复写入
- [x] 协议文档和 API 样例已更新
- [x] qmake6、smoke、并发和端到端测试通过
