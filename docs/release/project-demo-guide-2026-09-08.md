# 项目进度评审演示指南（本机版）

> 适用日期：2026-09-14
>
> 最终呈现原则：用户端和管理端默认走真实 Socket/SQLite；Dashboard 主数据走 Schema v0.4 快照，预测、推荐和预警走 Flask ADS 接口。`demo.json` 仅用于离线故障演练，不作为正常运行数据源。

## 一、演示内容与边界

| 模块 | 明天演示方式 | 当前代码入口 |
|---|---|---|
| 用户端登录、站点、充电桩、预约、充电、结算、个人资料、钱包 | **真实 Socket + SQLite** | `apps/user-client/src/socket_user_service.*` → `server` → `libs/database` |
| 管理员登录、概览、桩/站点/用户页面 | **真实 Socket + SQLite**（`EV_ADMIN_DATA_SOURCE=socket`） | `apps/admin-client/src/data/socketadminrepository.*` → `server` → `libs/database` |
| Web 大屏 | **Schema v0.4 快照 + Flask ADS** | `dashboard/serve.py` → `/api/v1/dashboard/snapshot`、`/api/v1/analysis/*` |
| 智能分析 | **真实生成数据 + Spark 模型 + Flask API** | `ml/` 生成器/分层/模型/服务；Dashboard 读取 Schema v0.4 快照和 ADS 结果 |

## 二阶段最终呈现（真实数据链路）

以下命令使用第一阶段 Schema v0.4 生成业务库，不使用 `dashboard/data/demo.json` 作为主呈现数据。运行产物放在仓库外：

```bash
export REPO="$PWD"
export S2=/tmp/ev-s2-site
export PATH=/tmp/ev-node/bin:$PATH
export PYTHONPATH="$S2"
export OUT=/tmp/ev-s2-present
rm -rf "$OUT" && mkdir -p "$OUT"

python3 ml/data/generate_analysis_dataset.py --output "$OUT/ev-analysis.sqlite" --ods-dir "$OUT/ods" \
  --seed 20260914 --days 90 --users 120 --stations 12 --piles-per-station 8 --orders 25000 --end-date 2026-09-14
python3 ml/jobs/quality_report.py --input "$OUT/ods" --output "$OUT/quality.json"
SPARK_LOCAL_IP=127.0.0.1 "$S2/pyspark/bin/spark-submit" --master local[2] ml/jobs/run_pipeline.py \
  --ods-dir "$OUT/ods" --output "$OUT/pipeline"
SPARK_LOCAL_IP=127.0.0.1 "$S2/pyspark/bin/spark-submit" --master local[2] ml/models/train_forecast.py \
  --features "$OUT/pipeline/ads/load_features" --output "$OUT/model"
python3 ml/models/build_outputs.py --features "$OUT/pipeline/ads/load_features" \
  --model "$OUT/model/spark_model" --model-version spark-rf-20260914 --output "$OUT/ads"
python3 ml/service/build_dashboard_snapshot.py --database "$OUT/ev-analysis.sqlite" --output "$OUT/ads/dashboard.json"
```

终端 1 启动分析服务：

```bash
EV_ANALYSIS_ARTIFACT_DIR="$OUT/ads" "$S2/bin/flask" --app ml.service.app run --host 127.0.0.1 --port 61501
```

终端 2 启动 Dashboard：

```bash
EV_ANALYSIS_API_BASE_URL=http://127.0.0.1:61501 \
EV_DASHBOARD_API_BASE_URL=http://127.0.0.1:61501 \
python3 dashboard/serve.py --port 61469
```

浏览器打开 `http://127.0.0.1:61469/`。页面主地图、站点和桩来自 `/api/v1/dashboard/snapshot`，预测/推荐/预警来自 Flask ADS 接口；若分析服务停止，页面保留基础业务快照并显示“不可用”状态。

代码解释：用户端通过 `EV_USER_CLIENT_TRANSPORT=socket` 选择 `SocketUserService`，管理端通过 `EV_ADMIN_DATA_SOURCE=socket` 选择 `SocketAdminRepository`；两者都使用协议 v1 TCP 长度帧和 JSON 信封，把请求发给 `server`，由服务端调用 SQLite 数据库层完成事务。管理端 Mock 仅保留为离线故障演练入口。

