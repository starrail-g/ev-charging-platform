#!/usr/bin/env bash
# 第二阶段一键批次流水线（VM 上运行; 需已 source /etc/profile 使 spark-submit/hdfs 可用）。
#
# 用法:
#   bash scripts/stage2/run_pipeline.sh --profile smoke --seed 20260914 --batch-id s2-smoke-20260915
#   bash scripts/stage2/run_pipeline.sh --profile standard --seed 20260914 --batch-id s2-standard-20260915 --verify-rerun
#
# 步骤: generate → ODS 上传 HDFS → profile(before) → clean(ODS→DWD) →
#       profile(after) → warehouse(DWS/ADS) → export(原子发布 latest)
# 规则: 默认拒绝覆盖已存在批次目录; --verify-rerun 用独立临时目录重跑并比较逻辑结果,
#       不删除、不触碰既有发布。
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EV_ANALYTICS_ROOT="${EV_ANALYTICS_ROOT:-$HOME/ev-stage2-artifacts}"
EV_HDFS_ROOT="${EV_HDFS_ROOT:-/ev-stage2}"
SPARK_SUBMIT="${SPARK_SUBMIT:-spark-submit}"
PYTHON="${PYTHON:-python3}"

# export_ads.py 等步骤以普通 python3 运行（非 spark-submit），需要 pyspark 可导入：
# 非登录环境（ssh/nohup/服务）不加载 /etc/profile、不带 PYTHONPATH —— 与 start_stage2.sh 同规则
# 从 Spark 发行版自动补挂，避免整链跑到最后一步才失败（2026-09-16 实测：漏挂时 step7
# 报 ModuleNotFoundError: pyspark，而前面 spark-submit 步骤全部正常）。
if ! "$PYTHON" -c 'import pyspark' >/dev/null 2>&1; then
  for candidate in "${SPARK_HOME:-/nonexistent}" /opt/module/spark-3.4.1; do
    if [ -f "$candidate/python/lib/pyspark.zip" ]; then
      PYSITE=("$candidate/python/lib/pyspark.zip" "$candidate"/python/lib/py4j-*-src.zip)
      export PYTHONPATH="$(IFS=:; echo "${PYSITE[*]}"):${PYTHONPATH:-}"
      echo "[env] pyspark not importable — prepended $candidate/python/lib to PYTHONPATH"
      break
    fi
  done
fi

PROFILE=""
SEED="20260914"
BATCH_ID=""
VERIFY_RERUN=0
while [ $# -gt 0 ]; do
  case "$1" in
    --profile) PROFILE="$2"; shift 2 ;;
    --seed) SEED="$2"; shift 2 ;;
    --batch-id) BATCH_ID="$2"; shift 2 ;;
    --verify-rerun) VERIFY_RERUN=1; shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done
if [ -z "$PROFILE" ] || [ -z "$BATCH_ID" ]; then
  echo "usage: run_pipeline.sh --profile <smoke|standard|low> --seed N --batch-id ID [--verify-rerun]" >&2
  exit 2
fi

BATCH="$EV_ANALYTICS_ROOT/batches/$BATCH_ID"
HDFS_BATCH="$EV_HDFS_ROOT/batches/$BATCH_ID"
# locality.wait=0: 本 VM 本地模式下 HDFS 块位置主机（master/127.0.1.1）与本地执行器主机
# （192.168.182.128）不匹配时，延迟调度让任务永久排队（SPARK-42923 征兆）——实测卡死 15 分钟零进展。
SPARK_CONF=(--master "local[2]" --conf spark.sql.session.timeZone=UTC --conf spark.sql.shuffle.partitions=4 \
            --conf spark.locality.wait=0 --conf spark.locality.wait.node=0 \
            --conf spark.locality.wait.rack=0 --conf spark.locality.wait.process=0)

if [ -e "$BATCH" ]; then
  echo "ERROR: batch dir already exists: $BATCH (默认拒绝覆盖; 请换 batch-id)" >&2
  exit 3
fi
mkdir -p "$BATCH/input" "$BATCH/quality" "$BATCH/logs"

echo "== [1/7] generate (profile=$PROFILE seed=$SEED) =="
"$PYTHON" "$REPO/analytics/generate_data.py" --profile "$PROFILE" --seed "$SEED" \
  --batch-id "$BATCH_ID" --out-root "$EV_ANALYTICS_ROOT" --repo-root "$REPO" \
  | tee "$BATCH/logs/generate.log"

echo "== [2/7] upload ODS -> HDFS $HDFS_BATCH =="
hdfs dfs -mkdir -p "$HDFS_BATCH/ods" "$HDFS_BATCH/dwd" "$HDFS_BATCH/dws" \
                  "$HDFS_BATCH/ads" "$HDFS_BATCH/quarantine"
hdfs dfs -put -f "$BATCH/input/"*.csv "$HDFS_BATCH/ods/" 2>&1 | tail -1 || true
hdfs dfs -ls "$HDFS_BATCH/ods" | tail -6

