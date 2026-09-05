# API 文档 / API Reference

本目录记录跨模块接口契约。Socket API 使用
[`docs/architecture/protocol.md`](../architecture/protocol.md) 定义的协议；
示例中的 ID 为整数，金额单位为人民币分，时间为 UTC ISO-8601。

## 当前实现与管理端依赖

服务端当前可运行：`health`、`echo`、`user.login`、`user.profile.get`、
`user.profile.update`、`wallet.recharge`、`station.list`、
`pile.list`、`order.active.get`、`order.history.list`、预约生命周期和充电
开始/停止/结算，以及本分支新增的 `admin.login`、统计、管理员桩/站/用户操作。
管理端 `AdminRepository` 仍使用 Mock；真实 Socket adapter 尚未接入，需按下方
wire 映射契约完成客户端联调。

主分支协作约定仍适用：C 端的管理员登录、概览统计和桩状态接口是第一阶段
联调闸门，目标时间为 9 月 7 日 18:00；在真实 Socket adapter 验证前，A/C
保留 Mock 或离线回退，不得把 Mock 结果当作真实 Socket 验收证据。B 端用户接口、
充电生命周期和本分支管理员接口均按下文 v1 契约提供。

每个状态修改请求都必须使用客户端生成的 `id`（1 至 64 字符）。相同操作和
标识性 payload 重放同一 ID 会返回第一次成功响应，即使客户端已重连；同一 ID
用于不同操作或参数会返回 `CONFLICT`（1201）。当前支持幂等的操作为
`reservation.create`、`reservation.confirm`、`reservation.cancel`、
`charging.start`、`charging.stop`、`charging.settle`、
`user.profile.update`、`wallet.recharge`。

## 登录与查询

```json
{"v":1,"id":"login-1","type":"user.login","payload":{"phone":"13912345678"}}
```

```json
{"v":1,"id":"login-1","type":"user.login.result","payload":{"user":{"id":1,"phone":"13912345678","nickname":"用户5678","avatar_path":null,"balance_cents":0,"status":"active"}}}
```

## 用户资料与钱包

资料查询只接受整数 `user_id`：

```json
{"v":1,"id":"profile-1","type":"user.profile.get","payload":{"user_id":1}}
{"v":1,"id":"profile-1","type":"user.profile.get.result","payload":{"user":{"id":1,"phone":"13800138000","nickname":"用户8000","avatar_path":null,"balance_cents":16950,"status":"active"}}}
```

资料更新必须提供 `nickname` 或 `avatar_path` 至少一个字段。`avatar_path` 可为字符串，
也可显式传 `null` 清除头像；成功返回持久化后的完整 `user`：

```json
{"v":1,"id":"profile-update-1","type":"user.profile.update","payload":{"user_id":1,"nickname":"新能源车主","avatar_path":"avatars/user-1.png"}}
{"v":1,"id":"profile-update-1","type":"user.profile.update.result","payload":{"user":{"id":1,"phone":"13800138000","nickname":"新能源车主","avatar_path":"avatars/user-1.png","balance_cents":16950,"status":"active"}}}
```

充值金额是正整数分。服务端在一个事务中校验用户、更新余额、追加 `recharge`
流水并保存幂等响应：

```json
{"v":1,"id":"recharge-1","type":"wallet.recharge","payload":{"user_id":1,"amount_cents":5000}}
{"v":1,"id":"recharge-1","type":"wallet.recharge.result","payload":{"balance_cents":21950,"transaction_id":5005}}
```

失败统一使用 error 信封：

```json
{"v":1,"id":"profile-missing","type":"error","payload":{"code":1200,"name":"NOT_FOUND","message":"user not found"}}
{"v":1,"id":"recharge-frozen","type":"error","payload":{"code":1101,"name":"ACCOUNT_FROZEN","message":"user is frozen"}}
{"v":1,"id":"recharge-invalid","type":"error","payload":{"code":1002,"name":"INVALID_REQUEST","message":"user_id and amount_cents must be positive integers"}}
{"v":1,"id":"recharge-db-error","type":"error","payload":{"code":1300,"name":"DATABASE_ERROR","message":"wallet recharge failed"}}
```

