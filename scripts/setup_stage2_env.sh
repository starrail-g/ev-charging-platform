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
    --target "$S2_DEPS_DIR" \
    pyspark==3.3.4 pyarrow==12.0.1 pandas==2.0.3 numpy==1.24.4 \
    Flask==2.3.3 flask-cors==4.0.0 scikit-learn==1.3.2 pytest==7.4.4
  touch "$S2_DEPS_DIR/.stage2-deps-ready"
fi

if [[ ! -x "$NODE_DIR/bin/node" ]]; then
  curl -fL --retry 3 \
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
