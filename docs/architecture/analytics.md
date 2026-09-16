# 第二阶段分析层契约（analytics）

> T0 契约冻结文档（2026-09-15 定稿）。配套：`docs/api/analytics.md`（API/前端契约）、
> `superpowers/plans/2026-09-14-stage2-delivery-plan.md`（计划）、`2026-09-14-stage2-technical-runbook.md`（手册）。
> 本文所有约定为实施依据；未在此定义的字段/行为以手册为准，不得另行发明。

## 1. 范围与来源

- 分析对象 = 第一阶段业务库 SQLite schema v0.4 的五张源表（**白名单列**）：
  `users`、`stations`、`charging_piles`、`charging_orders`、`wallet_transactions`。
- 数据来源标签 `source_type`：
  - `synthetic_warehouse` —— 第二阶段生成器产出的模拟数据集（默认，屏幕须标注"模拟数据"）；
  - `demo_fixture` —— 原 `dashboard/data/demo.json` 演示入口（保留，不得冒充分析数据）；
  - `sqlite_export` —— 预留（`export_sqlite.py` 可选路径）。
- 业务系统（Qt 用户端/管理端/服务端）保留演示背景，不承担脏数据实验。

## 2. 单位与时区（全链路）

| 约定 | 值 |
|---|---|
| 时区 | **UTC**（`spark.sql.session.timeZone=UTC`；展示标注 "UTC"） |
| 时间格式 | ISO-8601 `YYYY-MM-DDTHH:MM:SSZ`（秒级，无毫秒） |
| 日期边界 | 左闭右开 `[start, end)`；日粒度 `stat_date` = UTC 日历日 |
| 金额 | 整数**分**（CNY cents），BIGINT |
| 电量 | `energy_wh` 整数**瓦时**；展示换算 kWh（/1000） |
| 功率 | `power_kw`（kW，可小数）；负荷 = 分摊 Wh / 1000 / 1h → kW |
| 计费公式 | `total_amount_cents = (energy_wh * unit_price_cents_per_kwh + 999) // 1000 + service_fee_cents`（整数向上取整到分；出处 `libs/database/src/database.cpp:2084`） |

## 3. 原始记录（ODS）格式

每张表一个 CSV（UTF-8，含表头），**所有业务列为原始字符串**，前置信封列：

```
source_record_id,batch_id,source_file,source_line,<表白名单列...>
```

- `source_record_id`：全批次内唯一（`<table>-<6位序号>`，如 `orders-000123`）；
- `source_line`：该记录在其源文件中的行号（1 起，含表头偏移由实现记录在 manifest）；
- 白名单列（列名与业务库一致，全小写下划线）：

| 表 | 列 |
|---|---|
| users | id, phone, nickname, balance_cents, status, created_at, updated_at |
| stations | id, name, address, latitude, longitude, status, created_at, updated_at |
| charging_piles | id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, status, total_charge_count, total_charge_seconds, created_at, updated_at |
| charging_orders | id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at |
| wallet_transactions | id, user_id, order_id, transaction_type, amount_cents, balance_after_cents, created_at |

- 生成器输出（本地 `batches/<batch-id>/input/`）：上述五 CSV + `dirty_labels.jsonl`（污染真值，不送入规则判断）。
- `manifest.json` 字段：`batch_id, seed, profile, schema_version, source_type, timezone, data_start,
  data_end_exclusive, generated_at, generator_version, rules_version, code_commit, tables{rows,sha256},
  pollution{rate, rules[], counts}, collection_gaps[], notes`。

## 4. 数仓分层与物理路径

HDFS 根：`/ev-stage2/batches/<batch-id>/`；本地产物根：`EV_ANALYTICS_ROOT`（默认 `~/ev-stage2-artifacts`）。