相同请求 ID 和相同 payload 的资料更新或充值会返回第一次成功结果，不重复更新或
入账；同一 ID 更换操作或参数返回 `CONFLICT`。成功回放优先于冻结校验：用户被冻结
后仍返回原成功结果；新请求才会返回 `1101 ACCOUNT_FROZEN`。业务拒绝和数据库失败不写回放记录，
故障解除后可使用原 ID 重试。

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

停止后订单为 `pending_settlement` 且电桩立即回到 `idle`；结算只原子写入 charge
流水、扣减余额、将订单置为 `completed` 并累加电桩计数，不改变电桩状态。已完成订单必须同时具有 `started_at`、`ended_at`、
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

余额不足时订单保持 `pending_settlement`，电桩已经是 `idle`，余额和 charge 流水
不变。非法状态转换、重复活动订单、占用/故障电桩和重复请求 ID 参数冲突返回
`CONFLICT`（1201）；缺少资源返回 `NOT_FOUND`（1200）；持久化失败返回
`DATABASE_ERROR`（1300）且事务回滚。

登录冻结用户仍成功并返回 `status: "frozen"`。冻结只拦截预约创建、预约确认、开始
充电（直充和预约）及充值，统一返回 `1101 ACCOUNT_FROZEN`；所有只读接口、资料更新、
预约取消、停止充电和结算均放行。冻结不会自动关闭已经运行的订单。

请求 ID 在服务端数据库保留期内全局唯一。只有成功提交的状态修改会写入回放记录；
业务拒绝或数据库失败会回滚且不会固化记录，因此相同 ID 可在条件修复后重试，参数
变化或操作变化则返回 `CONFLICT`。

管理员接口当前实现：

| 接口 | 用途 | 状态 |
|---|---|---|
| `admin.login` | 管理员认证，错误码 1100 | 服务端已实现；无状态 token，后续请求携带 `administrator_id` |
| `admin.statistics.get` | 营收、桩状态、利用率摘要和逐日营收序列 | 服务端已实现，支持 `7d` / `30d`，返回固定长度 `revenue_daily` |
| `admin.pile.restart` | 桩重启和审计 | 服务端已实现；故障/离线桩恢复为空闲，其它状态返回冲突并保留审计记录 |
| `admin.station.list/create` | 站点查询/创建 | 服务端已实现；创建为超级管理员操作并按请求 ID 幂等 |
| `admin.user.list/status.set` | 用户查询、冻结/解冻 | 服务端已实现；状态修改为超级管理员操作并按请求 ID 幂等 |

## 管理端 AdminRepository 契约与 wire 映射

管理端 UI（`apps/admin-client`）通过 `AdminRepository` 抽象访问数据；当前唯一实现
是 `MockAdminRepository`（本 PR 交付）。接口语义是**管理端业务视图**，与 wire 协议
**非一对一**，映射关系如下（Socket 实现方案 2026-09-03 设计评审定稿，实现随 9/6
Socket 任务落地）：

| AdminRepository 方法 | 业务视图语义 | wire 映射策略 |
|---|---|---|
| `fetchOverview` | 概览指标（7 日/30 日营收、桩五态、利用率、快照时间） | 分别请求 `admin.statistics.get` 的 `7d` 与 `30d`；聚合卡片读取 `revenue_cents`，趋势图读取 `statistics.revenue_daily[*].revenue_cents` |
| `fetchStations` | 管理端全量站点（含桩数/在线率/7 日利用率聚合视图） | `admin.station.list` |
| `fetchUsers` | 管理端全量用户（含注册时间和活动订单状态） | `admin.user.list` |
| `fetchPiles` | 管理端**全量**桩列表（跨站，桩页过滤/搜索在本端完成） | 逐站 fan-out：`admin.station.list` → 每站 `pile.list(station_id)` → 合并（默认，D5） |

