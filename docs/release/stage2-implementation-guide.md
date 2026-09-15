# 第二阶段实现指南

> 本指南以第一阶段 SQLite Schema v0.4 为业务事实模型，通过确定性生成器构造分析数据库，再转换为可验证的大数据与智能分析链路。`04.数据集最终版/` 只作为字段范围和质量场景参考，不是主训练事实。生成数据、模型文件和日志默认放在仓库外 `../build/stage2-data/`，不提交运行库、密钥或构建产物。

## 1. 建议目录与环境

推荐新增以下源码目录；目录名可在实现时调整，但职责不要散落到 Qt、Server 或 Dashboard 业务代码中：

```text
ml/
  data/                 # schema、字典和质量规则（不放生成数据）
  jobs/                 # PySpark 作业：ingest、quality、dwd、dws、ads
  models/               # 特征、训练和评估代码
  service/              # Flask API 与响应模型
  tests/                # pytest/unittest 与契约测试
  README.md
```

本地准备（Ubuntu 22.04+，Python 3.10+；依赖安装在仓库外）：

```bash
cd /home/bit/projects/work/ev-charging-platform
source scripts/setup_stage2_env.sh
python3 -c "import pyspark, flask; print('pyspark', pyspark.__version__)"
```

`setup_stage2_env.sh` 使用清华 PyPI 与 npmmirror，将 PySpark/Flask/pytest 安装到
`/tmp/ev-s2-site`，Node.js 安装到 `/tmp/ev-node`；不会写入仓库，也不需要 sudo。

课程要求的 Hadoop 可安装为本地/伪分布式环境，但作业默认 `master=local[*]`，保证没有 HDFS 时也能验收。记录 `java -version`、`hadoop version`（若安装）和 `pyspark --version`；不要把 Hadoop 配置或本地路径写死进代码。

统一变量：

```bash
export EV_S2_ROOT=/tmp/ev-s2-present
export EV_S2_RAW="$PWD/04.数据集最终版"
export EV_S2_BASE_DATE=2026-09-14
mkdir -p "$EV_S2_ROOT"/{ods,dwd,dws,ads,quarantine,reports,models,logs}
```

## 2. 数据契约与标准化

### 2.1 ODS 原始层

生成器先从 `database/schema/schema.sql` 创建独立分析库，可选择加载 `database/seeds/dev.sql`，再生成符合约束的历史用户、站点、桩、订单和钱包流水。业务库通过现有触发器和外键校验后，导出订单、站点、桩状态事件等为 ODS；每条 ODS 记录增加 `source_table`、`source_database_hash`、`generation_seed`、`ingest_batch_id`、`ingested_at_utc` 和 `source_row_id`。ODS 不修值、不补值。质量问题注入发生在独立的原始事件层，不能直接写进 SQLite 业务表。

原始事件层保留三类来源：合法的 Schema 导出事实、按规则注入的坏事件、以及标记为独立主题的天气/节假日/电池遥测数据。每批生成 SHA-256、行数和生成参数清单；重复导入以 `source_database_hash + source_table + source_row_id` 去重，不修改原始事实。

### 2.2 DWD 清洗明细

建议统一字段（保留 `source_*` 原值）：

| DWD 字段 | 来源 | 规则 |
|---|---|---|
| `session_id` | `charging_orders.id/order_no` | 非空、唯一；保留数据库主键和订单号 |
| `event_date`/`start_hour` | `created_at/started_at/settled_at` | UTC ISO-8601；小时限定 0–23 |
| `duration_minutes` | `started_at/ended_at` | 按时间差计算；与生成参数不一致标质量问题 |
| `energy_kwh` | `energy_wh` | 瓦时转千瓦时；非负；0 值按场景保留 |
| `fee_cents` | `total_amount_cents` | 完成订单必须匹配钱包 charge 流水；金额保持整数分 |
| `station_id`/`pile_id`/`user_id` | 外键列 | 必须能回连 Schema v0.4；孤儿事件只留在 quarantine |
| `station_name`/`address`/`device_count` | `stations` + `charging_piles` 聚合 | 由业务表生成，不重复维护第二套站点事实 |
| `battery_*` | 独立分析主题 | 当前核心 Schema 没有遥测表；无明确外键时不得关联到订单负荷 |

