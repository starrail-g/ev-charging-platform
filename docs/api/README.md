# API 文档 / API Reference

本目录记录跨模块接口契约。Socket API 使用
[`docs/architecture/protocol.md`](../architecture/protocol.md) 定义的协议；
示例中的 ID 为整数，金额单位为人民币分，时间为 UTC ISO-8601。

## 当前实现与管理端依赖

服务端当前可运行：`health`、`echo`、`user.login`、`user.profile.get`、
`user.profile.update`、`wallet.recharge`、`station.list`、
`pile.list`、`order.active.get`、`order.history.list`、预约生命周期和充电
开始/停止/结算，以及已合入当前 `main` 的 `admin.login`、统计、管理员桩/站/用户操作。
管理端同时保留 Mock 和已实现的 `SocketAdminRepository`；Socket 适配层已覆盖
登录、概览、站点、全量桩游标聚合、用户和管理动作。Mock 仍是可选的本地回退，
不得被当作真实 Socket 验收证据。

主分支协作约定仍适用：C 端的管理员登录、概览统计和桩状态接口已进入第一阶段
联调与发布证据收集；A/C 可保留 Mock 或离线回退，但不得把 Mock 结果当作真实
Socket 验收证据。B 端用户接口、充电生命周期和当前 `main` 的管理员接口均按下文
v1 契约提供。

每个状态修改请求都必须使用客户端生成的 `id`（1 至 64 字符）。相同操作和
标识性 payload 重放同一 ID 会返回第一次成功响应，即使客户端已重连；同一 ID
用于不同操作或参数会返回 `CONFLICT`（1201）。当前支持幂等的操作为
`reservation.create`、`reservation.confirm`、`reservation.cancel`、
`charging.start`、`charging.stop`、`charging.settle`、
`user.profile.update`、`wallet.recharge`、`admin.station.create`、
`admin.pile.restart`、`admin.user.status.set`。

`admin.login` 成功响应包含 `token` 和 `expires_in_seconds`（当前为 8 小时）。除
`admin.login` 外，所有 `admin.*` 请求都必须在 payload 中携带该 token；只读接口也不例外。
token 在服务端进程内保存，服务重启后失效。建站、重启桩、冻结/解冻请求仍需携带
`administrator_id`，且必须与 token 对应的管理员一致。

地图扩展（`map.station.search`、`map.route.plan`、
`admin.map.audit.list`）已在服务端同时提供确定性 Mock 和生产 Tencent HTTP
运行路径。生产模式从服务端环境变量读取 key，调用腾讯 WebService；完整字段、分页、幂等、缓存、
1 MiB 响应保护和独立云端桩模拟器边界见
[`docs/architecture/map-service-protocol.md`](../architecture/map-service-protocol.md)。

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

管理员接口当前实现：原 admin.* 接口随 PR #12 合入 `main`（`3d015f7`，2026-09-06）；PR #13 已合入当前 `main`（`f3bee707`），并提供 `admin.pile.list` 全量桩查询：

| 接口 | 用途 | 状态 |
|---|---|---|
| `admin.login` | 管理员认证，错误码 1100 | 服务端已实现；成功返回 8 小时有效的随机 `token`，后续所有 `admin.*` 请求携带该 token |
| `admin.statistics.get` | 营收、桩状态、利用率摘要和逐日营收序列 | 服务端已实现，支持 `7d` / `30d`，返回固定长度 `revenue_daily`，必须携带 token |
| `admin.pile.list` | 全部站点（含 inactive）充电桩库存 | 服务端已实现；只读请求携带 token，以 `after_id`/`limit` 游标分页返回 `piles` 与可选 `next_after_id` |
| `admin.pile.restart` | 桩重启和审计 | 服务端已实现；仅故障/离线桩恢复为空闲并按请求 ID 幂等，idle/reserved/charging 返回冲突且不打断会话 |
| `admin.station.list/create` | 站点查询/创建 | 服务端已实现；必须携带 token，创建为超级管理员操作并按请求 ID 幂等 |
| `admin.user.list/status.set` | 用户查询、冻结/解冻 | 服务端已实现；必须携带 token，状态修改为超级管理员操作并按请求 ID 幂等 |

## 管理端 AdminRepository 契约与 wire 映射

管理端 UI（`apps/admin-client`）通过 `AdminRepository` 抽象访问数据；当前同时提供
`MockAdminRepository` 和 `SocketAdminRepository`。接口语义是**管理端业务视图**，与 wire 协议
**非一对一**，映射关系如下（Socket 实现方案 2026-09-03 设计评审定稿，实现随 9/6
Socket 任务落地）：

| AdminRepository 方法 | 业务视图语义 | wire 映射策略 |
|---|---|---|
| `fetchOverview` | 概览指标（7 日/30 日营收、桩五态、利用率、快照时间） | 分别请求 `admin.statistics.get` 的 `7d` 与 `30d`；聚合卡片读取 `revenue_cents`（7d 主体 + 30d 取聚合填副行），趋势图读取 `statistics.revenue_daily[*].revenue_cents` |
| `fetchStations` | 管理端全量站点（含桩数/在线率/7 日利用率聚合视图） | `admin.station.list` |
| `fetchUsers` | 管理端全量用户（含注册时间和活动订单状态） | `admin.user.list` |
| `fetchPiles` | 管理端**全量**桩列表（全部站点含 inactive 站桩；桩页过滤/搜索在本端完成） | 顺序请求 `admin.pile.list` 游标页并在适配层聚合（口径 A，服务端已实现） |