## 二、演示前一次性准备

以下命令按 Ubuntu/Linux 编写，仓库目录记为 `$REPO`。构建产物放在仓库外的 `../build`，避免污染 Git 工作区。

```bash
cd /path/to/ev-charging-platform
export REPO="$PWD"
export BUILD="$REPO/../build/ev-charging-demo"
mkdir -p "$BUILD/server" "$BUILD/user-client" "$BUILD/admin-client"
```

确认工具链：

```bash
qmake6 --version
python3 --version
```

预期：`qmake6` 使用 Qt 6.2.x 或更高版本。若 `qmake6` 不存在，不要临时改用 CMake；先切换到已经安装 Qt 的 Ubuntu/VM 环境。

## 三、构建三个可演示程序

### 图形界面入口说明

服务端 `ev-server` 是后台终端程序，没有图形窗口；用户端和管理端都是 Qt Widgets 图形界面，启动后会直接打开窗口：

- 用户端：运行 `$BUILD/user-client/ev-user-client`，首先进入“充电用户端”手机号登录页；登录成功后从底部“首页 / 充电 / 我的”进入站点查询、订单充电和个人钱包。
- 管理端：运行 `$BUILD/admin-client/src/admin-client`，首先进入“管理员登录”页；使用 `admin / 123456` 登录后进入“概览”，左侧导航可进入“充电桩”“充电站”“用户管理”。

如果是在 Ubuntu 桌面环境中，命令执行后窗口会出现在当前桌面；如果是在无桌面的 SSH/纯终端环境中，Qt 窗口无法显示，应改用带桌面的本机或虚拟机。`QT_QPA_PLATFORM=offscreen` 只用于自动化测试，不用于现场 GUI 演示。

### 1. 构建服务端

```bash
cd "$BUILD/server"
qmake6 -o Makefile "$REPO/server/server.pro"
make -j"$(nproc)"
```

代码逻辑：`server/server.pro` 把 `server/src/main.cpp`、`libs/protocol` 和 `libs/database` 编译成 `ev-server`。服务端负责 TCP 连接、协议分发、管理员会话、参数校验和数据库调用；Qt 客户端不直接打开 SQLite。

### 2. 构建用户端 Socket 客户端

```bash
cd "$BUILD/user-client"
qmake6 -o Makefile "$REPO/apps/user-client/user-client.pro"
make -j"$(nproc)"
```

代码逻辑：`apps/user-client/src/main.cpp` 仍是 Qt Widgets 界面；`IUserService` 隔离页面和数据源。设置 Socket 环境变量后，页面调用会进入 `SocketUserService`，网络等待在 `QtConcurrent` 工作线程中执行，结果通过 `QFutureWatcher` 回到 GUI 线程。

### 3. 构建管理端客户端

```bash
cd "$BUILD/admin-client"
qmake6 -o Makefile "$REPO/apps/admin-client/admin-client.pro"
make -j"$(nproc)"
```

代码逻辑：`MainWindow` 只依赖 `AdminRepository`。默认启动命令显式设置 `EV_ADMIN_DATA_SOURCE=socket` 后使用 `SocketAdminRepository` 访问刚才启动的 Socket 服务；未设置时才进入 Mock 离线演练。

## 四、启动服务端（终端 1）

先创建一份干净的演示数据库：

```bash
export DEMO_DB="$BUILD/ev-charging-demo.sqlite"
rm -f "$DEMO_DB"
EV_DATABASE_PATH="$DEMO_DB" \
EV_DATABASE_SEED_PATH="$REPO/database/seeds/dev.sql" \
EV_SCHEMA_PATH="$REPO/database/schema/schema.sql" \
EV_SERVER_HOST=127.0.0.1 EV_SERVER_PORT=45454 \
"$BUILD/server/ev-server"
```

看到 `ev-server listening on "127.0.0.1" 45454` 后保持终端运行。

代码逻辑：服务端第一次打开数据库时加载 Schema v0.4 和确定性种子数据。种子包含站点、空闲/预约/充电/故障/离线桩，以及可用于验证的用户和订单。写操作由数据库层用事务保护，金额统一为整数分。

### 观察 Socket 日志

