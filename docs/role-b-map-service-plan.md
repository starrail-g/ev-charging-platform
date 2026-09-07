# B 端地图服务与独立云端桩模拟器开发规划

## 1. 目标和边界

B 端负责 `server`、`libs/protocol`、`libs/database` 和 `database`。本规划
实现 [`map-service-protocol.md`](architecture/map-service-protocol.md) 定义的
服务端地图、站点导入、缓存审计、模拟器网关和 Schema v0.4。

B 不负责 Qt 地图页面或管理端页面；A 负责用户端地图适配，C 可选接入地图
审计列表。B 服务器是业务状态唯一写入者，独立云端 `pile-simulator` 只提交
经过认证的模拟建议，不能直接打开 SQLite。

## 2. 交付阶段

### B0：契约和基线冻结

输出：协议/数据库文档、字段表、错误码表和测试矩阵。

- 将地图操作加入 `docs/architecture/protocol.md`，并保留当前
  `admin.pile.list` 的分页和 1 MiB 规则。
- 在 `libs/protocol` 预留 1400–1410 错误码及名称映射。
- 固定 request fingerprint、分页 token、cache key、warning 结构和状态源。
- 形成 v0.3 → v0.4 数据迁移清单。

验收：文档与所有现有 operation/status/error 字段无冲突，旧客户端仍能
忽略新增字段。

### B1：Protocol 和统一响应保护

范围：`libs/protocol`、server response helper。

- 增加地图错误码、名称和 retryable 规则。
- 统一 compact JSON envelope size guard。
- 为可分页响应提供 page token 编解码和非法 token 错误。
- 保证超大单项返回 1409，不发送非法帧。

验收：qmake6 构建；协议单测覆盖 0/1 MiB/超 1 MiB、错误码、token 和
完整 envelope 大小。

### B2：Schema v0.4 和 Database API

范围：`database/schema`、`database/migrations`、`libs/database`。

- 增加站点 provider 身份和同步字段。
- 增加桩 simulation/source/timestamp 字段。
- 建立 map request/upstream/cache、pile event、simulation state 表。
- 增加索引、CHECK、外键和 30 天清理 SQL/API。
- 实现 map request、cache、audit、pile generation、simulation gateway 所需
  的数据库方法。
- 迁移失败必须整体回滚；现有 v0.3 站点和桩不可丢失。

验收：新库初始化、v0.3 升级、重复升级、坏数据回滚、foreign-key check、
清理任务和并发写测试全部通过。

### B3：腾讯地图适配和缓存

范围：server map service。

- 实现 geocoder、POI search/detail、driving/walking 调用。
- 实现坐标、字段、折线和上游状态校验。
- 实现 live/cache/stale/mock 四种数据源。
- 按规范化 origin、radius、page size、page token、station、mode 生成 cache
  key。
- 实现同 key 的并发 miss 合并、TTL、stale 上限和缓存清理。
- 所有上游调用记录脱敏审计，不记录 Key、完整 URL 或原始 JSON。

验收：使用 fake Tencent adapter 覆盖成功、超时、配额、权限、无结果、坏
JSON、缓存命中、过期和 stale 降级；无真实 Key 也能运行测试。

### B4：站点 upsert、幂等和桩生成

- 实现 `map.station.search` 的地址/坐标规范化和分页。
- 以 `(provider, provider_poi_id)` 去重站点。
- 首次站点导入与桩生成使用一个 `BEGIN IMMEDIATE` 事务。
- 实现 SHA-256 + PCG32 的确定性生成规则。
- 将地图审计记录和 `request_records` 区分开：前者审计每次请求，后者只
  保存带副作用搜索的成功响应重放。
- 相同 request ID/fingerprint 必须原样重放，不得重复写站点或桩。

验收：重复 POI、重复 request ID、参数冲突、首次导入失败、桩生成失败、
种子复现和大响应分页测试。

### B5：路线和管理员审计 handler

- 实现 `map.route.plan`，终点只从数据库读取。
- 实现路线缓存和折线抽稀；逐步缩减到完整 envelope 可发送。
- 实现 `admin.map.audit.list` 的 Token 鉴权、过滤、分页和稳定排序。
- 所有 handler 都在 Socket event loop 外执行慢 HTTP/数据库工作，并通过
  request ID 回传结果。

验收：客户端断开、请求超时、重复只读请求、管理员 Token 失效、audit 分页、
路线过大和并发请求测试。

### B6：独立云端 `pile-simulator`

建议部署为独立无状态服务，配套一个小型持久化运行状态存储或从服务端
恢复 tick/seed；它不连接业务 SQLite。

模拟器职责：

- 按固定 seed、tick 和 pile ID 生成 deterministic proposal；
- 维护调度、心跳、重试和服务健康状态；
- 提交 `simulator_id + tick_id + expected_versions + changes`；
- 对 rejected/stale proposal 使用同一 tick ID 重试，不能换 ID 造成重复。

B server gateway 职责：

- mTLS/service identity 鉴权和权限校验；
- 校验 seed、tick、snapshot version、pile 当前状态、订单占用和最小 idle；
- 在一个 SQLite 事务内更新桩、快照版本和 `pile_status_events`；
- 对重复 tick 返回原结果，对版本冲突返回 structured conflict；
- 模拟器离线时不影响预约、充电、结算和管理员重启。

云端验收：

- 同 seed/tick/输入得到相同 proposal；
- 重放同一 tick 不重复修改数据库；
- 业务事务抢先更新时，旧 proposal 被拒绝；
- 网络中断、服务重启、重复投递和时钟漂移不会破坏桩状态；
- 未授权服务不能调用模拟 gateway；
- simulator heartbeat、last-seen、失败率和延迟可观测。

### B7：端到端集成和交付

- 与 A 对齐 `map.station.search`、`map.route.plan` 字段和降级展示；
- 与 C 对齐 `admin.pile.list` 单桩字段和可选 `admin.map.audit.list`；
- 运行 server、database、protocol 的 qmake6 构建和测试；
- 补充真实服务器 smoke、并发、迁移和超大响应回归；
- 更新 `current.md`、架构文档和发布检查清单。

## 3. 建议代码边界

```text
server/src/map/
  map_service.*          # 用例编排、缓存、上游错误映射
  tencent_client.*       # HTTP/Tencent DTO 和解析
  map_cache.*            # 规范化 key、TTL、并发合并
  map_audit.*             # request/upstream audit
  pile_generator.*       # SHA-256 + PCG32
  simulation_gateway.*   # 内部 simulator proposal 验证

libs/database/
  map_repository.*
  pile_simulation_repository.*

database/migrations/
  003_v0.3_to_v0.4.sql

services/pile-simulator/
  scheduler / deterministic planner / internal client / health metrics
```

实际目录若与现有工程约定不同，应保持单一实现，不在 Qt 客户端复制服务端
逻辑。

## 4. 风险和暂停条件

出现以下情况时不得进入客户端联调：

- v0.4 迁移不能回滚；
- 新响应没有完整 envelope 大小保护；
- map search 重试仍可能重复建站或建桩；
- simulator 可以绕过 server 直接修改业务数据库；
- `reserved`/`charging` 可被模拟器覆盖；
- warning、error code 或分页字段在 A/B/C 之间不一致；
- 腾讯 Key、原始响应或精确长期位置进入仓库、数据库或日志。