### 2.3 质量问题与处理策略

发现和注入必须分开记录：

| 质量码 | 发现/注入 | 判定 | 处理 |
|---|---|---|---|
| `BAD_DATE_ANONYMIZED` | 发现 | 年份为 0014/0015，或遥测时间不可解析 | 采用相对日期；保留原字段和方法 |
| `DUPLICATE_SESSION` | 注入 | 复制一行 session | quarantine，按 session_id 去重 |
| `NULL_REQUIRED` | 注入 | 主键/站点/时间为空 | quarantine |
| `NEGATIVE_OR_RANGE` | 注入 | 电量、费用为负；SOC 不在 0–100；电压/温度越界 | quarantine 或标记，规则写入报告 |
| `ZERO_ENERGY`/`ZERO_FEE` | 发现 | 合法但业务上可疑的 0 值 | 保留，聚合时分别统计，不补成正数 |
| `LONG_SESSION` | 发现 | `duration_minutes > 24*60` | 保留原值，分析指标中排除或截尾，并记录口径 |
| `ORPHAN_STATION` | 发现 | 事实表站点不在维表 | quarantine，不能自动创建站点 |
| `BAD_TELEMETRY_TIME` | 发现 | record_time 全部相同/科学计数法 | 不用于小时负荷监督标签 |

质量报告至少输出：输入行数、有效行数、隔离行数、各质量码计数、去重数、站点关联率、时间覆盖区间和金额/能耗总量。报告必须能解释每一条被排除记录。

## 3. SparkSQL 分层作业

### 3.1 DWS 聚合

先形成统一的小时事实：

```sql
SELECT event_date,
       start_hour,
       station_id,
       COUNT(*) AS session_count,
       SUM(energy_kwh) AS energy_kwh,
       SUM(fee_cents) AS fee_cents,
       AVG(duration_minutes) AS avg_duration_minutes,
       COUNT(DISTINCT user_id) AS user_count
FROM dwd_charge_session
WHERE quality_valid = true
GROUP BY event_date, start_hour, station_id;
```

站点设备利用率采用明确分母：`device_count * 60` 为每小时可用桩分钟；充电区间与小时窗口求交得到占用分钟。若数据只有会话起止时间，使用区间交集，不从订单数推断利用率。故障/离线桩是否进入分母要在 ADS 字段中写明版本。

### 3.2 ADS 输出

建议至少生成以下稳定文件或表：

- `ads_revenue_daily`: `date, revenue_cents_raw, revenue_cents_estimated, completed_order_count, energy_kwh, data_quality_summary`。
- `ads_station_hourly`: `date, hour, station_id, load_kwh, session_count, utilization, idle_pile_estimate`。
- `ads_load_features`: 最近 7/14/30 天同小时、星期、站点、节假日（无真实节假日时为 `unknown`）和遥测健康特征。
- `ads_forecast`: `station_id, horizon_hours, target, predicted_value, generated_at, data_cutoff_at, model_version, status`。
- `ads_recommendation`: `origin_context, station_id, predicted_load, predicted_idle_rate, score, reasons, generated_at, model_version`。
- `ads_alert`: `station_id, horizon_hours, level, threshold, predicted_value, generated_at, dedupe_key, status`。

金额全部用整数分；模型输入可以使用浮点，但 API 输出必须带单位和小数规则。所有时间使用 UTC ISO-8601，分析日期另带 `timezone=UTC`。

## 4. 预测、推荐与预警

### 4.1 先做可解释基线

数据时间跨度短、真实时间语义不完整时，先实现按站点/小时/星期的历史均值或加权均值基线：

```text
预测(t+h) = 同站点、同星期、同小时的历史均值
           （样本不足时回退到站点小时均值，再回退到全局小时均值）
```

预测目标：`load_kwh`、`session_count`、`idle_pile_estimate`。1/6/24 小时只是预测窗口，不代表训练样本有 24 小时的完整连续观测。每个结果带 `fallback_level` 和样本数。

若基线回归通过，再用 Spark MLlib 的 `VectorAssembler` + `GBTRegressor` 或 `RandomForestRegressor` 做回归。特征可包括：小时、星期、站点类型、设备数、近 1/6/24 小时负荷滞后、滚动均值、SOC/温度聚合。按时间切分训练/验证，禁止随机打乱造成未来信息泄漏；记录 MAE/RMSE、训练区间和模型版本。不要在样本不足时宣称“精准”。

