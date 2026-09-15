# 智能分析子系统

## 环境准备（Ubuntu 22.04+，国内镜像）

项目不要求系统级 npm 包。若系统缺少 `python3-venv`、`node` 或 `pytest`，使用以下用户目录安装方式，不需要 sudo：

```bash
cd /path/to/ev-charging-platform
source scripts/setup_stage2_env.sh
python3 -m pytest ml/tests -q
node --version && npm --version
node --test dashboard/tests/*.test.mjs
```

运行前置（非登录 shell，如 ssh 一次性命令）：Spark 会话依赖 `JAVA_HOME`（`export JAVA_HOME=/opt/module/jdk1.8.0_261` 或先 `source /etc/profile`）；python 侧 SparkSession 还要求 `pyspark`/`py4j` 可导入——pip 依赖目录缺失时补挂 `PYTHONPATH=$SPARK_HOME/python/lib/pyspark.zip:$SPARK_HOME/python/lib/py4j-*-src.zip:$PYTHONPATH`（`scripts/start_stage2.sh` 已按同样规则自动处理）。

本次已验证版本（2026-09-15 收口复跑，Ubuntu 22.04 VM）：Python 3.10.12、Java 8（1.8.0_261）、Spark 3.4.1（宿主机 `/opt/module/spark-3.4.1`，`spark-submit` 直接使用；pyspark 包取自 `$SPARK_HOME/python/lib`）、pandas 2.0.3、numpy 1.24.4、pyarrow 12.0.1、pytest 7.4.4。此前记录的 PySpark 3.3.4 为 pip 安装路径（`$S2_DEPS/pyspark/bin/spark-submit`）；两套环境下命令与产物等价。

第二阶段的数据分层、质量规则、预测/推荐/预警方案和 Flask 服务契约见：

- `docs/release/stage2-plan-2026-09.md`：团队排期、责任、里程碑和验收门。
- `docs/release/stage2-implementation-guide.md`：数据字典、ODS→DWD→DWS→ADS、模型、API、Dashboard 接入和验证命令。

当前状态：Schema v0.4 确定性数据生成、ODS 质量隔离、PySpark 分层、Spark MLlib 训练、预测/推荐/预警产物和 Flask 只读 API 已可复现运行。订单生成采用 78% 完成、15% 取消、7% 异常的确定性漏斗，避免完成率固定为 100%。快照还输出 RFM 用户分层、设备 z-score 异常检测、站点负荷聚类、订单转化曲线和能源峰谷识别。新增数据集仍必须先经过质量报告和清洗，再进入模型；分析服务只读分析产物，不直接写业务 SQLite。模型不可用时返回稳定的 `MODEL_UNAVAILABLE` 降级响应，不影响登录、站点查询、预约、充电和结算。

## 边界说明（与 analytics 链分工）

- 本 `ml/` 链是**本地链**：以 `spark-submit --master local[2]`（或 `local[*]`）在单机/VM 上直接运行，
  读写本地文件系统，**不使用 HDFS/YARN**；产物供 Flask 分析与 Dashboard 分析工作台使用。
- 课程要求的 Hadoop 伪分布式证据（HDFS/YARN/MapReduce）由另一条链承担：`analytics/` +
  `scripts/stage2/run_pipeline.sh`（ODS 上传 HDFS `/ev-stage2/batches/<id>/ods`、四层数仓与发布校验）。
  两条链的口径以各自文档为准，勿把 ml 链的运行表述为"HDFS/集群证据"。
- 利用率口径（2026-09-15 起）：小时/日网格全量展开（分母含零订单小时），利用率 =
  Σ 区间∩桶分摊充电秒数 /（桩数 × 全程秒数）；秒数/电量/金额按重叠占比分别分摊；
  DWS 新增 `dws/station_day`，DWD 无效行独立落盘至 `dwd/quarantine_charging_orders`。

## 已验证运行链路

