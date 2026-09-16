# 分析大屏 API 契约（analytics api）

> T0 契约冻结文档（2026-09-15 定稿）。配套：`docs/architecture/analytics.md`（数仓契约）、
> 手册第 5 节（响应结构约定）。实现：`analytics/api/`。所有响应 `Content-Type: application/json; charset=utf-8`。

## 1. 路由总览

| 路由 | 说明 |
|---|---|
| `GET /api/health` | 进程可用性 + `active_batch` + `snapshot_ready`；**不启动 Spark**、不读大表 |
| `GET /api/dashboard?start=<date>&end=<date>&station_id=<int>` | 大屏数据；`start` 含、`end` 不含，UTC 日期；窗口 ≤ 90 天；`station_id` 省略 = 全部 |
| `GET /api/quality?batch_id=<id>` | 已发布批次质量报告（完整清洗口径，不按屏幕站点过滤） |
| `GET /` 与静态资源 | 大屏页面（`dashboard/` 资产），默认拓扑地图；兼容 runtime config 路由 |

同一响应内所有数据来自**同一个 batch_id**（同一快照）；`latest` 切换期间已开始的请求保持旧批次，不混批。

## 2. 响应包络（P0）

```json
{
  "status": "ok",
  "meta": {
    "batch_id": "s2-standard-20260915",
    "source_type": "synthetic_warehouse",
    "timezone": "UTC",
    "coverage": "complete",
    "data_start": "2026-06-16T00:00:00Z",
    "data_end_exclusive": "2026-09-14T00:00:00Z",
    "available_start": "2026-06-16T00:00:00Z",
    "available_end_exclusive": "2026-09-15T00:00:00Z",
    "generated_at": "2026-09-15T08:00:00Z",
    "is_realtime": false,
    "stale": false
  },
  "query": {"start": "2026-09-07", "end": "2026-09-14", "station_id": null},
  "data": {
    "overview": {"revenueCents": 1901, "completedOrders": 2, "energyKwh": 15.001},
    "stations": [
      {"id": 1, "name": "示例充电站", "address": "...", "latitude": 0.0, "longitude": 0.0,
       "status": "active", "pileCount": 6, "pileCounts": {"idle": 1, "reserved": 1, "charging": 2, "fault": 1, "offline": 1}}
    ],
    "piles": [
      {"id": 1, "stationId": 1, "code": "P-01-A", "type": "fast", "powerKw": 120.0,
       "unitPriceCentsPerKwh": 120, "status": "idle", "totalChargeCount": 10, "totalChargeSeconds": 36000}
    ],
    "stationUtilization": [{"stationId": 1, "utilization": 0.031}],
    "revenueDaily": [{"date": "2026-09-01", "revenueCents": 1901, "completedOrders": 2, "energyWh": 15001}],
    "loadHourly": [{"hourStart": "2026-09-01T00:00:00Z", "loadKw": 5.0, "allocatedWh": 5000, "chargeSeconds": 1800}],
    "quality": {
      "inputRows": 7, "keptRows": 3, "duplicateRows": 1, "quarantineRows": 3,
      "byTable": {"charging_orders": {"input": 7, "kept": 3, "duplicate": 1, "quarantine": 3, "repaired": 0}}
    }
  }
}
```

- 上述 `data.*` 字段结构为契约；数值为 2026-09-01 窗口样例（来自 fixture A 手算基准，非生产结果）。
- **窗口三类口径（2026-09-15 起）**：`data_start/data_end_exclusive` = 生成窗口（manifest 原值）；
  `available_start/available_end_exclusive` = **发布覆盖窗口**（生成窗口 ∪ 末端结算追加日 + 1 天，右开）——
  API 的覆盖校验与 `error.available_range` 以后者为准；**默认查询窗口** = 覆盖窗口中「最新 ≤90 天」
  的一段（响应 `meta.default_start/default_end_exclusive` 明示；覆盖本身不超 90 天时即全覆盖窗口）。
  数仓按设计保留 `settled_at ≥ 生成窗口末端` 的结算行（不静默丢收入），若只暴露生成窗口会出现
  “收入进了 overview、对应结算日却查不到”的缺口；旧批次缺 `available_*` 时回退生成窗口（行为不变）。
- `revenueDaily` / `loadHourly` 覆盖**完整查询窗口**（只在 manifest 证明窗口完整时补零；批次缺失不出行）；
  `loadHourly` 带完整 UTC 时间戳，不假设只有 0–23 时。
- 金额一律整数分（`revenueCents`）；电量 `revenueDaily.energyWh` 为整数 Wh，`overview.energyKwh` 为 k kWh 展示值
  （`energyWh/1000`，保留 3 位小数）。
