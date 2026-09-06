# 数据库与服务端评审修改要求

本文整理数据库、服务端、协议以及 A 用户端适配相关的评审意见，作为后续实现、联调和验收清单。

## P0：合并前必须解决的问题

### 1. 必须提供完整的数据库事务一致性证明

开发计划明确要求以下操作必须在同一个业务事务内完成：

- 创建预约：订单变为 `pending_reservation`，充电桩变为 `reserved`。
- 确认预约：订单变为 `reserved`。
- 开始充电：订单和充电桩同时变为 `charging`。
- 停止充电：订单变为 `pending_settlement`。
- 结算：钱包扣款、钱包流水、订单完成、充电桩释放必须同时成功或同时回滚。

当前 PR 的结算路径已经使用 `BEGIN IMMEDIATE`，也实现了余额不足时回滚，但必须补充可重复的并发测试，不能只依赖单线程冒烟脚本。

#### 两个客户端同时结算同一个订单

- 只能有一个请求成功；
- 只能生成一条 `charge` 流水；
- 用户余额只能扣除一次；
- 订单只能完成一次；
- 充电桩只能释放一次。

#### 两个客户端同时预约同一个闲置桩

- 只能有一个预约成功；
- 只能生成一个活动订单；
- 只能有一个请求将桩更新为 `reserved`；
- 另一个请求必须返回 `CONFLICT`。

#### 结算过程中模拟数据库写入失败

- 订单仍为 `pending_settlement`；
- 充电桩仍为 `charging`；
- 用户余额不变；
- 不存在半条 `wallet_transactions` 记录。

当前 `settleCharging()` 的核心代码位于 `database.cpp:952`，需要增加故障注入或事务回滚测试。

### 2. 必须强化订单状态约束

当前 Schema 主要限制了：

- `pending_settlement` 必须有 `ended_at`；
- `completed` 必须有开始、结束、结算时间；
- 完成订单必须存在对应扣款流水。

仍需补充以下语义约束：

#### `pending_reservation`

```text
reserved_at IS NOT NULL
started_at IS NULL
ended_at IS NULL
settled_at IS NULL
```

#### `reserved`

```text
reserved_at IS NOT NULL
started_at IS NULL
ended_at IS NULL
settled_at IS NULL
```

#### `charging`

```text
started_at IS NOT NULL
ended_at IS NULL
settled_at IS NULL
```

#### `pending_settlement`

```text
started_at IS NOT NULL
ended_at IS NOT NULL
settled_at IS NULL
total_amount_cents > 0
```

#### `completed`

```text
started_at IS NOT NULL
ended_at IS NOT NULL
settled_at IS NOT NULL
total_amount_cents > 0
```

此外，时间字段目前只是 `TEXT`，应统一采用 UTC ISO-8601 格式。如果 SQLite `CHECK` 约束无法表达跨表条件，就必须在 `libs/database` 中统一实现，并增加直接构造异常数据的测试。

### 3. 结算金额必须完全使用整数分，并明确计算规则

计费字段单位必须明确：

- `energy_wh` 的单位是瓦时；
- `unit_price_cents_per_kwh` 的单位是分/千瓦时；
- `total_amount_cents` 的单位是分。

必须明确服务费是否计入总金额、小于 1 分时的处理、能量为 0 时是否允许结算，以及采用四舍五入、向上取整还是截断。

建议唯一规则为：

```text
total_amount_cents =
ceil(energy_wh × unit_price_cents_per_kwh / 1000)
+ service_fee_cents
```

服务端必须根据订单保存的费率重新计算金额，不能信任客户端传入的金额。该规则必须同步写入数据库文档和 API 文档。

### 4. 钱包流水必须与余额建立更强的一致性约束

`wallet_transactions.balance_after_cents` 必须和用户在该笔流水之后的余额相匹配。

需要明确：