`fetchPiles` 的已知权衡（对应 9/3 评审 Q2 协议缺口——wire 暂无 `admin.pile.list`）：

- **请求数**：1 次 `station.list` + N 次 `pile.list`（N = 站数）；两站演示数据为 3 个请求。
- **跨站快照一致性**：各站 `pile.list` 为顺序请求、同一刷新周期发起；管理端查询是
  非事务性只读视图，允许站间秒级差异，刷新后重新取全量。
- **部分失败**：任一站点请求失败（协议错误或网络错误）→ 本次 `fetchPiles` 按整页
  错误结束（管理端需要一致的全量视图，不允许静默缺站），UI 进入 error 态可重试；
  查询类请求无幂等限制，可原样重试。
- **协议演进**：若后续冻结 `admin.pile.list`（可带可选 `station_id`/查询参数），
  adapter 内部切换为单请求实现，`AdminRepository` 接口不变。

接口变更说明：三个 `fetch*` 纯虚方法自 PR #6（2026-09-02 合并）起即为
`AdminRepository` 契约的一部分，本 PR 为回退后的恢复而非新设计。当前仓库中接口
实现仅有 `MockAdminRepository` 且与本 PR 同步交付，不破坏 `main` 编译；接口
breaking-change 风险窗口在后续 Socket 实现（新实现类）接入时，届时需同步新增
实现类并保持本契约不变。

接口闸门通过前，管理端 Mock 数据不得冒充真实 Socket 联调结果。

### 管理端统计响应

`admin.statistics.get` 的 `range` 决定逐日序列长度。日期按 UTC 日历日计算，
从最早日升序排列到当前 UTC 日；没有已完成订单的日期也会返回 0，避免客户端
因缺失日期错位绘图。`revenue_cents`、`completed_order_count` 和 `energy_wh`
分别等于 `revenue_daily` 对应字段之和：

```json
{
  "v": 1,
  "id": "admin-statistics-7d",
  "type": "admin.statistics.get.result",
  "payload": {
    "statistics": {
      "range": "7d",
      "revenue_cents": 3050,
      "revenue_daily": [
        {"date": "2026-08-30", "revenue_cents": 0, "completed_order_count": 0, "energy_wh": 0},
        {"date": "2026-08-31", "revenue_cents": 3050, "completed_order_count": 1, "energy_wh": 25000},
        {"date": "2026-09-01", "revenue_cents": 0, "completed_order_count": 0, "energy_wh": 0}
      ],
      "completed_order_count": 1,
      "energy_wh": 25000,
      "updated_at": "2026-09-05T03:20:00Z"
    }
  }
}
```

示例仅展示序列中间字段；实际 `7d` 响应包含 7 条、`30d` 响应包含 30 条。

### 管理端站点利用率

`admin.station.list` 返回的每个站点对象包含 `utilization` 和
`utilization_range: "7d"`，可直接用于站点利用率排行。该值统一定义为最近 7 个
UTC 自然日内该站实际充电总时长除以该站所有充电桩在统计周期内的可提供总时长：

- 分子只累计 `charging`、`pending_settlement`、`completed` 订单的
  `started_at` 至 `ended_at` 与 `[period_start, period_end)` 的交集；`charging`
  且 `ended_at` 为空时，以统计截止时间作为结束时间。
- 分母按桩累加 `period_end - max(period_start, pile.created_at)`；故新建桩只从
  `created_at` 开始计入。`fault`、`offline` 不从分母扣除。
- `admin.statistics.get.statistics.avg_station_utilization` 是所有站点
  `utilization` 的算术平均，不按充电桩数量加权；统计接口和站点列表复用同一计算方法。

`admin.station.create` 返回的新站点对象同样包含 `utilization: 0.0` 和
`utilization_range: "7d"`。
