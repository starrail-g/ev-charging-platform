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

本次已验证版本：Python 3.10.12、Java 8、PySpark 3.3.4、pandas 2.0.3、Node 20.18.1、npm 10.8.2、Qt 6.2.4/qmake6。`spark-submit` 使用 `$S2_DEPS/pyspark/bin/spark-submit`。

第二阶段的数据分层、质量规则、预测/推荐/预警方案和 Flask 服务契约见：

- `docs/release/stage2-plan-2026-09.md`：团队排期、责任、里程碑和验收门。
- `docs/release/stage2-implementation-guide.md`：数据字典、ODS→DWD→DWS→ADS、模型、API、Dashboard 接入和验证命令。

当前状态：Schema v0.4 确定性数据生成、ODS 质量隔离、PySpark 分层、Spark MLlib 训练、预测/推荐/预警产物和 Flask 只读 API 已可复现运行。订单生成采用 78% 完成、15% 取消、7% 异常的确定性漏斗，避免完成率固定为 100%。快照还输出 RFM 用户分层、设备 z-score 异常检测、站点负荷聚类、订单转化曲线和能源峰谷识别。新增数据集仍必须先经过质量报告和清洗，再进入模型；分析服务只读分析产物，不直接写业务 SQLite。模型不可用时返回稳定的 `MODEL_UNAVAILABLE` 降级响应，不影响登录、站点查询、预约、充电和结算。

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
  --output /tmp/ev-s2-final-analysis/ads/dashboard.json

PYTHONPATH=/tmp/ev-s2-site EV_ANALYSIS_ARTIFACT_DIR=/tmp/ev-s2-final-analysis/ads \
  flask --app ml.service.app run --host 127.0.0.1 --port 61501
```

已验证：`pytest ml/tests -q` 为 11 passed；完整分层输出 DWD 25,005 行（有效 24,997）、DWS 11,435 行、ADS 日营收 90 行；Spark 训练 9,160/2,275 行，验证 MAE 55.83、RMSE 74.12；API health/forecast/recommendations/alerts/dashboard snapshot 均可通过 HTTP 返回；Dashboard 快照来自 Schema v0.4 SQLite（14 站点、102 桩），并提供 `analytics` 工作台数据（用户、设备、订单、能源、收益、站点、服务代理指标），不依赖 `demo.json` 作为正常数据源。运行库、Parquet、模型和日志均位于仓库外，不纳入 Git。