依赖安装受限时可使用国内镜像安装到项目外目录；本次验证环境为 `/tmp/ev-s2-site`，运行时设置 `PYTHONPATH=/tmp/ev-s2-site`。

```bash
# 生成 Schema v0.4 业务库和 ODS（默认 90 天、120 用户、12 站点、25,000 完成订单）
PYTHONPATH=/tmp/ev-s2-site python3 ml/data/generate_analysis_dataset.py \
  --output /tmp/ev-s2-final/ev-analysis.sqlite \
  --ods-dir /tmp/ev-s2-final/ods --seed 20260914 --days 90 \
  --users 120 --stations 12 --piles-per-station 8 --orders 25000 \
  --end-date 2026-09-14

PYTHONPATH=/tmp/ev-s2-site python3 ml/jobs/quality_report.py \
  --input /tmp/ev-s2-final/ods --output /tmp/ev-s2-final/quality.json

PYTHONPATH=/tmp/ev-s2-site SPARK_LOCAL_IP=127.0.0.1 \
/tmp/ev-s2-site/pyspark/bin/spark-submit --master local[2] \
  ml/jobs/run_pipeline.py --ods-dir /tmp/ev-s2-final/ods \
  --output /tmp/ev-s2-final-pipeline

PYTHONPATH=/tmp/ev-s2-site SPARK_LOCAL_IP=127.0.0.1 \
/tmp/ev-s2-site/pyspark/bin/spark-submit --master local[2] \
  ml/models/train_forecast.py --features /tmp/ev-s2-final-pipeline/ads/load_features \
  --output /tmp/ev-s2-final-model

PYTHONPATH=/tmp/ev-s2-site python3 ml/models/build_outputs.py \
  --features /tmp/ev-s2-final-pipeline/ads/load_features \
  --model /tmp/ev-s2-final-model/spark_model \
  --model-version spark-rf-20260914 \
  --output /tmp/ev-s2-final-analysis/ads

PYTHONPATH=/tmp/ev-s2-site python3 ml/service/build_dashboard_snapshot.py \
  --database /tmp/ev-s2-final/ev-analysis.sqlite \
  --dws /tmp/ev-s2-final-pipeline/dws \
  --output /tmp/ev-s2-final-analysis/ads/dashboard.json

PYTHONPATH=/tmp/ev-s2-site EV_ANALYSIS_ARTIFACT_DIR=/tmp/ev-s2-final-analysis/ads \
  flask --app ml.service.app run --host 127.0.0.1 --port 61501
```

已验证（2026-09-15 收口复跑 + 2026-09-16 评审修复批复跑，逐字引自 VM 输出；证据包在仓库外 `build/stage2/evidence/`）：`python3 -m pytest ml/tests -q` 为 **22 passed**（评审修复批 VM 复跑；含 5 条 Spark 管线用例：分摊守恒 / 零订单小时分母 / quarantine 守恒与快照同口径 / NULL 判定落盘回归（缺失与不可解析数值）/ 守恒中止单测）；完整分层输出 **输入 25,005 行（正式 DWD 有效 19,543 / 隔离 5,462，守恒 balanced）**、**DWS `station_hourly` 30,142 行 + `station_day` 1,260 行（小时/日网格全量展开，分母含零订单时段）**、ADS 日营收 90 行；Spark 训练 **24,430/5,712 行，验证 MAE 16.30、RMSE 30.78**；预测产物 42 条（14 站 × 1/6/24h，`target_hour` 三档各异）、推荐 3 条、预警 1 条；Flask 只读 API（health / forecast 契约与降级 / dashboard snapshot 契约）由 `ml/tests/test_service.py` 用例覆盖通过；Dashboard 快照来自 Schema v0.4 SQLite（14 站点、102 桩），并提供 `analytics` 工作台数据（用户、设备、订单、能源、收益、站点、服务代理指标），不依赖 `demo.json` 作为正常数据源。运行库、Parquet、模型和日志均位于仓库外，不纳入 Git。