用户端演示时不要关闭服务端终端（终端 1），直接观察滚动日志即可。服务端当前会输出：

```text
ev-server listening on "127.0.0.1" 45454
request "user-..." "user.login"
request "user-..." "station.list"
request "user-..." "pile.list"
request "user-..." "reservation.create"
request "user-..." "charging.start"
request "user-..." "charging.stop"
request "user-..." "charging.settle"
```

每行的第一个字段是协议请求 ID，第二个字段是操作类型；它可以用来证明用户端确实经过 Socket 到达服务端。当前用户端每个请求都会新建一个 TCP 连接，收到响应后立即关闭，因此服务端可能紧跟着打印 `socket error "The remote host closed the connection"`；如果客户端操作已成功，这属于正常短连接回收提示，不是业务错误。真正需要关注的是 `Connection refused`、协议帧错误，或客户端同时显示请求失败。当前日志只记录请求类型/ID和连接错误，不输出密码、管理员 Token、钱包金额明细或 SQL；详细业务结果应以客户端界面和数据库状态为准。

如需保存本次演示日志，可在终端 1 用以下方式启动（仍然会实时显示）：

```bash
EV_DATABASE_PATH="$DEMO_DB" \
EV_DATABASE_SEED_PATH="$REPO/database/seeds/dev.sql" \
EV_SCHEMA_PATH="$REPO/database/schema/schema.sql" \
EV_SERVER_HOST=127.0.0.1 EV_SERVER_PORT=45454 \
"$BUILD/server/ev-server" 2>&1 | tee "$BUILD/server/demo-server.log"
```

演示结束后不要把 `demo-server.log` 或演示数据库复制进 Git；它们属于本地过程材料。

演示数据库账号：

- 用户端可用任意 11 位数字手机号；推荐 `13800000000`，首次登录会自动创建。
- 若需要观察种子数据，可使用 `13800138000`。
- 管理端 Mock 登录：账号 `admin`，密码 `123456`。

## 五、用户端真实 Socket 演示（终端 2）

```bash
cd "$BUILD/user-client"
EV_USER_CLIENT_TRANSPORT=socket \
EV_SERVER_HOST=127.0.0.1 EV_SERVER_PORT=45454 \
./ev-user-client
```

### 推荐操作顺序

1. **登录**

   输入 `13800000000`，点击“登录”。

   讲解：客户端发送 `user.login`；服务端在事务中查询或创建用户，返回用户 ID、状态和整数分余额。这里不是 Mock 登录。

2. **站点和充电桩查询**

   在首页点击“查询”，打开一个站点查看桩列表（推荐选择包含 A-01 的软件园一号站，先不要点击桩）。

   讲解：页面依次调用 `station.list` 和 `pile.list`。`SocketUserService` 将协议字段映射为 UI DTO；页面不含 SQL，也不拼接协议 JSON。

3. **充值**

   进入“我的”，充值 `100.00` 元。

   讲解：请求是 `wallet.recharge`，服务端在一个事务中更新余额并追加钱包流水；充值金额在网络和数据库中都是 `amount_cents=10000`。

4. **创建预约并确认**

   回到首页进入站点详情，点击 A-01 进入充电页，再点击“预约该充电桩”，然后点击“确认预约”。

   讲解：第一次请求创建 `pending_reservation` 订单并把桩置为 `reserved`；确认请求只推进订单到 `reserved`。如果现场模拟重复点击，客户端会复用同一个请求 ID，服务端不会重复创建订单。

5. **开始充电**

   点击“开始充电”。

   讲解：`charging.start` 在同一事务中把订单和桩都变为 `charging`。预约启动传 `order_id`；直接启动则传 `pile_id`，两种参数不会同时发送。

6. **停止充电**

   点击“停止充电”。

   讲解：`charging.stop` 计算本次金额，将订单变为 `pending_settlement`，同时立即释放充电桩；此时尚未扣钱包余额。

7. **结算**

   点击“结算”。

   讲解：`charging.settle` 检查余额，在一个事务中写入一条负的 `charge` 流水、扣除余额、完成订单并更新充电桩累计次数。重复结算会得到稳定的业务错误或幂等响应，不会重复扣款。