echo "== [3/7] profile before (ODS) =="
"$SPARK_SUBMIT" "${SPARK_CONF[@]}" "$REPO/analytics/jobs/quality_profile.py" --stage before \
  --input "hdfs://$HDFS_BATCH/ods" --out "$BATCH/quality/profile_before.json" \
  --batch-id "$BATCH_ID" > "$BATCH/logs/profile_before.log" 2>&1
tail -2 "$BATCH/logs/profile_before.log"

echo "== [4/7] clean (ODS -> DWD/quarantine) =="
"$SPARK_SUBMIT" "${SPARK_CONF[@]}" "$REPO/analytics/jobs/clean.py" \
  --input "hdfs://$HDFS_BATCH/ods" --output "hdfs://$HDFS_BATCH/dwd" \
  --quarantine "hdfs://$HDFS_BATCH/quarantine" \
  --report "$BATCH/quality/clean_report.json" --batch-id "$BATCH_ID" \
  > "$BATCH/logs/clean.log" 2>&1
grep -h "^\[" "$BATCH/logs/clean.log" || tail -5 "$BATCH/logs/clean.log"

echo "== [5/7] profile after (DWD) =="
"$SPARK_SUBMIT" "${SPARK_CONF[@]}" "$REPO/analytics/jobs/quality_profile.py" --stage after \
  --dwd "hdfs://$HDFS_BATCH/dwd" --out "$BATCH/quality/profile_after.json" \
  --batch-id "$BATCH_ID" > "$BATCH/logs/profile_after.log" 2>&1
tail -2 "$BATCH/logs/profile_after.log"

echo "== [6/7] warehouse (DWS/ADS) =="
"$SPARK_SUBMIT" "${SPARK_CONF[@]}" "$REPO/analytics/jobs/build_warehouse.py" \
  --dwd "hdfs://$HDFS_BATCH/dwd" --output "hdfs://$HDFS_BATCH" \
  --manifest "$BATCH/manifest.json" --clean-report "$BATCH/quality/clean_report.json" \
  --batch-id "$BATCH_ID" > "$BATCH/logs/warehouse.log" 2>&1
grep -h "^ADS " "$BATCH/logs/warehouse.log" || tail -8 "$BATCH/logs/warehouse.log"

echo "== [7/7] export + 原子发布 =="
"$PYTHON" "$REPO/analytics/export_ads.py" --analytics-root "$EV_ANALYTICS_ROOT" \
  --batch-id "$BATCH_ID" --ads-uri "hdfs://$HDFS_BATCH/ads" | tee "$BATCH/logs/export.log"

if [ "$VERIFY_RERUN" = "1" ]; then
  echo "== [verify-rerun] 同 seed 独立临时目录重跑（不改动既有发布） =="
  RERUN_ROOT="$EV_ANALYTICS_ROOT/tmp/$BATCH_ID-rerun"
  if [ -e "$RERUN_ROOT" ]; then
    echo "ERROR: rerun dir exists: $RERUN_ROOT" >&2; exit 3
  fi
  mkdir -p "$RERUN_ROOT"
  "$PYTHON" "$REPO/analytics/generate_data.py" --profile "$PROFILE" --seed "$SEED" \
    --batch-id "$BATCH_ID" --out-root "$RERUN_ROOT" --repo-root "$REPO" \
    > "$RERUN_ROOT/generate.log" 2>&1
  RERUN_BATCH="$RERUN_ROOT/batches/$BATCH_ID"
  DIFFS=0
  for f in users.csv stations.csv charging_piles.csv charging_orders.csv wallet_transactions.csv dirty_labels.jsonl; do
    if cmp -s "$BATCH/input/$f" "$RERUN_BATCH/input/$f" && [ -f "$BATCH/input/$f" ]; then
      echo "  IDENTICAL $f"
    else
      echo "  DIFFERS   $f"
      DIFFS=$((DIFFS+1))
    fi
  done
  "$SPARK_SUBMIT" "${SPARK_CONF[@]}" "$REPO/analytics/jobs/clean.py" \
    --input "file://$RERUN_BATCH/input" --output "file://$RERUN_BATCH/dwd" \
    --quarantine "file://$RERUN_BATCH/quarantine" \
    --report "$RERUN_BATCH/clean_report.json" --batch-id "$BATCH_ID" \
    > "$RERUN_ROOT/clean.log" 2>&1
  "$PYTHON" - "$BATCH/quality/clean_report.json" "$RERUN_BATCH/clean_report.json" <<'PYEOF'
import json, sys
a = json.load(open(sys.argv[1], encoding="utf-8"))["tables"]
b = json.load(open(sys.argv[2], encoding="utf-8"))["tables"]
same = json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)
print("RERUN_CLEAN_REPORTS_EQUAL" if same else "RERUN_CLEAN_REPORTS_DIFFER")
sys.exit(0 if same else 1)
PYEOF
  if [ "$DIFFS" = "0" ]; then
    echo "VERIFY_RERUN_PASS (inputs byte-identical, clean logic equal, 旧发布未触碰)"
  else
    echo "VERIFY_RERUN_FAIL (input diffs=$DIFFS)" >&2
    exit 1
  fi
fi

echo "PIPELINE_DONE batch=$BATCH_ID dir=$BATCH"
