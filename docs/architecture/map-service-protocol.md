# 服务端腾讯地图与充电桩模拟协议（Protocol v1 扩展）

> 状态：已确认的接口契约，服务端实现、数据库迁移和用户端适配尚未完成。
> 适用范围：Socket Protocol v1 的向后兼容扩展。

## 1. 目标与边界

本协议将腾讯地图 WebService 调用统一收口到服务端：

```text
Qt 用户端
    | Socket Protocol v1
    v
服务端地图模块
    |-- 缓存/脱敏审计 --> SQLite
    |-- 腾讯 WebService --> 地址、POI、路线
    `-- 桩模拟器 --> 稳定生成的充电桩与安全状态变化
```

- 腾讯地图 Key 只存在于服务端进程环境或被 Git 忽略的服务端本地配置。
- 用户端不得直接调用腾讯 WebService，也不得加载需要腾讯 Key 的 GL JavaScript。
- 用户端只接收服务端返回的站点、坐标、距离、路线摘要和折线，并在本地 Qt/HTML 页面绘制。
- 腾讯 POI 只提供位置数据，不提供可信的桩数量、价格、功率、状态或订单数据。
- 充电桩由服务端首次导入站点时稳定生成并持久化；后续查询不得重复生成。
- `reserved` 和 `charging` 只由真实预约/充电事务产生，不允许随机模拟。
- 本扩展保留现有四字节大端长度前缀、UTF-8 JSON 信封、1 MiB 上限和错误响应结构。
- PR #13 的 `admin.pile.list` 是管理端全量桩查询；本协议不改变它的操作名或职责。

## 2. 兼容性规则

- 信封版本继续使用 `v: 1`，新增操作的成功响应仍以 `.result` 结尾。
- `station.list` 保持数据库只读查询，不隐式调用腾讯或生成充电桩。
- `pile.list` 保持请求结构，向响应增加快照元数据和可忽略的新字段。
- 未识别新增字段的旧客户端仍可读取原有 `stations`、`piles` 核心字段。
- 地图搜索会持久化站点和首次生成桩，因此纳入请求 ID 幂等保护。
- 路线查询属于可缓存的只读请求，不写入 `request_records`，但必须写脱敏审计记录。
- 当前用户端地图实现可作为字段/错误映射参考，不能继续作为最终腾讯调用边界。

## 3. 公共数据对象

### 3.1 起点 `origin`

地址形式：

```json
{"kind":"address","value":"沈阳市浑南区软件园"}
```

坐标形式：

```json
{"kind":"coordinate","latitude":41.7192,"longitude":123.4315}
```

规则：

- `kind` 只能是 `address` 或 `coordinate`。
- 地址去除首尾空格后长度为 1–200 个字符。
- 纬度范围为 -90 至 90，经度范围为 -180 至 180，且必须是有限数值。
- 同一个对象中不得同时提交地址值和坐标字段。

### 3.2 数据来源

`data_source` 只能是：

- `tencent_live`：本次腾讯调用成功。
- `tencent_cache`：使用未过期缓存。
- `tencent_stale`：腾讯失败后使用不超过七天的旧缓存。
- `server_mock`：服务端显式启用的确定性演示数据。

`tencent_stale` 和 `server_mock` 必须同时返回非空 `warning`，客户端不得展示为实时数据。

### 3.3 警告对象

```json
{
  "code": 1403,
  "name": "MAP_UPSTREAM_UNAVAILABLE",
  "message": "地图服务暂不可用，当前展示缓存结果",
  "retryable": true
}
```

成功且无降级时 `warning` 为 JSON `null`。

## 4. `map.station.search`

根据地址或坐标搜索附近充电站。服务端负责地址解析、腾讯 POI 搜索、站点 upsert、首次桩生成、缓存和审计。

### 4.1 请求

```json
{
  "v": 1,
  "id": "a-map-001",
  "type": "map.station.search",
  "payload": {
    "user_id": 1,
    "origin": {
      "kind": "address",
      "value": "沈阳市浑南区软件园"
    },
    "radius_meters": 1000,
    "page_size": 20
  }
}
```

请求约束：

- `user_id` 必须是正整数且用户存在；冻结用户允许执行只读地图查询。
- `radius_meters` 范围为 10–1000，默认 1000。
- `page_size` 范围为 1–20，默认 20。
- 腾讯搜索关键词由服务端固定为“充电站”，客户端不能传任意关键词。
- 相同请求 ID、操作和识别参数重放时返回原成功响应；更换参数重用 ID 返回 `CONFLICT`。

### 4.2 成功响应

```json
{
  "v": 1,
  "id": "a-map-001",
  "type": "map.station.search.result",
  "payload": {
    "provider": "tencent",
    "data_source": "tencent_live",
    "resolved_origin": {
      "latitude": 41.7192,
      "longitude": 123.4315
    },
    "stations": [
      {
        "id": 42,
        "provider": "tencent",
        "provider_poi_id": "provider-poi-id",
        "name": "示例充电站",
        "address": "沈阳市浑南区示例路1号",
        "latitude": 41.721,
        "longitude": 123.435,
        "distance_meters": 480,
        "status": "active",
        "pile_total": 8,
        "pile_idle": 6,
        "pile_reserved": 0,
        "pile_charging": 0,
        "pile_fault": 1,
        "pile_offline": 1,
        "map_synced_at": "2026-09-07T08:00:00Z",
        "pile_snapshot_version": 3
      }
    ],
    "fetched_at": "2026-09-07T08:00:00Z",
    "expires_at": "2026-09-08T08:00:00Z",
    "request_record_id": 10001,
    "warning": null
  }
}
```

`request_record_id` 指向本扩展计划新增的 `map_request_logs.id`，不指向现有幂等表
`request_records`。

站点合并规则：

- 内部 `stations.id` 是后续 `pile.list`、预约和充电操作使用的唯一业务 ID。
- 腾讯站点使用 `provider=tencent` 与 `provider_poi_id` 唯一定位。
- 已存在 POI 只更新名称、地址、坐标和同步时间，不重新生成桩。
- 一次搜索中缺失某 POI 不得自动停用历史站点；停用仍由管理端业务决定。
- 腾讯字段不得覆盖桩价格、数量、功率、状态或订单信息。

## 5. `map.route.plan`

查询起点到内部业务站点的真实驾车或步行路线。

### 5.1 请求

```json
{
  "v": 1,
  "id": "a-route-001",
  "type": "map.route.plan",
  "payload": {
    "user_id": 1,
    "origin": {
      "kind": "coordinate",
      "latitude": 41.7192,
      "longitude": 123.4315
    },
    "station_id": 42,
    "mode": "driving"
  }
}
```

- `mode` 只能是 `driving` 或 `walking`。
- 客户端只提交 `station_id`，服务端必须从数据库读取终点坐标。
- 不接受客户端同时提交 `station_id` 和自定义终点坐标。
- `station_id` 必须指向有效且具有合法坐标的站点。

### 5.2 成功响应

```json
{
  "v": 1,
  "id": "a-route-001",
  "type": "map.route.plan.result",
  "payload": {
    "provider": "tencent",
    "data_source": "tencent_live",
    "mode": "driving",
    "origin": {
      "latitude": 41.7192,
      "longitude": 123.4315
    },
    "destination": {
      "station_id": 42,
      "name": "示例充电站",
      "address": "沈阳市浑南区示例路1号",
      "latitude": 41.721,
      "longitude": 123.435
    },
    "distance_meters": 1380,
    "duration_seconds": 300,
    "polyline_encoding": "coordinate_pairs",
    "polyline": [
      [41.7192, 123.4315],
      [41.7201, 123.433],
      [41.721, 123.435]
    ],
    "fetched_at": "2026-09-07T08:01:00Z",
    "expires_at": "2026-09-07T08:06:00Z",
    "request_record_id": 10002,
    "warning": null
  }
}
```

路线规则：

- 服务端解码腾讯压缩折线后再返回；每个点均为 `[latitude, longitude]`。
- 距离统一为米，时间统一为秒。
- 单条路线最多返回 4096 个坐标点；超过时按保持首尾和路径形状的规则抽稀。
- 不得返回腾讯 Key、完整腾讯请求 URL 或原始响应 JSON。
- 路线缓存默认五分钟；腾讯失败时可返回仍有效或不超过七天的旧缓存。
- 无可用缓存时返回地图错误，不得把直线距离伪装成腾讯路线。

## 6. `admin.map.audit.list`

管理员使用现有会话 Token 查询脱敏地图调用记录。

### 6.1 请求

```json
{
  "v": 1,
  "id": "c-map-audit-001",
  "type": "admin.map.audit.list",
  "payload": {
    "token": "administrator-session-token",
    "operation": "map.route.plan",
    "result_status": "success",
    "limit": 50
  }
}
```

- `token` 必须满足现有 `admin.*` 鉴权规则。
- `operation` 可省略，允许值为 `map.station.search`、`map.route.plan`。
- `result_status` 可省略，允许值为 `success`、`degraded`、`failed`。
- `limit` 范围为 1–100，默认 50，按 `created_at` 倒序返回。

### 6.2 成功响应

```json
{
  "v": 1,
  "id": "c-map-audit-001",
  "type": "admin.map.audit.list.result",
  "payload": {
    "records": [
      {
        "id": 10002,
        "request_id": "a-route-001",
        "user_id": 1,
        "operation": "map.route.plan",
        "provider": "tencent",
        "station_id": 42,
        "route_mode": "driving",
        "result_status": "success",
        "http_status": 200,
        "upstream_status": 0,
        "latency_ms": 215,
        "cache_hit": false,
        "result_count": 1,
        "created_at": "2026-09-07T08:01:00Z"
      }
    ]
  }
}
```

## 7. `pile.list` 向后兼容扩展

请求保持不变：

```json
{"v":1,"id":"pile-list-001","type":"pile.list","payload":{"station_id":42}}
```

扩展后的响应：

```json
{
  "v": 1,
  "id": "pile-list-001",
  "type": "pile.list.result",
  "payload": {
    "station_id": 42,
    "snapshot_version": 7,
    "generated_at": "2026-09-07T08:02:00Z",
    "refresh_after_seconds": 60,
    "piles": [
      {
        "id": 4201,
        "station_id": 42,
        "pile_code": "M42-01",
        "pile_type": "fast",
        "power_kw": 120,
        "unit_price_cents_per_kwh": 130,
        "status": "idle",
        "total_charge_count": 0,
        "total_charge_seconds": 0,
        "restart_count": 0,
        "last_restart_at": null,
        "simulated": true,
        "status_source": "simulation",
        "status_updated_at": "2026-09-07T08:02:00Z"
      }
    ]
  }
}
```

`status_source` 只能是 `seed`、`simulation`、`business` 或 `administrator`。客户端按 `refresh_after_seconds` 轮询；本扩展不新增服务端主动推送帧。

PR #13 的 `admin.pile.list` 返回相同的单桩核心字段，并可增加同样的 `simulated`、`status_source`、`status_updated_at`；它负责跨站全量查询，不负责触发腾讯同步或模拟状态更新。

## 8. 充电桩稳定生成规则

腾讯 POI 首次写入内部站点后，在同一数据库事务中生成充电桩：

- 每站生成 4–12 个桩。
- 随机源由服务端固定种子与 `provider:provider_poi_id` 共同派生，结果必须可复现。
- 同一站点已经存在桩时禁止重新生成；修改种子不改变已持久化数据。
- 快充约占 60%，慢充约占 40%，并保证至少各有一个。
- 快充功率从 60、120、180 kW 中选择；慢充从 7、11、22 kW 中选择。
- 站点基础价格从 90、110、130 分/度中选择，快充在基础价格上增加 20 分/度。
- 初始状态只允许 `idle`、`fault`、`offline`，权重分别为 80%、10%、10%。
- 默认保证活动站点至少有一个 `idle` 桩。
- 桩编号格式为 `M<station_id>-<两位序号>`，并继续满足站内唯一约束。

首次生成与站点 upsert 任一失败时必须整体回滚，不得留下无桩站点或重复桩。

## 9. 安全状态模拟规则

服务端默认每 60 秒运行一次模拟事务：

| 当前状态 | 允许的模拟结果 |
|---|---|
| `idle` | `idle`、`fault`、`offline` |
| `fault` | `fault`、`idle`、`offline` |
| `offline` | `offline`、`idle`、`fault` |
| `reserved` | 禁止模拟，只能由预约事务更新 |
| `charging` | 禁止模拟，只能由充电事务更新 |

强制约束：

- 仅 `simulated=true` 且没有活动订单的桩参与模拟。
- 关联任何未完成订单（`pending_reservation`、`reserved`、`charging` 或
  `pending_settlement`）的桩必须跳过。虽然 v0.3 在停止充电后释放桩，本模拟契约采用
  更保守的隔离规则，待订单完成结算后才允许该桩重新参与模拟。
- 模拟器不得把一个活动站点的最后一个空闲桩变为不可用；该下限可由配置关闭。
- 预约、充电、取消、停止充电和管理员重启的事务优先于模拟器。
- 每批变化使用一个 SQLite 写事务；失败时全部回滚且不递增快照版本。
- 有实际变化时，站点快照版本只递增一次，每个变化分别写 `pile_status_events`。
- 固定测试种子必须产生相同初始桩和相同状态变化序列。

## 10. 错误码扩展

| Code | Name | 含义 | 默认可重试 |
|---:|---|---|---|
| 1400 | `MAP_DISABLED` | 服务端地图功能关闭 | 否 |
| 1401 | `MAP_NOT_CONFIGURED` | 服务端没有有效 Key | 否 |
| 1402 | `MAP_UPSTREAM_TIMEOUT` | 腾讯调用超时 | 是 |
| 1403 | `MAP_UPSTREAM_UNAVAILABLE` | 腾讯或外部网络暂不可用 | 是 |
| 1404 | `MAP_QUOTA_EXCEEDED` | 腾讯调用额度不足 | 否 |
| 1405 | `MAP_PERMISSION_DENIED` | Key 权限或来源限制不满足 | 否 |
| 1406 | `MAP_NO_RESULT` | 地址、POI 或路线没有结果 | 是 |
| 1407 | `MAP_RESPONSE_INVALID` | 腾讯响应字段、坐标或路线无效 | 是 |
| 1408 | `MAP_RATE_LIMITED` | 客户端请求过于频繁 | 是 |

错误响应允许增加兼容字段：

```json
{
  "v": 1,
  "id": "a-map-001",
  "type": "error",
  "payload": {
    "code": 1404,
    "name": "MAP_QUOTA_EXCEEDED",
    "message": "地图服务调用额度不足",
    "retryable": false,
    "degraded": false,
    "request_record_id": 10001
  }
}
```

客户端只能按 `code` 分支。服务端不得把 SQL、Key、文件路径、完整 URL、原始腾讯消息或堆栈放入响应。

## 11. 持久化与保留契约

实现阶段计划升级到 Schema v0.4；本协议 PR 不修改当前 Schema v0.3，也不声明迁移已完成。

计划增加：

- `stations`：`provider`、`provider_poi_id`、`map_synced_at`、内容摘要；`provider + provider_poi_id` 唯一。
- `charging_piles`：`simulated`、`status_source`、`status_updated_at`。
- `map_request_logs`：每个客户端地图请求的脱敏结果。
- `map_upstream_call_logs`：geocoder、place search、place detail、driving、walking 的独立调用结果。
- `map_cache_entries`：解析后的站点和路线缓存。
- `pile_status_events`：随机、业务和管理员状态变化记录。
- `simulation_state`：模拟种子标识、最后执行时间和站点快照版本。

保留规则：

- 地图请求、上游调用和桩状态事件默认保留 30 天，按 UTC 清理。
- 审计坐标最多保留四位小数；业务响应可保留路线显示所需精度。
- 不持久化腾讯 Key、含 Key URL、认证信息、原始腾讯 JSON 或用户精确位置的长期历史。
- 当前站点和桩业务数据不随 30 天审计清理任务删除。

## 12. 缓存与服务端配置

```text
TENCENT_MAP_KEY=<仅服务端本地配置>
TENCENT_MAP_ENABLED=1
EV_MAP_STATION_CACHE_TTL_SECONDS=86400
EV_MAP_ROUTE_CACHE_TTL_SECONDS=300
EV_MAP_STALE_MAX_SECONDS=604800
EV_PILE_SIMULATION_ENABLED=1
EV_PILE_SIMULATION_INTERVAL_SECONDS=60
EV_PILE_SIMULATION_SEED=<服务端本地固定值>
EV_PILE_SIMULATION_MIN_IDLE=1
```

- 地址/站点缓存默认 24 小时，路线缓存默认 5 分钟，旧缓存最长 7 天。
- 服务端腾讯请求必须异步执行，不得阻塞 Socket 事件循环。
- 相同区域的并发缓存未命中请求应合并，避免并发消耗配额。
- 客户端不得获得 `TENCENT_MAP_KEY` 或模拟种子。

## 13. 验收要求

- 地址和坐标两种起点均能搜索腾讯充电站。
- POI 首次导入、重复同步、站点去重和首次桩生成具有事务测试。
- 相同 POI 和相同请求 ID 不会创建重复站点或重复桩。
- 固定种子可复现桩数量、类型、功率、价格和状态序列。
- 搜索返回的内部站点 ID 可直接用于 `pile.list`、预约和充电。
- 驾车、步行使用不同腾讯端点，距离、秒数和折线坐标顺序正确。
- 路线终点只能来自服务端内部站点。
- 覆盖缓存命中、过期、旧缓存降级、Mock 降级和无缓存失败。
- 覆盖配额、权限、超时、无结果、非法坐标和异常 JSON。
- 状态模拟不能改变活动订单占用的桩，也不能随机产生 `reserved`/`charging`。
- `pile.list` 与 PR #13 的 `admin.pile.list` 使用一致的单桩核心字段。
- 地图请求与状态变化均生成脱敏记录，30 天清理不影响业务站点和桩。
- Key、完整 URL、原始响应、运行数据库和日志不得进入 Git。
- 服务端、数据库和客户端后续实现及验收继续只使用 qmake6。

## 14. 后续实现顺序

1. B 完成 Schema v0.4 迁移和数据访问接口。
2. B 完成服务端腾讯适配、缓存、审计和错误映射。
3. B 完成稳定桩生成器与安全状态模拟器。
4. B 完成协议、数据库、并发和 TCP 端到端测试。
5. A 将用户端地图调用替换为 `map.station.search` 与 `map.route.plan`。
6. C 按需在管理端接入 `admin.map.audit.list`，并保持 `admin.pile.list` 兼容。