### 4.2 低拥堵站点推荐

对每个候选站点计算可解释分数，例如：

```text
score = 0.50 * predicted_idle_rate
      - 0.30 * normalized_predicted_load
      - 0.20 * normalized_wait_risk
```

仅对 active 且有有效设备数的站点排序。输出前三名及理由（“预测空闲率高”“未来 1 小时负荷低”等），无候选站点返回 `NO_CANDIDATE`，不凭距离或缺失字段暗中补全。

### 4.3 负荷预警

阈值放入配置而不是代码常量，例如 `load_ratio >= 0.85` 为 high、`>= 0.70` 为 medium；分母为站点设备数 × 额定功率时必须标明额定功率来源。预警以 `station_id + horizon + data_cutoff + level` 去重，保存 acknowledged 状态，不重复刷屏。没有可信容量数据时，使用历史 P95 作为“统计阈值”，并在页面标注“历史基线预警”。

## 5. 模型服务契约（Flask）

建议新增 `POST /api/v1/analysis/forecast`、`GET /api/v1/analysis/recommendations`、`GET /api/v1/analysis/alerts` 和 `GET /api/v1/analysis/health`。示例请求：

```json
{
  "station_ids": ["station-1"],
  "horizons_hours": [1, 6, 24],
  "as_of": "2026-09-16T08:00:00Z",
  "model_version": "baseline-20260916"
}
```

成功响应至少包含：`status`, `generated_at`, `data_cutoff_at`, `timezone`, `model_version`, `items`。单项字段包含 `station_id`, `horizon_hours`, `target`, `predicted_value`, `unit`, `sample_count`, `fallback_level`。失败响应使用稳定码：`INSUFFICIENT_DATA`、`MODEL_UNAVAILABLE`、`INVALID_REQUEST`、`INTERNAL_ERROR`，不返回 traceback、文件路径或密钥。

Flask 服务只读 ADS/模型产物；不写业务 SQLite，不改变订单/桩状态。服务端 B 若要统一出口，可增加 Socket `analysis.*` 适配，但不得让 Qt 客户端绕过既定权限和业务边界直接访问数据库。

## 6. Dashboard 与 Qt 接入

现有 Dashboard 已支持本地 ECharts、离线拓扑图及 loading/empty/error/offline/stale 状态。新增分析板块应复用这些状态组件：

- 负荷趋势：实际值与预测值分色，图例标明“历史/预测”，横轴显示 UTC 时间和 1/6/24 小时窗口。
- 站点推荐：显示站点、预测空闲率、负荷和理由；无结果显示专门空态。
- 预警：显示等级、阈值、预测值、生成时间和模型版本；预测失败保留基础统计横幅。
- 页面显示数据来源（`ads_snapshot`、`model_service`、`fallback_baseline`）和最后更新时间，不把预测值混入第一阶段实时桩状态占比。

当前 Dashboard 的二阶段独立工作台采用侧边导航切页，页面之间通过 URL hash 切换；每页只组合一个或两个相关分析域，并在图表上方显示快照 KPI。页面包括：网络总览、智能预测、用户与设备、订单与能源、收益与站点、评价与服务。分析页内部保留以下八个数据视角：

- 机器学习预测：1/6/24 小时 Spark MLlib 负荷预测、模型版本和数据截止时间；
- 充电用户：完成订单次数分群和复购用户比例；
- 充电设备：设备状态、快慢充类型、功率档位和模拟设备数量；
- 充电订单：近 30 日全部/完成/取消订单趋势、完成率、取消率和平均时长；
- 充电能源：总能耗、单次平均能耗、小时能耗与订单量双轴、峰值小时；
- 运营收益：近 30 日营收和日均收益；
- 站点运营：按营收排序的站点 Top 10；
- 评价与服务：当前 Schema v0.4 没有评价表，因此明确展示完成率、非取消率、复购率和时长代理指标，不伪造评分。