- 充值流水是否必须有幂等键；
- 充值金额是否必须为正；
- 扣款金额是否必须为负；
- `charge` 是否只能对应一个已完成订单；
- `refund` 是否必须关联原扣款或订单；
- `adjustment` 是否必须关联管理员；
- 钱包流水是否允许删除；
- 是否允许修改历史流水；
- 钱包流水是否必须只追加。

建议：

- 充值请求必须带业务幂等键；
- `idempotency_key` 对充值和扣款都应为非空；
- 已完成订单对应的 `charge` 流水禁止删除和修改；
- `refund`、`adjustment` 必须增加操作人或审计信息；
- 增加“用户余额与最后一条流水余额一致”的一致性检查；
- 增加余额负数、重复扣款、重复充值测试。

当前 Schema 相关部分见 `schema.sql:143`。

### 5. 请求幂等记录需要明确作用域和生命周期

`request_records` 使用以下字段实现重复请求回放：

```text
request_id
operation
fingerprint
response_json
```

当前 `request_id` 是全局主键：

```sql
request_id TEXT PRIMARY KEY
```

协议必须明确请求 ID 是全局唯一、用户范围唯一、TCP 连接范围唯一还是服务端实例范围唯一。建议规定请求 ID 必须由客户端生成，并在整个服务端数据库范围内唯一；或者为 `request_records` 增加 client/session/user 作用域。

指纹不应继续依赖简单字符串拼接，例如：

```text
userId:pileId
userId:orderId
userId:orderId:endedAt
```

建议使用：

```text
operation + canonical JSON payload
```

对 JSON 字段进行稳定排序后再生成哈希。

协议还必须明确以下失败请求规则：余额不足的结算请求是否允许用相同 ID 在充值后重试、参数错误是否允许复用相同 ID、数据库异常后相同 ID 是否可以安全重试，以及状态冲突后重试是否返回新的状态。

这些规则必须写入 `docs/api/README.md` 和 `docs/architecture/protocol.md`，并由测试固定下来。

## P1：必须在本 PR 或紧随 PR 中解决的问题

### 6. 当前 B 响应字段与 A 用户端适配器不一致

这是当前真实 Socket 联调的主要阻塞点。B 数据库层返回字段和 A 当前 `SocketUserService` 读取字段存在明显差异。

#### 站点字段不一致

B 返回：

```json
{
  "pile_total": 3,
  "pile_idle": 1,
  "pile_reserved": 1,
  "pile_charging": 1
}
```

A 当前适配器读取：

```json
{
  "total_piles": 3,
  "available_piles": 1,
  "price_cents_per_kwh": 120
}
```

涉及 B 的 `database.cpp:478`。建议统一为：

```text
pile_total
pile_idle
pile_reserved
pile_charging
```

A 适配层应按冻结后的字段映射，页面不得自行猜测字段名。

#### 充电桩字段不一致

B 返回：

```json
{
  "pile_code": "A-01",
  "pile_type": "fast"
}
```

A 当前适配器主要读取：

```json
{
  "code": "...",
  "type": "..."
}
```

建议最终统一为：

```text
pile_code
pile_type
power_kw
unit_price_cents_per_kwh
status
```

不建议同时保留多套别名。相关代码位于 `database.cpp:428`。

#### 订单字段不一致

B 返回：

```text
unit_price_cents_per_kwh
total_amount_cents
```

A 适配层此前使用过：

```text
unit_price_cents
amount_cents
```

B 当前订单查询还没有返回：

```text
station_name
station_address
pile_number
```

要求 B 明确订单响应的完整字段，增加站点名称、站点地址、桩编号的稳定映射，或规定客户端根据 `station_id`、`pile_id` 再查询详情。A/C 不允许通过猜测字段名联调。

### 7. `order.history.list` 需要先完成协议决策

PR 新增了 `order.history.list`，但 2026-09-02 的协议总结中原本没有该操作，且 `protocol-summary-2026-09-02.md` 尚未同步变化。

团队必须确认：