| 层 | 路径 | 数据集（parquet） | 粒度 |
|---|---|---|---|
| ODS | `.../ods/` | `users.csv, stations.csv, charging_piles.csv, charging_orders.csv, wallet_transactions.csv` | 一行源记录（原始字符串） |
| DWD | `.../dwd/` | `dim_user, dim_station, dim_pile, fact_order, fact_wallet, repair_log` | 唯一业务实体/事件；类型标准化、主外键有效 |
| 隔离区 | `.../quarantine/` | `quarantine`（一行一 `source_record_id`，`reason_codes` 数组） | 源记录级 |
| DWS | `.../dws/` | `dws_station_day`、`dws_station_hour` | 见下 |
| ADS | `.../ads/` | `ads_overview, ads_revenue_trend, ads_load_hour, ads_station_rank, ads_quality_summary, ads_pile_snapshot` | 见下 |

### 4.1 DWS 粒度

- `dws_station_day`：`station_id + stat_date`（UTC）。
  - `created_order_count`（按 `created_at`，含取消/异常等全部合法状态）；
  - `settled_order_count` / `revenue_cents` / `settled_energy_wh`（按 `settled_at`，仅 completed）；
  - `settled_charge_seconds`（已完成订单充电秒数，按 `settled_at` 归日）。
- `dws_station_hour`：`station_id + hour_start`（UTC 整点）。
  - `allocated_wh`（订单电量按充电区间∩小时比例分摊，高精度累计，最终一次性舍入）；
  - `charge_seconds`（区间∩小时秒数）；
  - `capacity_pile_seconds`（≥1 桩 × 1 小时 = 3600；按窗口内活跃桩容量只计一次）；
  - `avg_load_kw` = `sum(allocated_wh)/1000/1h`。

### 4.2 ADS 数据集（屏幕投影，全部含 `batch_id`）

- `ads_overview`（1 行/批次）：窗口合计 `revenue_cents, completed_orders, energy_wh, total_order_count,
  cancelled_orders, exception_orders, station_count, pile_count, avg_station_utilization` +
  `window_start, window_end_exclusive, coverage, source_type, generated_at`。
- `ads_revenue_trend`：`stat_date × station_id → revenue_cents, completed_orders, energy_wh`。
- `ads_load_hour`：`hour_start × station_id → allocated_wh, charge_seconds, capacity_pile_seconds, load_kw`。
- `ads_station_rank`：`station_id → revenue_cents, completed_orders, energy_wh, utilization, rank_no`
  （全窗口；平局按 `station_id` 升序）。
- `ads_quality_summary`：清洗守恒与规则统计（每表 `input/kept/duplicate/quarantine/repaired` + 规则命中）。
- `ads_pile_snapshot`：`snapshot_at` 时刻各桩状态快照（`pile_id, station_id, pile_code, pile_type,
  power_kw, unit_price_cents_per_kwh, status, total_charge_count, total_charge_seconds`），标签"批次快照"。
  **同行附带站点 `station_name, latitude, longitude`**——大屏站点节点（地图投影/列表命名）的数据来源；
  缺坐标即发布缺陷（前端 `assertFinite` 会直接报错，曾实测命中）。

指标口径补充（与计划 §5.4 一致）：
- 利用率 = 窗口内 `Σ charge_seconds / Σ capacity_pile_seconds`（先按桩截断区间再汇总，≤100%）；
- 小时分摊：`allocated_wh = energy_wh × overlap_seconds / (ended-started)`，每单 Σ 分配 = 源电量；
- 空白日期仅在 manifest 证明窗口完整时补零；批次缺失返回 unknown；桩快照不从当前表推断历史在线率。
- **发布覆盖窗口（2026-09-15 起）**：`available_start/available_end_exclusive` = 生成窗口 ∪
  末端结算追加日 + 1 天（右开）。导出（export_ads.py）按实际数据范围写入发布快照 meta；
  API 默认查询窗口、覆盖校验与前端筛选控件均以此为准，保证窗口末端结算收入可查询
  （生成窗口 `data_start/data_end_exclusive` 原值仍保留在 meta 中）。

## 5. 目录与发布

