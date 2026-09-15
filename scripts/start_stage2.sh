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
DATA_VERSION="stage2-mlfix-g2-v4-20260916"
# 代码指纹：ml 链关键脚本任何变化都自动触发重建（避免"版本号忘记改 → 旧预测/旧利用率继续展示"）。
ML_FINGERPRINT="$(cat "$REPO/ml/jobs/run_pipeline.py" "$REPO/ml/models/build_outputs.py" \
  "$REPO/ml/models/train_forecast.py" "$REPO/ml/service/build_dashboard_snapshot.py" 2>/dev/null \
  | sha256sum | cut -c1-12 || echo no-fingerprint)"
DATA_VERSION="$DATA_VERSION-$ML_FINGERPRINT"
FORCE_REFRESH="${EV_S2_REFRESH:-0}"

if [[ -f "$CONFIG_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  source "$CONFIG_FILE"
  set +a
else
  echo "[stage2] 未找到 $CONFIG_FILE（可复制 config/example.env 后填写）；继续以空 Key 启动，地图相关功能将降级。" >&2
fi

# 三个腾讯 Key 授权类型不同（服务端 WebService / 管理端静态图 WebService / 用户端 JS GL），
# 允许缺失或使用不同值：缺失只告警不中断（单跑不误伤），相应地图功能自动降级。
for name in TENCENT_MAP_KEY TENCENT_STATIC_MAP_KEY TENCENT_MAP_JS_KEY; do
  if [[ -z "${!name:-}" ]]; then
    echo "[stage2] 提示：$name 未配置（演示前请在 $CONFIG_FILE 填写，相关地图功能只降级不中断）。" >&2
  fi
done
if [[ -n "${TENCENT_MAP_KEY:-}" && -n "${TENCENT_STATIC_MAP_KEY:-}" && -n "${TENCENT_MAP_JS_KEY:-}" ]] \
   && [[ "$TENCENT_MAP_KEY" != "$TENCENT_STATIC_MAP_KEY" || "$TENCENT_MAP_KEY" != "$TENCENT_MAP_JS_KEY" ]]; then
  echo "[stage2] 提示：三个腾讯 Key 取值不同。不同授权类型的 Key 允许不同（旧脚本要求同值为历史前提，已作废）。" >&2
fi
# 未配置服务端 WebService Key 时默认关闭在线地图，避免空 Key 反复失败（可用 TENCENT_MAP_ENABLED 显式覆盖）。
if [[ -z "${TENCENT_MAP_KEY:-}" && -z "${TENCENT_MAP_ENABLED:-}" ]]; then
  TENCENT_MAP_ENABLED=0
fi

cd "$REPO"
source "$REPO/scripts/setup_stage2_env.sh" >/tmp/ev-s2-env.log
export PYTHONPATH="${PYTHONPATH:-/tmp/ev-s2-site}"
export PATH="/tmp/ev-node/bin:$PATH"
export EV_SERVER_HOST="${EV_SERVER_HOST:-127.0.0.1}"
export EV_SERVER_PORT="${EV_SERVER_PORT:-45454}"
# 各服务端口：默认与历史演示一致；EV_*_PORT 可覆盖（并行实例 / 验收场景互不打扰）。
export EV_ANALYSIS_API_PORT="${EV_ANALYSIS_API_PORT:-61501}"
export EV_ANALYTICS_API_PORT="${EV_ANALYTICS_API_PORT:-61470}"
export EV_DASHBOARD_PORT="${EV_DASHBOARD_PORT:-61469}"
export EV_ANALYSIS_API_BASE_URL="${EV_ANALYSIS_API_BASE_URL:-http://127.0.0.1:$EV_ANALYSIS_API_PORT}"
export EV_DASHBOARD_API_BASE_URL="${EV_DASHBOARD_API_BASE_URL:-$EV_ANALYSIS_API_BASE_URL}"
# 分析模式（?source=analytics）由 dashboard/serve.py 反向代理到 analytics 快照服务（同源接线）。
export EV_ANALYTICS_API_BASE_URL="${EV_ANALYTICS_API_BASE_URL:-http://127.0.0.1:$EV_ANALYTICS_API_PORT}"
export EV_DATABASE_PATH="$OUT/ev-analysis.sqlite"
export EV_SCHEMA_PATH="$REPO/database/schema/schema.sql"
export EV_DATABASE_SEED_PATH="$REPO/database/seeds/dev.sql"
export EV_ADMIN_DATA_SOURCE=socket
mkdir -p "$OUT" "$RUN_DIR" "$BUILD/server" "$BUILD/user" "$BUILD/admin"

# Java：spark-submit 依赖 JAVA_HOME。非交互环境（ssh/nohup/服务）不加载 /etc/profile，
# 这里按课程 VM 常见安装位置与 PATH 中的 java 自动回退，保证各启动方式下都能跑。
if [[ -z "${JAVA_HOME:-}" ]]; then
  for candidate in /opt/module/jdk1.8.0_261 /usr/lib/jvm/java-8-openjdk-amd64 /usr/lib/jvm/default-java; do
    if [[ -x "$candidate/bin/java" ]]; then
      export JAVA_HOME="$candidate"
      break
    fi
  done
fi
if [[ -z "${JAVA_HOME:-}" ]] && command -v java >/dev/null 2>&1; then
  export JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
fi
if [[ -n "${JAVA_HOME:-}" ]]; then
  echo "[stage2] JAVA_HOME=$JAVA_HOME"
else
  echo "[stage2] 警告：未找到 Java（JAVA_HOME 未设置且常见位置缺失）；Spark 步骤可能失败。" >&2
fi

# spark-submit 解析：优先 pip 依赖目录（$S2_DEPS_DIR/pyspark），其次系统 Spark（$SPARK_HOME →
# 课程 VM 安装位置 → PATH）；非交互环境可能三者皆缺，故保留完整回退链。
SPARK_SUBMIT="${EV_S2_SPARK_SUBMIT:-}"
if [[ -z "$SPARK_SUBMIT" ]]; then
  if [[ -x "$S2_DEPS_DIR/pyspark/bin/spark-submit" ]]; then
    SPARK_SUBMIT="$S2_DEPS_DIR/pyspark/bin/spark-submit"
  elif [[ -n "${SPARK_HOME:-}" && -x "$SPARK_HOME/bin/spark-submit" ]]; then
    SPARK_SUBMIT="$SPARK_HOME/bin/spark-submit"
  elif [[ -x /opt/module/spark-3.4.1/bin/spark-submit ]]; then
    SPARK_SUBMIT="/opt/module/spark-3.4.1/bin/spark-submit"
  else
    SPARK_SUBMIT="$(command -v spark-submit || true)"
  fi
fi
if [[ -z "$SPARK_SUBMIT" ]]; then
  echo "[stage2] 未找到 spark-submit（pip 依赖目录与系统 Spark 均无）；请先运行 scripts/setup_stage2_env.sh 或设置 EV_S2_SPARK_SUBMIT。" >&2
  exit 2
fi
echo "[stage2] spark-submit: $SPARK_SUBMIT"

# python 侧步骤（build_outputs.py 需要 `import pyspark`）依赖 PYTHONPATH 可导入 pyspark/py4j：
# pip 依赖目录不可用时，从所解析到的 Spark 发行版补挂 python/lib（与本地模式工作流约定一致）。
if [[ ! -e "$S2_DEPS_DIR/pyspark" ]]; then
  SPARK_DIST="$(dirname "$(dirname "$(readlink -f "$SPARK_SUBMIT")")")"
  PY4J_ZIP="$(ls "$SPARK_DIST"/python/lib/py4j-*-src.zip 2>/dev/null | head -n 1)"
  if [[ -f "$SPARK_DIST/python/lib/pyspark.zip" ]]; then
    export PYTHONPATH="$SPARK_DIST/python/lib/pyspark.zip${PY4J_ZIP:+:$PY4J_ZIP}${PYTHONPATH:+:$PYTHONPATH}"
    export SPARK_HOME="$SPARK_DIST"
    echo "[stage2] python 侧 pyspark/py4j: $SPARK_DIST/python/lib（SPARK_HOME 同步）"
  fi
fi

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
  SPARK_LOCAL_IP=127.0.0.1 "$SPARK_SUBMIT" --master local[2] \
    ml/jobs/run_pipeline.py --ods-dir "$OUT/ods" --output "$OUT/pipeline"
fi

if [[ "$DATA_REBUILD" == 1 || ! -d "$OUT/model/spark_model" ]]; then
  echo "[stage2] 训练 Spark MLlib 预测模型"
  SPARK_LOCAL_IP=127.0.0.1 "$SPARK_SUBMIT" --master local[2] \
    ml/models/train_forecast.py --features "$OUT/pipeline/ads/load_features" \
    --output "$OUT/model"
fi

if [[ "$DATA_REBUILD" == 1 || ! -f "$OUT/ads/forecast.json" || ! -f "$OUT/ads/dashboard.json" ]]; then
  echo "[stage2] 生成预测、推荐、预警和 Dashboard 快照"
  python3 ml/models/build_outputs.py --features "$OUT/pipeline/ads/load_features" \
    --model "$OUT/model/spark_model" --model-version spark-rf-20260914 \
    --output "$OUT/ads"
  python3 ml/service/build_dashboard_snapshot.py \
    --database "$OUT/ev-analysis.sqlite" --dws "$OUT/pipeline/dws" --output "$OUT/ads/dashboard.json"
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
  EV_SERVER_PORT="$EV_SERVER_PORT" TENCENT_MAP_KEY="${TENCENT_MAP_KEY:-}" \
  TENCENT_MAP_ENABLED="${TENCENT_MAP_ENABLED:-1}" "$BUILD/server/ev-server"
) >"$RUN_DIR/server.log" 2>&1 & PIDS+=("$!")

(
  EV_ANALYSIS_ARTIFACT_DIR="$OUT/ads" "$S2_DEPS_DIR/bin/flask" \
    --app ml.service.app run --host 127.0.0.1 --port "$EV_ANALYSIS_API_PORT"
) >"$RUN_DIR/analysis.log" 2>&1 & PIDS+=("$!")

(
  EV_ANALYTICS_ROOT="${EV_ANALYTICS_ROOT:-$HOME/ev-stage2-artifacts}" \
    python3 -m flask --app 'analytics.api.app:create_app()' run --host 127.0.0.1 --port "$EV_ANALYTICS_API_PORT"
) >"$RUN_DIR/analytics.log" 2>&1 & PIDS+=("$!")

(
  EV_ANALYSIS_API_BASE_URL="$EV_ANALYSIS_API_BASE_URL" \
  EV_DASHBOARD_API_BASE_URL="$EV_DASHBOARD_API_BASE_URL" \
  EV_ANALYTICS_API_BASE_URL="$EV_ANALYTICS_API_BASE_URL" \
  python3 dashboard/serve.py --port "$EV_DASHBOARD_PORT"
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
    EV_SERVER_PORT="$EV_SERVER_PORT" TENCENT_MAP_JS_KEY="${TENCENT_MAP_JS_KEY:-}" \
    "$BUILD/user/ev-user-client"
  ) >"$RUN_DIR/user-client.log" 2>&1 & PIDS+=("$!")
  (
    EV_ADMIN_DATA_SOURCE=socket EV_SERVER_HOST="$EV_SERVER_HOST" \
    EV_SERVER_PORT="$EV_SERVER_PORT" EV_ANALYSIS_API_BASE_URL="$EV_ANALYSIS_API_BASE_URL" \
    TENCENT_STATIC_MAP_KEY="${TENCENT_STATIC_MAP_KEY:-}" "$BUILD/admin/src/admin-client"
  ) >"$RUN_DIR/admin-client.log" 2>&1 & PIDS+=("$!")
  echo "[stage2] Qt 用户端和管理端已启动"
else
  echo "[stage2] 未检测到桌面环境，已跳过 Qt 窗口；设置 EV_S2_START_GUI=1 并在桌面终端运行即可。"
fi

echo "[stage2] Dashboard: http://127.0.0.1:$EV_DASHBOARD_PORT/"
echo "[stage2] 分析模式（同源代理）: http://127.0.0.1:$EV_DASHBOARD_PORT/?source=analytics"
echo "[stage2] 日志目录: $RUN_DIR（不含密钥）"
echo "[stage2] 按 Ctrl-C 停止全部进程"
wait