- `order.history.list` 是否正式纳入 v1；
- 如果纳入，是否更新协议总结；
- A 适配器是否必须实现；
- 历史记录只返回 `completed`，还是包含 `cancelled`、`exception`；
- 排序依据是 `settled_at`、`ended_at` 还是 `created_at`；
- 是否需要分页。

当前 `listOrderHistory()` 使用：

```sql
WHERE user_id = :user_id
ORDER BY created_at DESC, id DESC
```

如果“历史充电记录”定义为已完成充电，建议改为：

```sql
WHERE user_id = :user_id
  AND status = 'completed'
ORDER BY settled_at DESC, id DESC
```

应增加混合状态测试：`completed`、`cancelled`、`charging`、`pending_settlement` 同时存在时，历史接口只能返回协议规定的终态记录。

### 8. 订单历史需要返回用户端要求的展示字段

用户端历史记录要求展示：完成时间、充电站地址、花费金额。

建议数据库服务层提供稳定 DTO：

```json
{
  "id": 1001,
  "status": "completed",
  "ended_at": "2026-09-01T10:15:00Z",
  "settled_at": "2026-09-01T10:16:00Z",
  "station_id": 1,
  "station_name": "软件园一号站",
  "station_address": "沈阳市浑南区软件园路1号",
  "pile_id": 101,
  "pile_code": "A-01",
  "total_amount_cents": 3050
}
```

数据库层可以使用 `JOIN`，或者建立明确的读取 DTO，但客户端不能直接访问 SQLite。

### 9. 预约取消必须校验订单与充电桩的对应关系

取消预约时必须保证：

- 该桩当前仍由该订单占用；
- 该订单处于 `pending_reservation` 或 `reserved`；
- 不能释放其他订单占用的 `reserved` 桩；
- 不能取消 `charging`、`pending_settlement` 或 `completed` 订单。

建议在同一事务中使用能够验证订单归属的条件：

```sql
EXISTS (
    SELECT 1
    FROM charging_orders
    WHERE id = :order_id
      AND pile_id = :pile_id
      AND status IN ('pending_reservation', 'reserved')
)
```

相关实现位于 `database.cpp:776`。

### 10. 直接开始充电和预约开始充电必须统一业务规则

PR 支持两种开始方式：

```text
charging.start {user_id, order_id}
charging.start {user_id, pile_id}
```

两条路径都必须校验：

- 用户存在且状态为 `active`；
- 用户不存在其他活动订单；
- 充电桩属于活动站点；
- 充电桩状态为 `idle` 或对应预约状态；
- 充电桩没有被其他订单占用；
- 状态修改和订单创建在同一事务内完成。

必须测试：

- 用户已有 `pending_reservation` 时，不能通过 `pile_id` 直接开始另一根桩；
- 用户已有 `charging` 时，不能通过 `order_id` 或 `pile_id` 再次开始充电；
- 冻结用户不能创建预约、确认预约、开始充电或结算。

### 11. 服务端数据库访问不能阻塞所有 Socket 连接

当前 `server/src/main.cpp:21` 中，每个 `ClientConnection` 虽然拥有独立 `Database` 对象，但仍在同一个 Qt 主线程的 `readyRead` 回调中同步调用数据库。

这可能导致：

- 一个慢查询阻塞整个服务端事件循环；
- 一个 SQLite 锁等待阻塞其他客户端；
- 一个数据库初始化影响所有新连接。

建议至少采用以下方案之一：

- 将数据库操作移至专用 worker 线程；
- 使用 Qt queued connection；
- 为每个连接建立明确的数据库线程归属；
- 对数据库锁等待设置最大时间；
- 将长事务和查询从 Socket 线程中移出。

并发测试要求：客户端 A 持有数据库锁时，客户端 B 的 health/echo 不应被无限阻塞，只读查询应在可接受超时内返回，锁冲突应转换为 `DATABASE_ERROR` 或稳定业务错误。

### 12. 数据库初始化和种子数据必须可重复运行

