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
| `station_id` 合法但不存在 / 窗口内无业务记录 | **200 + `"status":"empty"`**（`data` 为完整窗口的空结构） |
| 窗口超出批次 coverage | **400**，`error.available_range` 给出可用范围，不悄悄补零 |
| 无成功快照 / 快照文件损坏 | **503**（`empty`/`503` 均**不得**回退 demo 数据伪装成功） |
| 批次旧但可读 | 200，`meta.stale=true`（过期判定取配置），页面显示数据日期 |

错误结构：

```json
{"status": "error", "error": {"code": "invalid_window", "message": "start must be before end",
 "available_range": {"start": "2026-06-16", "end": "2026-09-14"}}}
```

`code` 枚举：`invalid_date`、`invalid_window`、`window_too_large`、`invalid_station_id`、
`out_of_coverage`、`snapshot_missing`、`snapshot_corrupt`。

## 4. 前端映射（dashboard/js/api-data-provider.js）

- `overview.revenueCents` → 直接接入视图模型 `overview.revenueCents`；
- `revenueDaily` → 趋势数组（按日期，**不假定永远 7 条**；30d 视图 = 请求 30 天窗口）；
- `loadHourly` → 负荷图（完整时间戳）；
- `stations/piles` → 与现有 demo 视图模型同形（camelCase，字段见上）；`stationUtilization` → 站级利用率；
- `adaptDashboardData` 扩展为按 `source_type` 校验：`synthetic_warehouse` 数据必须携带
  `meta.batch_id/generated_at/coverage`；`demo_fixture` 保持原演示路径；
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
