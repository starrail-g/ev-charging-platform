# API 文档 / API Reference

本目录记录跨模块接口契约。Socket API 使用
[`docs/architecture/protocol.md`](../architecture/protocol.md) 定义的协议；
示例中的 ID 为整数，金额单位为人民币分，时间为 UTC ISO-8601。

## 当前实现与管理端依赖

服务端当前可运行：`health`、`echo`、`user.login`、`station.list`、
`pile.list`、`order.active.get`、`order.history.list`、预约生命周期和充电
开始/停止/结算。管理端后续依赖的 `admin.login`、统计、管理员桩/站/用户操作
仍为契约，尚未由服务端实现。管理端第一阶段使用 `AdminRepository` 的 Mock
实现；接入真实 Socket 前必须冻结字段并完成接口闸门确认。

每个状态修改请求都必须使用客户端生成的 `id`（1 至 64 字符）。相同操作和
标识性 payload 重放同一 ID 会返回第一次成功响应，即使客户端已重连；同一 ID
用于不同操作或参数会返回 `CONFLICT`（1201）。当前支持幂等的操作为
`reservation.create`、`reservation.confirm`、`reservation.cancel`、
`charging.start`、`charging.stop`、`charging.settle`。

## 登录与查询

```json
{"v":1,"id":"login-1","type":"user.login","payload":{"phone":"13912345678"}}
```

```json
{"v":1,"id":"login-1","type":"user.login.result","payload":{"user":{"id":1,"phone":"13912345678","nickname":"用户5678","avatar_path":null,"balance_cents":0,"status":"active"}}}
```

```json
{"v":1,"id":"station-1","type":"station.list","payload":{}}
{"v":1,"id":"pile-1","type":"pile.list","payload":{"station_id":1}}
{"v":1,"id":"active-1","type":"order.active.get","payload":{"user_id":1}}
{"v":1,"id":"history-1","type":"order.history.list","payload":{"user_id":1}}
```

`station.list.result` 返回 `stations`；`pile.list.result` 返回 `piles`。桩状态
为 `idle`、`reserved`、`charging`、`fault`、`offline`。活动订单查询返回订单或
JSON `null`；历史接口只返回 `completed` 订单，按 `settled_at` 倒序返回 `orders`，
并包含 `station_name`、`station_address`、`pile_code` 和金额字段。

## 预约

```json
{"v":1,"id":"reserve-1","type":"reservation.create","payload":{"user_id":1,"pile_id":101}}
```

成功响应包含 `pending_reservation` 订单和 `reserved` 电桩。使用返回的订单 ID
确认或取消：

```json
{"v":1,"id":"confirm-1","type":"reservation.confirm","payload":{"user_id":1,"order_id":2001}}
{"v":1,"id":"cancel-1","type":"reservation.cancel","payload":{"user_id":1,"order_id":2001}}
```

确认后的订单为 `reserved`；取消后的订单为 `cancelled`，电桩回到 `idle`。

## 充电与结算

预约启动和直接启动分别使用 `order_id` 或空闲桩的 `pile_id`，二者不能同时提供：

```json
{"v":1,"id":"start-1","type":"charging.start","payload":{"user_id":1,"order_id":2001}}
{"v":1,"id":"direct-start-1","type":"charging.start","payload":{"user_id":1,"pile_id":101}}
```

两种方式都原子地返回 `charging` 状态的订单和电桩。停止和结算：

```json
{"v":1,"id":"stop-1","type":"charging.stop","payload":{"user_id":1,"order_id":2001,"ended_at":"2026-09-01T10:15:00Z"}}
{"v":1,"id":"settle-1","type":"charging.settle","payload":{"user_id":1,"order_id":2001}}
```

停止后订单为 `pending_settlement`；结算原子写入 charge 流水、扣减余额、将订单
置为 `completed` 并释放电桩。已完成订单必须同时具有 `started_at`、`ended_at`、
`settled_at` 和匹配的钱包流水。

结算金额完全使用整数分：`energy_wh` 为瓦时，费率为分/千瓦时，服务费为分，
`total_amount_cents = ceil(energy_wh * unit_price_cents_per_kwh / 1000) + service_fee_cents`。
服务端使用订单保存的费率和服务费重新计算，不信任客户端金额；充电停止时能量至少为
1 Wh，结算总额必须为正。

## 错误和业务规则

所有失败响应的 `type` 为 `error`，并保留请求 ID：

```json
{"v":1,"id":"settle-1","type":"error","payload":{"code":1202,"name":"INSUFFICIENT_BALANCE","message":"insufficient balance"}}
```

余额不足时订单保持 `pending_settlement`，电桩保持 `charging`，余额和 charge 流水
不变。非法状态转换、重复活动订单、占用/故障电桩和重复请求 ID 参数冲突返回
`CONFLICT`（1201）；缺少资源返回 `NOT_FOUND`（1200）；持久化失败返回
`DATABASE_ERROR`（1300）且事务回滚。

冻结用户不能创建或确认预约、开始充电或结算。当前冻结不会自动关闭已经运行的
订单，该策略将在管理员冻结接口实现时单独确定。

请求 ID 在服务端数据库保留期内全局唯一。只有成功提交的状态修改会写入回放记录；
业务拒绝或数据库失败会回滚且不会固化记录，因此相同 ID 可在条件修复后重试，参数
变化或操作变化则返回 `CONFLICT`。

管理员接口草案：

| 接口 | 用途 | 状态 |
|---|---|---|
| `admin.login` | 管理员认证，错误码 1100 | 已列入协议，服务端待实现 |
| `admin.statistics.get` | 营收、桩状态、利用率摘要 | 字段待评审 |
| `admin.pile.restart` | 桩重启和审计 | 服务端待实现 |
| `admin.station.*` | 站点查询/创建 | 服务端待实现 |
| `admin.user.list/status.set` | 用户查询、冻结/解冻 | 服务端待实现 |

接口闸门通过前，管理端 Mock 数据不得冒充真实 Socket 联调结果。