为避免“只做 SQL 聚合”，快照还输出可解释的数据挖掘结果：用户侧使用 RFM（最近消费、频次、金额）分层；设备侧使用充电次数 z-score 识别异常桩；站点侧按利用率与能耗规则聚类为高负荷/均衡/低负荷；订单侧输出日级转化漏斗和完成率曲线；能源侧输出小时峰谷与双轴关联；预测页使用 Spark MLlib RandomForestRegressor，并叠加 P95 基线预警。所有方法、特征和阈值写入快照字段或模型元数据，便于答辩追溯。

页面切换只改变当前分析视角，不重新请求数据库；图表使用同一份带版本和更新时间的快照，避免不同面板之间出现口径漂移。宽度小于 1180px 时侧边导航转为横向导航，小于 720px 时分析卡片单列排列。

优先将结果接入 Dashboard；用户端只增加轻量“推荐站点/预计等待风险”入口，分析服务不可用时隐藏分析卡并保持现有站点、预约、充电流程。管理端可在概览或销售页增加分析状态入口，但不要把预测营收与已验收的 7/30 日实际营收混为一张指标。

## 7. 验证清单

### 数据与 Spark

```bash
python3 ml/data/generate_analysis_dataset.py \
  --output "$EV_S2_ROOT/ev-analysis.sqlite" --ods-dir "$EV_S2_ROOT/ods" \
  --seed 20260914 --days 90 --users 120 --stations 12 --piles-per-station 8 \
  --orders 25000 --end-date 2026-09-14
python3 ml/jobs/quality_report.py --input "$EV_S2_ROOT/ods" --output "$EV_S2_ROOT/reports/quality.json"
SPARK_LOCAL_IP=127.0.0.1 "$S2_DEPS_DIR/pyspark/bin/spark-submit" --master local[2] \
  ml/jobs/run_pipeline.py --ods-dir "$EV_S2_ROOT/ods" --output "$EV_S2_ROOT/pipeline"
PYTHONPATH=/tmp/ev-s2-site python3 -m pytest ml/tests -q
```

断言包括：分层行数对账、质量码计数、session 去重、站点外键、金额分求和、UTC 日期连续性、同输入两次输出一致。

### 模型与 API

```bash
SPARK_LOCAL_IP=127.0.0.1 "$S2_DEPS_DIR/pyspark/bin/spark-submit" --master local[2] \
  ml/models/train_forecast.py --features "$EV_S2_ROOT/pipeline/ads/load_features" \
  --output "$EV_S2_ROOT/model"
python3 ml/models/build_outputs.py --features "$EV_S2_ROOT/pipeline/ads/load_features" \
  --model "$EV_S2_ROOT/model/spark_model" --output "$EV_S2_ROOT/ads"
EV_ANALYSIS_ARTIFACT_DIR="$EV_S2_ROOT/ads" "$S2_DEPS_DIR/bin/flask" \
  --app ml.service.app run --host 127.0.0.1 --port 61501
curl -s http://127.0.0.1:61501/api/v1/analysis/health
```

API 测试覆盖正常、非法请求、空数据、模型文件缺失、超时/异常、版本字段、单位和降级状态。停止 Flask 后重新执行用户端/管理端原有 qmake6 测试，证明基础业务不受影响。

### 第一阶段回归与发布

按 `current.md` 既有命令重新执行受影响 Qt 模块：`qmake6 <module>.pro`、`make -j2`、QtTest；同时运行 `node --test dashboard/tests/*.test.mjs`、`python dashboard/serve.py --check`、`git diff --check` 和敏感信息扫描。所有构建目录、运行数据库、模型二进制、日志和本地环境文件放在仓库外或被 `.gitignore` 排除。

## 8. 答辩演示顺序

1. 展示原始数据质量报告：匿名日期、零费用、异常时长和遥测时间问题。
2. 运行一次 SparkSQL 分层，展示 DWD 隔离记录和 ADS 指标对账。
3. 打开 Dashboard：历史负荷→1/6/24 小时预测→推荐站点→预警。
4. 停止模型服务，刷新页面，展示“基础统计+分析不可用”降级；随后验证用户端预约/充电或管理端查询仍可用。
5. 展示模型版本、数据截止时间、测试输出和源码目录，明确哪些是样本归一化/基线回退，避免把演示数据或预测结果表述为生产实时数据。