工作台扩展（2026-09-16）：`build_warehouse.py` 从同批次 DWD 增加
`ads_user_activity`（用户 × 站点 × 结算日的完成频次、金额和最近结算）、
`ads_user_summary`（清洗后用户总量）和 `ads_order_activity`（站点 × 创建日 × 状态 × 起始小时的
订单数及完成时长）。导出器仍只读取 ADS，将必要事实写入发布快照内部 `workbenchFacts`；
API 的 `workbench.py` 在筛选后聚合为 `data.analytics`，不向浏览器返回内部事实表，
也不读取业务库或启动 Spark。旧快照缺少扩展事实时，缺失指标明确降级。
聚合事实 v2 在 `ads_order_activity` 增加完成订单真实时长四桶（15/30/60 分钟边界，右闭），
发布快照逐行校验四桶之和等于完成数量。营收回归、站点利用率分组和完成率控制限
由 API 对所选窗口统计，不引入独立数据源，也不回推缺失分桶。
日期、复购分母、RFM 阈值与独立 ML 来源边界见 `docs/api/analytics.md` §7。

```
仓库（入 git）                    产物（不入 git）
analytics/                        D:/work/chargingplatform/build/stage2/   (Windows 工作区)
  generate_data.py                EV_ANALYTICS_ROOT/                       (VM, 默认 ~/ev-stage2-artifacts)
  export_sqlite.py(可选)            batches/<batch-id>/{manifest.json,input/,quality/,logs/}
  jobs/{quality_profile,clean,build_warehouse}.py
  sql/{dws_station_day,dws_station_hour,ads_dashboard}.sql
  export_ads.py                   published/<batch-id>/{dashboard.json,quality.json,manifest.json}
  api/{app.py,snapshot_store.py}  published/latest.json  (指针，成功发布后才原子切换)
  tests/...
scripts/stage2/{run_pipeline.sh,verify_release.py}
```

- 发布原子性：staging 校验通过后才切换 `latest.json`；失败保留旧批次（`export_ads.py` 内先写
  `published/<batch-id>/`，成功后再单写 `latest.json`）。
- 批次命名：`s2-<profile>-<YYYYMMDD>`（如 `s2-smoke-20260915`、`s2-standard-20260915`）。

## 6. 数据规模（profile）

| profile | 站 | 桩 | 用户 | 订单 | 窗口（UTC） |
|---|---:|---:|---:|---:|---|
| smoke | 2 | 12 | 100 | 1,000 | 2026-09-01 ~ 2026-09-14（含） |
| standard | 30 | 300 | 5,000 | 50,000 | 2026-06-16 ~ 2026-09-13（含） |
| low | 10 | 100 | 500 | 10,000 | 2026-06-16 ~ 2026-09-13（含） |

统一 seed 默认 `20260914`。同 seed 重跑业务记录与污染标签逐字一致；`generated_at` 独立保存。

## 7. 清洗规则（DQ01–DQ12）与守恒

- 规则清单、检测与处置、验收重点沿用计划 §5.2 表（DQ01 完全重复 / DQ02 同主键冲突整组隔离 /
  DQ03 关键缺失 / DQ04 非关键缺失修复 / DQ05 类型与单位 / DQ06 状态码变体 / DQ07 时间格式与逻辑 /
  DQ08 负值与超范围 / DQ09 外键缺失 / DQ10 计费与流水不一致 / DQ11 时间重叠与超物理上限 /
  DQ12 异常但合法高峰与缺测时段）。
- 每张小表独立守恒：`输入 = 保留 + 去重 + 隔离`；修复是保留行子集（`repair ≤ keep`），不得重复计数。
- 处置优先级：不可解析/关键缺失/主键冲突 → 隔离；全字段重复 → 去重；其余做枚举/外键/时间/计费/流水/
  区间校验；可修复字段保留原值并记录 `repair_log`。
- 业务主键：`users.id / stations.id / charging_piles.id / charging_orders.order_no / wallet_transactions.id`。
- 金额检查：completed 订单必须满足计费公式且存在匹配 charge 流水（`user/order/金额相反/created_at=settled_at`）。