8. **历史记录**

   点击底部“充电”页，展示已完成订单和消费汇总。

   讲解：历史来自 `order.history.list`，服务端只返回已完成订单并按结算时间倒序；不是界面本地拼出的假记录。

### 可选异常演示

- 登录输入 `123`：展示手机号校验，不会发 Socket 请求。
- 关闭终端 1 后点击查询：展示连接失败/超时提示；重新启动服务端后可重试。
- 选择非空闲桩：展示服务端状态冲突，不会创建订单。
- 充值不足后结算：展示 `1202 INSUFFICIENT_BALANCE`，订单保持待结算状态。

## 六、管理端真实 Socket 呈现（终端 3）

```bash
cd "$BUILD/admin-client"
EV_ADMIN_DATA_SOURCE=socket \
EV_SERVER_HOST=127.0.0.1 EV_SERVER_PORT=45454 \
EV_ANALYSIS_API_BASE_URL=http://127.0.0.1:61501 \
./src/admin-client
```

`EV_ADMIN_DATA_SOURCE=socket` 让管理端通过终端 1 的真实 Socket 服务读取 Schema v0.4 数据；分析摘要通过终端 2 的 Flask 服务读取预测结果。

### 推荐操作顺序

1. 使用 `admin / 123456` 登录。
2. 概览页展示总桩数、可用率、利用率、近 7 日营收、故障/离线关注列表和站点拓扑。
3. 概览中的“智能预测与调度”显示未来 1 小时峰值、站点和模型版本；分析服务异常时显示“暂不可用”，不影响基础管理功能。
4. 打开“充电桩”，用状态筛选查看空闲、预约、充电中、故障、离线。
5. 打开“充电站”和“用户管理”，展示表格字段和状态标签。

讲解：页面调用 `AdminRepository` 的异步方法，Socket 实现通过协议 v1 访问服务端；页面不直接打开 SQLite。若需演练空态/错误态，可清除 `EV_ADMIN_DATA_SOURCE` 后重新启动管理端，使用 Mock 数据模式。

## 七、Dashboard 演示（终端 4）

```bash
cd "$REPO"
python3 dashboard/serve.py --port 61469
```

浏览器打开：<http://127.0.0.1:61469/>

讲解：大屏从 Flask `/api/v1/dashboard/snapshot` 加载经过校验的 Schema v0.4 主数据，经 `data-adapter.js` 校验整数金额、坐标和桩状态，再由 `app.js` 组装指标、告警、ECharts 图表和地图；预测、推荐和预警来自 `/api/v1/analysis/*`。未配置腾讯 JS Key 时自动使用离线拓扑，不影响业务数据呈现。

可现场展示两个状态：

- <http://127.0.0.1:61469/?map=topology>：强制离线拓扑图。
- <http://127.0.0.1:61469/?state=offline>：展示离线状态横幅和保留快照。

## 八、现场最小检查清单

- [ ] 服务端终端保持显示 `listening on 127.0.0.1:45454`
- [ ] 用户端启动命令包含 `EV_USER_CLIENT_TRANSPORT=socket`
- [ ] 用户端能完成：登录 → 查询 → 充值 → 预约 → 确认 → 开始 → 停止 → 结算
- [ ] 管理端设置 `EV_ADMIN_DATA_SOURCE=socket`，能读取服务端真实数据
- [ ] Dashboard 能打开，Schema 快照图表和拓扑图非空，分析摘要显示已更新或明确降级
- [ ] 不展示任何真实 Tencent Key、数据库文件、构建目录或调试日志

## 九、现场故障处理

| 现象 | 处理 |
|---|---|
| 用户端提示服务连接失败 | 检查终端 1 是否仍运行、端口是否为 45454；确认用户端命令包含 Socket 环境变量 |
| 用户端显示已有未完成订单 | 进入充电页先完成停止/结算，或换一个全新手机号重新演示 |
| 结算提示余额不足 | 在“我的”充值后重试；不要直接改数据库 |
| 管理端没有服务端数据 | 这是当前预期，说明管理端 Socket 适配器尚未实现，继续按 Mock 展示 |
| Dashboard 空白 | 确认通过 `python3 dashboard/serve.py` 启动，不要直接双击 HTML；再访问 `?map=topology` |
