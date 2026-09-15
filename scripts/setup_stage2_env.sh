#!/usr/bin/env bash
# Source this file to prepare the stage-2 Python/Spark and Node toolchains.
# Dependencies are installed outside the repository and no sudo/system files
# are required.
set -euo pipefail

S2_DEPS_DIR="${S2_DEPS_DIR:-/tmp/ev-s2-site}"
NODE_DIR="${NODE_DIR:-/tmp/ev-node}"
mkdir -p "$S2_DEPS_DIR" "$NODE_DIR"

if [[ ! -f "$S2_DEPS_DIR/.stage2-deps-ready" ]]; then
  python3 -m pip install --index-url https://pypi.tuna.tsinghua.edu.cn/simple \
    --timeout 30 --retries 3 \
    --target "$S2_DEPS_DIR" \
    pyarrow==12.0.1 pandas==2.0.3 numpy==1.24.4 \
    Flask==2.3.3 flask-cors==4.0.0 scikit-learn==1.3.2 pytest==7.4.4
  # pyspark 下载约 317MB，镜像偶发停滞（实测 stall）；此项可失败——start_stage2.sh
  # 会自动回退到系统 Spark（挂载 $SPARK_DIST/python/lib 的 pyspark.zip/py4j），
  # 不让一次性大包拖死裸环境自举。
  if ! python3 -m pip install --index-url https://pypi.tuna.tsinghua.edu.cn/simple \
       --timeout 20 --retries 2 \
       --target "$S2_DEPS_DIR" pyspark==3.3.4; then
    echo "[setup] 提示：pyspark pip 安装未完成（镜像停滞/不可达）；继续，启动时回退系统 Spark。" >&2
  fi
  touch "$S2_DEPS_DIR/.stage2-deps-ready"
fi

if [[ ! -x "$NODE_DIR/bin/node" ]]; then
  curl -fL --retry 3 --connect-timeout 15 --max-time 600 \
    https://npmmirror.com/mirrors/node/v20.18.1/node-v20.18.1-linux-x64.tar.xz \
    -o "$NODE_DIR/node.tar.xz"
  tar -xJf "$NODE_DIR/node.tar.xz" -C "$NODE_DIR" --strip-components=1
fi

export PYTHONPATH="$S2_DEPS_DIR${PYTHONPATH:+:$PYTHONPATH}"
export PATH="$NODE_DIR/bin:$PATH"
export S2_DEPS_DIR NODE_DIR
echo "PYTHONPATH=$PYTHONPATH"
echo "node=$($NODE_DIR/bin/node --version) npm=$($NODE_DIR/bin/npm --version)"
echo "spark-submit=$S2_DEPS_DIR/pyspark/bin/spark-submit"