- `stations[].pileCounts` 为**批次快照**时刻的状态计数，不得标注"实时"。

## 3. 参数与错误语义

| 情况 | 行为 |
|---|---|
| 日期非法（非 `YYYY-MM-DD`）/ `start >= end` / 窗口 > 90 天 | **400**，结构化错误（见下） |
| `station_id` 非正整数 | 400 |
| `batch_id` 非法（绝对路径 / 路径分隔符 / `..` / 越出 `published/` 目录） | **400**，`error.code=invalid_batch_id`（读取范围固定为 `published/<batch>/quality.json`，拒绝越界） |
| `station_id` 合法但不存在 / 窗口内无业务记录 | **200 + `"status":"empty"`**（`data` 为完整窗口的空结构） |
| 窗口超出**发布覆盖窗口**（`available_*`） | **400**，`error.available_range` 给出可用范围，不悄悄补零 |
| 无成功快照 / 快照文件损坏 / JSON 合法但内部结构坏（容器或记录字段类型不合法） | **503**，`error.code=snapshot_corrupt`（统一结构化错误，不得 HTML 500；`empty`/`503` 均**不得**回退 demo 数据伪装成功） |
| 批次旧但可读 | 200，`meta.stale=true`（过期判定取配置），页面显示数据日期 |

错误结构：

```json
{"status": "error", "error": {"code": "invalid_window", "message": "start must be before end",
 "available_range": {"start": "2026-06-16", "end": "2026-09-15"}}}
```

`code` 枚举：`invalid_date`、`invalid_window`、`window_too_large`、`invalid_station_id`、`invalid_batch_id`、
`out_of_coverage`、`snapshot_missing`、`snapshot_corrupt`。

## 4. 前端映射（dashboard/js/api-data-provider.js）

- `overview.revenueCents` → 直接接入视图模型 `overview.revenueCents`；
- `revenueDaily` → 趋势数组（按日期，**不假定永远 7 条**；30d 视图 = 请求 30 天窗口）；
- `loadHourly` → 负荷图（完整时间戳）；
- `stations/piles` → 与现有 demo 视图模型同形（camelCase，字段见上）；`stationUtilization` → 站级利用率；
- `adaptDashboardData` 扩展为按 `source_type` 校验：`synthetic_warehouse` 数据必须携带
  `meta.batch_id/generated_at/coverage`；`demo_fixture` 保持原演示路径；
- `meta.available_start/available_end_exclusive` → 筛选控件 min/max（可查询覆盖窗口）；
  `meta.default_start/default_end_exclusive` → 初始值与“重置”的默认窗口（覆盖超 90 天时收敛到
  最新一段，保证“重置→应用”永远合法）；旧批次缺省逐级回退 `data_start/data_end_exclusive`；
  **空结果响应同样先应用覆盖窗口**，否则分享链接首开命中空结果后“重置”无法恢复全窗口；
- 无预测时隐藏预测区域；"实时"字样一律禁止（`is_realtime:false`）。

## 5. 数据源与降级标注（屏幕必须可见）

| source_type | 页面标注 |
|---|---|
| `synthetic_warehouse` | "分析批次：<batch_id>（模拟数据，UTC，数据截至 <generated_at>）" |
| `demo_fixture` | "演示数据（demo_fixture），非分析产物" |
| API 失败 / 503 | 显示失败态 + 可重试；**不回退演示数据** |
| `meta.stale=true` | 显示数据日期 + "批次可能过期" |

## 6. 启动与验收入口（实现后）

```bash
# VM（分析产物所在机）
python3 -m flask --app 'analytics.api.app:create_app()' run --host 127.0.0.1 --port 61469
curl --fail 'http://127.0.0.1:61469/api/health'
curl --fail 'http://127.0.0.1:61469/api/dashboard?start=2026-09-07&end=2026-09-14'

# 测试
python3 -m unittest discover -s analytics/tests -p 'test_api*.py' -v
```

- Flask 提供静态大屏资产（`dashboard/`）与 runtime config；**不得**把 `serve.py` 的地图 WebService 凭据搬进浏览器配置；
- 外网地图不是离线链路依赖（本轮默认拓扑地图）；
- 刷新页面只触发本地 JSON 读取，**不启动 Spark 批处理**。

## 7. G3 工作台与独立 ML 服务（2026-09-16）

`GET /api/dashboard` 增加 `data.analytics`，由同一发布批次的 ADS 聚合事实计算，随
`start/end/station_id` 筛选。内部 `workbenchFacts` 不随 API 响应返回，不导出姓名、手机号或账本。