当前种子数据使用 `INSERT OR IGNORE`，但冒烟测试依赖固定订单、桩、用户、冻结用户和余额状态，第一次执行后可能改变状态，导致第二次运行失败。

应采用以下方案之一：

- 每次冒烟测试使用全新的临时数据库；
- 测试开始前执行数据库 reset/seed；
- 为每次测试生成隔离数据库；
- 测试结束后删除临时数据库。

测试输出必须包含数据库路径和 schema 版本。

验收目标：

```text
在同一工作目录连续运行两次 smoke.py，
两次结果均 PASS，
且结果不会依赖上一次运行留下的订单状态。
```

## P2：建议补充的问题

### 13. 增加数据库索引和查询计划验证

建议使用 `EXPLAIN QUERY PLAN` 验证：

- 按手机号查询用户；
- 按站点查询充电桩；
- 按用户查询活动订单；
- 按用户查询历史订单；
- 按桩查询活动订单；
- 按结算时间查询营收统计。

建议补充或确认：

```sql
CREATE INDEX ix_users_phone
ON users(phone);

CREATE INDEX ix_orders_user_settled
ON charging_orders(user_id, settled_at DESC);

CREATE INDEX ix_orders_status_settled
ON charging_orders(status, settled_at DESC);
```

唯一索引、部分索引和普通索引不要重复创建，迁移脚本和新建 Schema 必须保持一致。

### 14. 增加 SQLite 运行参数和恢复策略

建议在数据库文档中固定：

```text
PRAGMA foreign_keys = ON
PRAGMA busy_timeout = 5000
journal_mode = WAL 或 DELETE
synchronous = NORMAL 或 FULL
```

并说明：

- 为什么选择该 journal 模式；
- 数据库损坏时如何检测；
- 是否支持数据库备份；
- 迁移前是否自动生成备份；
- 迁移失败后如何恢复；
- SQLite 锁冲突如何对外转换。

### 15. 增加数据库异常和故障注入测试

建议增加以下测试：

- schema 文件不存在；
- seed 文件不存在；
- schema 版本错误；
- 数据库目录不可写；
- 数据库只读；
- SQLite 锁冲突；
- 钱包流水写入失败；
- 订单更新失败；
- 充电桩释放失败；
- 请求记录写入失败；
- response JSON 损坏；
- 数据库文件损坏；
- 外键违反；
- 迁移中断。

每个错误都必须：

- 返回稳定错误码；
- 事务回滚；
- 数据库仍可重新打开；
- 不会留下半完成订单。

### 16. 管理员操作必须保留审计关系

开发计划包含：

- 冻结/解冻用户；
- 远程重启充电桩；
- 站点和桩管理；
- 统计查询。

当前 `pile_restart_logs` 已有基础表，但仍需确认：

- `administrator_id` 是否不能为空；
- 操作失败是否也必须记录；
- 重启原因是否必填；
- 被拒绝操作是否记录；
- 用户冻结/解冻是否有历史审计表；
- 钱包调账是否记录操作人；
- 审计记录是否允许删除或修改。

管理员动作不能只修改当前状态而没有历史记录。

## 评审后的交付检查清单

- [ ] 事务边界和回滚路径有代码证据。
- [ ] 同订单并发结算测试通过。
- [ ] 同桩并发预约测试通过。
- [ ] 数据库故障注入测试通过。
- [ ] 订单状态与时间字段约束已统一。
- [ ] 金额计算完全使用整数分并写入文档。
- [ ] 钱包流水、余额和幂等关系已固定。
- [ ] B/A DTO 字段完成共同冻结。
- [ ] `order.history.list` 是否纳入 v1 已完成决策。
- [ ] 订单历史包含用户端需要的展示字段。
- [ ] 预约取消校验订单与桩的对应关系。
- [ ] 两种开始充电路径遵守同一业务规则。
- [ ] 数据库操作不会阻塞所有 Socket 连接。
- [ ] 冒烟测试可重复执行。
- [ ] 索引、SQLite 参数、恢复策略和审计关系有文档及测试证据。