`fetchPiles` 的已知权衡（对应 9/3 评审 Q2 协议缺口；2026-09-07 评审口径 A 重开：
管理员应查看**全部站点**（含 inactive）下的桩——`pile.list` 仅允许查 active 站，
逐站 fan-out 会漏停运站桩并使概览/站页/桩页数字口径分裂）：

- **契约**：`admin.pile.list`——读类鉴权（payload 有 `token`，可选
  `after_id` 和 `limit`，无 `administrator_id`）；成功响应
  `admin.pile.list.result {piles:[…], next_after_id?}`，行结构同
  `pile.list`（readPile 11 列）；范围 = 全部站点（含 inactive 站）的桩，与
  `admin.statistics.get` 的全库桩计数、`admin.station.list` 的站级聚合同口径。
- **分页与帧限制**：`after_id` 为最后已消费桩 ID（首请求 `0`），`limit` 默认
  `100`、最大 `250`；有后续页时服务端回 `next_after_id`。服务端按完整 JSON
  envelope 的实际字节数截页，绝不发送超过协议 1 MiB 上限的成功帧；单条遗留数据
  无法装入一帧时返回受控 `1500`，不会发送客户端必然拒绝的坏帧。
- **失败语义**：整页 error（管理端需要一致的全量视图，不允许静默缺站），UI 进入
  error 态可重试；查询类请求无幂等限制，可原样重试。
- **当前实现**：服务端已提供 `admin.pile.list` 游标页；`SocketAdminRepository`
  顺序聚合页面后才向 UI 返回完整列表，范围覆盖全部站点（含 inactive）。
- **已移除**：原 D5 逐站 fan-out（`admin.station.list` → 每站 `pile.list(station_id)`
  并行聚合）及其 active 站过滤逻辑，见 `a7706b3` 后续 commit。

接口变更说明：三个 `fetch*` 纯虚方法自 PR #6（2026-09-02 合并）起即为
`AdminRepository` 契约的一部分；当前仓库同时提供 `MockAdminRepository` 和
`SocketAdminRepository`，两者均遵循本契约。

接口闸门通过前，管理端 Mock 数据不得冒充真实 Socket 联调结果。

## 管理端 Socket 对接层 Q1–Q7 冻结对账（2026-09-05 冻结，2026-09-06 随 PR #12 更新）

> 设计稿：`superpowers/plans/2026-09-03-socket-admin-repository-design.md`（D1–D8）。
> Q1–Q7 待冻结输入于 9/4 需求评审提出，B 在 PR #10 分支以代码、测试（server/tests/admin.py）
> 与文档冻结（1f157de revenue_daily / 11702ae+4eb0bad 利用率 / 45627d5 restart 语义），
> 随后经 PR #12 合入 `main`（`3d015f7`，2026-09-06）并在合并中把鉴权升级为 token 会话
> （600c657）。C 侧 Socket 适配层按 B 实际构造点（loginAdministrator/readPile/
> listAdminUsers/listAdminStations/getStatistics）逐字段核对并同步。

| # | 冻结输入 | 冻结结论 | 状态 |
|---|---|---|---|
| Q1 | login/statistics/station.list/user.list 响应字段清单（snake_case） | B 实现即样例：admin{id,username,role,status}；statistics 信封样例见下方「管理端统计响应」节；station/user 键与 schema 列直出 | 已冻结（构造点逐字段核对全命中） |
| Q2 | 管理端全量桩列表：`admin.pile.list` 增补 or 逐站聚合 | **2026-09-07 评审口径 A**：管理员视图 = 全部站点（含 inactive）桩；`admin.pile.list` 读类携带 token，以 `after_id`/`limit` 游标分页，响应行结构同 `pile.list`，服务端查询全量 `charging_piles` 并遵守 1 MiB 帧上限。 | 已实现并由 `server/tests/admin.py` 覆盖分页、inactive 站桩和超大遗留行 |
| Q3 | statistics 对象覆盖 | `revenue_daily` 固定长度（7d→7/30d→30 条）、UTC 日历日升序补零、行内键 date/revenue_cents/completed_order_count/energy_wh、聚合=序列和、updated_at 同快照；**无独立 30d 合计键** → C fetchOverview 双请求（7d 主体 + 30d 取聚合值） | 已冻结（1f157de + B 文档样例） |
| Q4 | 利用率口径 | 最近 7 个 UTC 自然日**时间加权占用率**：分子=charging/pending_settlement/completed 订单区间∩窗口（开放单截到 updated_at），分母=桩自 max(窗口起点, created_at) 可用时长（fault/offline 不扣）；站均=简单平均、与 range 无关；站行带 utilization+utilization_range="7d"。语义与演示 0.42 占位不同（socketparse 注释已同步） | 已冻结（11702ae/4eb0bad） |
| Q5 | range 仅 `7d`/`30d`（A-02 裁剪） | B 仅实现 7d/30d、无 today/month/all；Web 大屏搁置，A-02 收敛为 Qt 概览卡（7d 主卡 + 30d 副行） | 已确认（实现即口径） |
| Q6 | admin.* 鉴权机制 | **PR #12 终稿（覆盖 9/5 v1 冻结）**：`admin.login` 发放进程内 8h 随机 `token`；除 login 外所有 `admin.*` 请求携带 token；mutation（station.create/pile.restart/user.status.set）额外携带 `administrator_id` 且必须与 token 主体一致；token 缺失/过期/不匹配 → 1100 UNAUTHORIZED，客户端收到 1100 清除本地认证状态 | 已冻结（PR #12/main `3d015f7` 代码实证） |
| Q7 | 桩 total_charge_count/seconds、用户 created_at、站聚合字段 | readPile 11 列全含（含 restart_count/last_restart_at）、user.list 含 created_at/active_order_status、站行含 pile 五态计数+utilization | 已冻结（构造点核对） |