- `users`：已完成订单按结算日统计频次，频次 ≥ 2 为复购。全网分母为清洗后用户总数；
  单站分母为该站所选窗口有完成订单的用户数。0 单用户只计入全网频次分布。
- `user_mining`：R = 所选窗口末日与最近结算日之差（UTC，允许 0）；F = 完成订单数；
  M = 整数分金额。高价值阈值 F ≥ 8 且 M ≥ 100000，成长 F ≥ 3，其余低频。
  RFM 样本只含消费用户，`sample_count` 是总样本数，`top_users` 仅展示金额前 10 名。
- `equipment`：清洗后批次末桩快照，按站点过滤，不随日期回溯；状态以中文展示。
- `equipment_mining`：以所选站点范围全部桩的 `totalChargeCount` 计算总体均值、标准差和 z-score；
  绝对值 ≥2 为统计偏离异常，不等于设备故障。展示偏离最大的 8 桩，正常桩使用不同颜色。
  空样本/缺计数时不提供评分；无方差时 z-score 和异常数为 `null`，图中改为实际累计次数。
- `orders`：按创建日筛选，状态取批次末状态；平均时长仅取完成订单。
- `revenue/stations`：沿用筛选后的 ADS 结算营收；近 30 日相对查询终点计算。
- `energy`：沿用 ADS 小时重叠分摊电量，各时段互斥且覆盖 24 小时；
  订单曲线统计所选创建日队列中完成订单的起始小时，与电量分摊口径分别标注。
- 缺失用户事实的旧批次仍可展示设备、营收、负荷；需重建数仓补齐用户分析。
  未提供的评价事实和设备重启次数
  明确显示“未提供”；分母为零的比例为 `null`，不将其画成 0 或 `NaN%`。

独立 ML 的启动环境：`EV_ANALYSIS_ARTIFACT_DIR` 指向 `forecast.json`、
`recommendations.json`、`alerts.json`、`model_metadata.json` 所在目录；启动
`waitress-serve --listen=127.0.0.1:61501 ml.service.app:app`。生成/训练步骤见 `ml/README.md`。
analytics 大屏进程设置 `EV_ANALYSIS_API_BASE_URL=http://127.0.0.1:61501` 后重启，
`runtime-config.js` 会注入此地址；ML 可设置 `EV_ANALYSIS_ALLOWED_ORIGIN` 为大屏源地址。
这里的 loopback 地址供 VM 内浏览器使用。

腾讯底图：大屏进程读取浏览器专用 `TENCENT_MAP_JS_KEY`，经不缓存的 `runtime-config.js`
传给现有 GL 渲染器。分析模式不再强制拓扑；无 key、离线或 SDK 失败时仍回退拓扑，
`?map=topology` 可显式指定离线模式。WebService `TENCENT_MAP_KEY` 不会发送给浏览器。
实际 key 仅放部署环境，禁止提交到仓库；控制台须开通 JavaScript API GL 并允许部署来源。

预测、推荐、预警仍显式标注“独立 ML 链路”，不随 G3 筛选变化，也不是 G3 批次预测。
本次接入不改模型训练数据边界；要预测 G3 需另行构建其小时特征并重新训练。

### 工作台图表补全

- 营收回归：查询窗口内最近 30 日的已发布营收，以实际日差为自变量做 OLS；
  少于两日不拟合，常量序列的 R² 为 `null`。`fitted_cents` 与实际日期逐行对应，缺测日不补零。
- 站点分组：沿用项目利用率阈值，高负荷 ≥65%，均衡 ≥35%，其余低负荷。
  分组均值仅对实际成员计算；空分组为 `null`。图中按站点营收降序展示利用率与累计营收占比，
  两条序列共用站点横轴；零总营收的占比为 `null`，不强行分成三个非空组。
- 履约时长：扩展 `workbenchFacts.version=2`，ADS 订单聚合增加 `duration_le15`、
  `duration_15_30`、`duration_30_60`、`duration_gt60`。只统计完成订单真实起止时间之差，
  对应秒数 `(0,900]`、`(900,1800]`、`(1800,3600]`、`(3600,+∞)`，按创建日和站点过滤。
  每条聚合记录的四桶之和必须等于完成订单数，否则快照返回 503；v1 旧批次兼容且明确缺失分桶。
- 服务控制图：按所选创建日队列，整体完成率为基线，按每天订单量计算二项比例的三倍标准差控制限，
  上下限裁剪到 0–1；没有订单的日期不产生控制点。此图反映批次末完成状态，不是评价评分。
- 订单漏斗仅将展示标签映射为中文，底层状态码和统计数量不变。
