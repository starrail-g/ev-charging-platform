#!/usr/bin/env bash
# One-command stage-2 startup: prepare data/ADS, build Qt binaries when absent,
# then launch the real Socket server, Flask analysis API, Dashboard and (when a
# desktop is available) both Qt clients. Runtime data/logs stay under /tmp.
set -Eeuo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${EV_S2_OUT:-/tmp/ev-s2-present}"
BUILD="${EV_S2_BUILD:-/tmp/ev-s2-final-build}"
RUN_DIR="${EV_S2_RUN_DIR:-/tmp/ev-s2-run}"
CONFIG_FILE="${EV_CONFIG_FILE:-$REPO/config/local.env}"
JOBS="${EV_S2_JOBS:-2}"
START_GUI="${EV_S2_START_GUI:-1}"
DATA_VERSION="stage2-order-funnel-v3-20260915"
FORCE_REFRESH="${EV_S2_REFRESH:-0}"

if [[ -f "$CONFIG_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  source "$CONFIG_FILE"
  set +a
else
  echo "缺少 $CONFIG_FILE。请先复制 config/example.env 并填写三个腾讯 Key。" >&2
  exit 2
fi

if [[ -z "${TENCENT_MAP_KEY:-}" || -z "${TENCENT_STATIC_MAP_KEY:-}" || -z "${TENCENT_MAP_JS_KEY:-}" ]]; then
  echo "TENCENT_MAP_KEY、TENCENT_STATIC_MAP_KEY、TENCENT_MAP_JS_KEY 均必须配置。" >&2
  exit 2
fi
if [[ "$TENCENT_MAP_KEY" != "$TENCENT_STATIC_MAP_KEY" || "$TENCENT_MAP_KEY" != "$TENCENT_MAP_JS_KEY" ]]; then
  echo "三个腾讯 Key 必须使用同一运行时值。" >&2
  exit 2
fi

cd "$REPO"
source "$REPO/scripts/setup_stage2_env.sh" >/tmp/ev-s2-env.log
export PYTHONPATH="${PYTHONPATH:-/tmp/ev-s2-site}"
export PATH="/tmp/ev-node/bin:$PATH"
export EV_SERVER_HOST="${EV_SERVER_HOST:-127.0.0.1}"
export EV_SERVER_PORT="${EV_SERVER_PORT:-45454}"
export EV_ANALYSIS_API_BASE_URL="${EV_ANALYSIS_API_BASE_URL:-http://127.0.0.1:61501}"
export EV_DASHBOARD_API_BASE_URL="${EV_DASHBOARD_API_BASE_URL:-$EV_ANALYSIS_API_BASE_URL}"
export EV_DATABASE_PATH="$OUT/ev-analysis.sqlite"
export EV_SCHEMA_PATH="$REPO/database/schema/schema.sql"
export EV_DATABASE_SEED_PATH="$REPO/database/seeds/dev.sql"
export EV_ADMIN_DATA_SOURCE=socket
mkdir -p "$OUT" "$RUN_DIR" "$BUILD/server" "$BUILD/user" "$BUILD/admin"

DATA_REBUILD=0
if [[ "$FORCE_REFRESH" == "1" || ! -f "$OUT/.stage2-data-version" || "$(<"$OUT/.stage2-data-version")" != "$DATA_VERSION" ]]; then
  DATA_REBUILD=1
  echo "[stage2] 检测到数据版本变化，将重新生成业务库、Spark 分层、模型和 ADS"
fi

if [[ "$DATA_REBUILD" == 1 || ! -f "$OUT/ev-analysis.sqlite" || ! -d "$OUT/ods" ]]; then
  echo "[stage2] 生成 Schema v0.4 分析数据库和 ODS"
  python3 ml/data/generate_analysis_dataset.py \
    --output "$OUT/ev-analysis.sqlite" --ods-dir "$OUT/ods" \
    --seed 20260914 --days 90 --users 120 --stations 12 \
    --piles-per-station 8 --orders 25000 --end-date 2026-09-14
fi

if [[ "$DATA_REBUILD" == 1 || ! -d "$OUT/pipeline/ads/load_features" ]]; then
  echo "[stage2] 执行质量报告和 Spark 分层"
  python3 ml/jobs/quality_report.py --input "$OUT/ods" --output "$OUT/quality.json"
  SPARK_LOCAL_IP=127.0.0.1 "$S2_DEPS_DIR/pyspark/bin/spark-submit" --master local[2] \
    ml/jobs/run_pipeline.py --ods-dir "$OUT/ods" --output "$OUT/pipeline"
fi

if [[ "$DATA_REBUILD" == 1 || ! -d "$OUT/model/spark_model" ]]; then
  echo "[stage2] 训练 Spark MLlib 预测模型"
  SPARK_LOCAL_IP=127.0.0.1 "$S2_DEPS_DIR/pyspark/bin/spark-submit" --master local[2] \
    ml/models/train_forecast.py --features "$OUT/pipeline/ads/load_features" \
    --output "$OUT/model"
fi

if [[ "$DATA_REBUILD" == 1 || ! -f "$OUT/ads/forecast.json" || ! -f "$OUT/ads/dashboard.json" ]]; then
  echo "[stage2] 生成预测、推荐、预警和 Dashboard 快照"
  python3 ml/models/build_outputs.py --features "$OUT/pipeline/ads/load_features" \
    --model "$OUT/model/spark_model" --model-version spark-rf-20260914 \
    --output "$OUT/ads"
  python3 ml/service/build_dashboard_snapshot.py \
    --database "$OUT/ev-analysis.sqlite" --output "$OUT/ads/dashboard.json"
  printf '%s\n' "$DATA_VERSION" > "$OUT/.stage2-data-version"
fi

if [[ ! -x "$BUILD/server/ev-server" ]]; then
  echo "[stage2] qmake6 构建服务端"
  (cd "$BUILD/server" && qmake6 -o Makefile "$REPO/server/server.pro" && make -j"$JOBS")
fi
if [[ ! -x "$BUILD/user/ev-user-client" ]]; then
  echo "[stage2] qmake6 构建用户端"
  (cd "$BUILD/user" && qmake6 -o Makefile "$REPO/apps/user-client/user-client.pro" && make -j"$JOBS")
fi
if [[ ! -x "$BUILD/admin/src/admin-client" ]]; then
  echo "[stage2] qmake6 构建管理端"
  (cd "$BUILD/admin" && qmake6 -o Makefile "$REPO/apps/admin-client/admin-client.pro" && make -j"$JOBS")
fi

PIDS=()
cleanup() {
  trap - TERM INT EXIT
  for pid in "${PIDS[@]:-}"; do
    kill "$pid" 2>/dev/null || true
  done
}
trap cleanup TERM INT EXIT

echo "[stage2] 启动 Socket 服务端、Flask 和 Dashboard"
(
  EV_DATABASE_PATH="$EV_DATABASE_PATH" EV_SCHEMA_PATH="$EV_SCHEMA_PATH" \
  EV_DATABASE_SEED_PATH="$EV_DATABASE_SEED_PATH" EV_SERVER_HOST="$EV_SERVER_HOST" \
  EV_SERVER_PORT="$EV_SERVER_PORT" TENCENT_MAP_KEY="$TENCENT_MAP_KEY" \
  TENCENT_MAP_ENABLED="${TENCENT_MAP_ENABLED:-1}" "$BUILD/server/ev-server"
) >"$RUN_DIR/server.log" 2>&1 & PIDS+=("$!")

(
  EV_ANALYSIS_ARTIFACT_DIR="$OUT/ads" "$S2_DEPS_DIR/bin/flask" \
    --app ml.service.app run --host 127.0.0.1 --port 61501
) >"$RUN_DIR/analysis.log" 2>&1 & PIDS+=("$!")

(
  EV_ANALYSIS_API_BASE_URL="$EV_ANALYSIS_API_BASE_URL" \
  EV_DASHBOARD_API_BASE_URL="$EV_DASHBOARD_API_BASE_URL" \
  python3 dashboard/serve.py --port 61469
) >"$RUN_DIR/dashboard.log" 2>&1 & PIDS+=("$!")

if command -v curl >/dev/null 2>&1; then
  for _ in {1..30}; do
    if curl -fsS "$EV_ANALYSIS_API_BASE_URL/api/v1/analysis/health" >/dev/null 2>&1; then
      break
    fi
    sleep 1
  done
fi

if [[ "$START_GUI" == 1 && ( -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ) ]]; then
  (
    EV_USER_CLIENT_TRANSPORT=socket EV_SERVER_HOST="$EV_SERVER_HOST" \
    EV_SERVER_PORT="$EV_SERVER_PORT" TENCENT_MAP_JS_KEY="$TENCENT_MAP_JS_KEY" \
    "$BUILD/user/ev-user-client"
  ) >"$RUN_DIR/user-client.log" 2>&1 & PIDS+=("$!")
  (
    EV_ADMIN_DATA_SOURCE=socket EV_SERVER_HOST="$EV_SERVER_HOST" \
    EV_SERVER_PORT="$EV_SERVER_PORT" EV_ANALYSIS_API_BASE_URL="$EV_ANALYSIS_API_BASE_URL" \
    TENCENT_STATIC_MAP_KEY="$TENCENT_STATIC_MAP_KEY" "$BUILD/admin/src/admin-client"
  ) >"$RUN_DIR/admin-client.log" 2>&1 & PIDS+=("$!")
  echo "[stage2] Qt 用户端和管理端已启动"
else
  echo "[stage2] 未检测到桌面环境，已跳过 Qt 窗口；设置 EV_S2_START_GUI=1 并在桌面终端运行即可。"
fi

echo "[stage2] Dashboard: http://127.0.0.1:61469/"
echo "[stage2] 日志目录: $RUN_DIR（不含密钥）"
echo "[stage2] 按 Ctrl-C 停止全部进程"
wait