## 8. 手算样例（fixture A，人工核对基准）

| 订单 | 桩 | started | ended | settled | Wh | 分/kWh | 服务费 | 金额分 |
|---|---|---|---|---|---:|---:|---:|---:|
| T1（O1） | P1 | 09-01T00:30Z | 09-01T01:30Z | 09-01T01:35Z | 10000 | 120 | 100 | **1300** |
| T2（O2） | P2 | 09-01T02:00Z | 09-01T02:30Z | 09-01T02:35Z | 5001 | 120 | 0 | **601** |

人工预期：营收 **1901 分**；电量 **15.001 kWh**；充电 **5400 秒**；9/1 利用率 **3.125%**
（5400/(2×86400)）；小时 00/01/02 分配 **5000/5000/5001 Wh**（负荷 5/5/5.001 kW）。
`analytics/tests/fixtures/dq01/` 为可直接回放的最小样例（ODS=7、DWD=3、去重=1、隔离=3、修复=0）。

## 9. 责任人（按计划 §6.1 角色迁移建议）

| 模块 | 主要负责 | 复核 |
|---|---|---|
| 生成器/污染/fixture | A | B |
| 清洗/质量报告 | A | B |
| DWS/ADS SQL、导出、Flask API | B | C |
| 大屏接入/筛选/状态 | C | A |
| 集成/证据/打包 | C | 全员 |

## 10. 运行环境已知陷阱（VM 实测，实现必须遵守）

1. **本地模式延迟调度卡死（SPARK-42923 征兆，最烈）**：`local[N]` 下读取 HDFS Parquet 时，
   块位置主机（master/127.0.1.1）与本地执行器主机（192.168.182.128）不匹配，延迟调度会让
   后续任务**永久排队**（UI 恒显 1 done / 0 active，实测卡 15 分钟零进展）。所有 spark-submit
   必须携带 `--conf spark.locality.wait=0 --conf spark.locality.wait.node=0
   --conf spark.locality.wait.rack=0 --conf spark.locality.wait.process=0`
   （run_pipeline.sh 与三个 jobs 的 builder 均已内置）。
2. **不要用 `F.cast(col, "double")`**：pyspark 3.4.1 实测该调用返回字符串 `'double'` 而非 Column，
   随后 `F.when(cond, 该串)` 会把类型名当**字面量**写进数据（canon 列出现 `'string'` 的实案）。
   一律用方法形式 `col.cast("double")`。
3. **`unix_timestamp` 解析 `"T…Z"` 字符串返回 NULL**（默认格式不含字面量 `Z`）→ 会静默关闭
   依赖它的校验（如超物理上限）。必须 `to_timestamp(col, "yyyy-MM-dd'T'HH:mm:ss'Z'").cast("long")`。
4. **PySpark 收集 timestamps 得到的是"驱动本地时区"naive datetime**（与 session.timeZone 无关）：
   跨时区 VM（本机 CST=UTC+8）上直接 `strftime`/`str` 会错 8 小时。需要字符串时一律在 Spark 侧
   用 `date_format` 产出（测试与 export_ads 均按此修）。
5. **`analytics/jobs/` 下禁止出现与 stdlib 同名的模块文件**（如 `profile.py`）：spark-submit 把脚本
   目录放进 `sys.path[0]`，pyspark 内部 `import cProfile → import profile` 会反向导入本文件造成
   循环导入崩溃。质量统计模块因此定名 `quality_profile.py`，勿改回。
6. **夹具 CSV 手工编辑后必须过 `TestFixtureIntegrity`**：少/多一个逗号会造成整行字段错位
   （Spark PERMISSIVE 模式静默丢尾字段），曾导致 20 字段行伪装成"合法"数据参与全链路。

> 本文件与实施代码冲突时：先改本文件再改代码（评审门禁）；任何"已实现"声称须以
> `build/stage2/evidence/` 实测记录为准。