C 侧代码落点（2026-09-06 token 适配，已推送 `ec09270`）：login 响应校验并提取 token（LoginResult.token）；
buildPayload 对 admin.*（除 login）附加 token、mutation 附加 administrator_id（登录缓存
admin.id）；收到 1100 清除本地 token/认证状态；fetchOverview 双请求合并（任一失败整页
error，同 D5 fan-out 哲学）。restart 语义（45627d5）与 C Mock/UI 逐字一致（仅 fault/offline
可重启、其余 1201），无代码改动。

9/7 18:00 闸门说明：B 的 admin.* handler 已随 PR #12 合入 `main`（2026-09-06），管理端
token 适配已完成并推送（`ec09270`），且**通过真实 main 服务端联调冒烟**（2026-09-06：
首轮 login→overview 双 range→逐站 fan-out→冻结/解冻 PASS，restart 因当时库内无 fault
桩跳过；补跑对 A-03/B-02 真跑 `admin.pile.restart` PASS，`request_records` 留痕）。
证据文件在**仓库外** `D:/work/chargingplatform/build/repro/`（不入 git；逐字记录见
`docs/requirements/current.md` §2）；闸门以登录/概览/桩状态/动作为准，材料不冒充真实联调。

### 管理端统计响应

`admin.statistics.get` 的 `range` 决定逐日序列长度。日期按 UTC 日历日计算，
从最早日升序排列到当前 UTC 日；没有已完成订单的日期也会返回 0，避免客户端
因缺失日期错位绘图。`revenue_cents`、`completed_order_count` 和 `energy_wh`
分别等于 `revenue_daily` 对应字段之和。响应固定携带 `has_data`
（= 已完成订单数 > 0 或桩总数 > 0）：空库为 `false`，客户端据此展示
「暂无概览数据」空态而非 0 值指标页（2026-09-07 评审对齐，C socketparse
已解析并映射 `OverviewResult.hasData`；键缺失按 true 处理并告警）：

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
      "updated_at": "2026-09-05T03:20:00Z",
      "has_data": true
    }
  }
}
```

上例省略了请求帧；实际请求 payload 至少为
`{"token":"<admin.login 返回的 token>","range":"7d"}`。

示例仅展示序列中间字段；实际 `7d` 响应包含 7 条、`30d` 响应包含 30 条。

### C 侧映射：`revenue_daily` → `RevenueSeries`（2026-09-08，feature/admin-revenue 本地产物）

管理端营收图表（概览 `RevenueMetricCard` 与销售业绩页 `RevenuePage`）把每个 range 响应的
`revenue_daily` **整条保留**到模型 `RevenueSeries`（`adminmodels.h`：range/days/totalCents/updatedAt/available），
wire 协议**零改动**——仍是 B 冻结的 `admin.statistics.get` 7d/30d 样例：

- `SocketAdminRepository::fetchOverview` 对 `7d` 与 `30d` 各发一次请求，两响应**各自**保留完整逐日序列与各自
  `updated_at`；两次统计的快照时间可能不同，客户端**不宣称同一快照**（概览/销售页分别显示选中范围的更新时间；
  摘要口径仍以 7d 主体为准）。`30d` 聚合合计仍取 30d 响应 `revenue_cents`（= 30 条序列和）填 `revenue30dCents`。
- 严格解析（`socketparse::parseRevenueSeries`，图表专用，摘要路径不受影响）：range 回声必须等于请求值；条数恰为
  7/30；`date` 为 UTC 日历日、连续升序且末日 == `updated_at` 当日；金额为非负整数且 ≤ 2^53-1；逐日之和 ==
  `revenue_cents` 合计。**任一不符 → 该序列 `available=false`**（营收卡显示「趋势暂不可用」与重试入口），
  不补 0、不静默丢弃；序列坏只影响营收区，不清其它摘要。
- 序列合法但全零 = 正常零营收（`has_data` 语义不变，空库仍由 7d 主体 `has_data=false` 表达）。

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
