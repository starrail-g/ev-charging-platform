# 服务端代码评审讲解稿

> **历史稿标注（2026-09-08）**：本文档记录 2026-09-08 服务端代码评审时的讲解口径；此后管理端
> Socket 适配器已随 PR #11/#13 合入 `main` 并验证（`EV_ADMIN_DATA_SOURCE=socket` 为最终演示路径），
> Schema 已由 v0.3 升级为 v0.4（PR #19）。文内 §1 与 §2 两处已就地更新为当前状态，
> 其余讲解结构（启动流程、协议、事务、测试）仍适用。

## 1. 先用一句话说明服务端职责

服务端是 Qt/C++ 编写的 TCP Socket 服务。它接收用户端或管理端发来的协议 v1 请求，完成请求校验、管理员认证、业务分发和 SQLite 持久化，再把统一格式的成功或错误响应返回给客户端。

服务端不负责绘制界面，也不让客户端直接访问 SQLite。完整链路是：

```text
Qt 用户端 / 管理端
        │ TCP + 协议 v1
        ▼
server/src/main.cpp
        │ 调用
        ▼
libs/database/src/database.cpp
        │ Qt SQL
        ▼
database/schema/schema.sql + SQLite
```

现场可以强调：用户端与管理端均走这条真实 Socket 链路——管理端 Socket 适配器已随 PR #11/#13 合入 main 并验证，`EV_ADMIN_DATA_SOURCE=socket` 是最终演示路径（Mock 保留为离线兜底）。

## 2. 服务端代码文件分工

| 文件 | 作用 |
|---|---|
| `server/src/main.cpp` | 创建 TCP 服务、接收连接、读取请求、分发操作、管理员 Token、返回响应 |
| `libs/protocol/include/ev_protocol/message.h` | 协议消息结构和错误码 |
| `libs/protocol/src/message.cpp` | JSON 信封校验和错误名称映射 |
| `libs/protocol/src/frame_codec.cpp` | 四字节长度前缀编解码、半包/粘包处理 |
| `libs/database/include/ev_database/database.h` | 数据库服务公开接口 |
| `libs/database/src/database.cpp` | SQLite 初始化、查询、事务、状态机和幂等 |
| `database/schema/schema.sql` | SQLite v0.4 表、索引、视图、触发器（v0.3→v0.4 升级随 PR #19 合入） |
| `database/seeds/dev.sql` | 可重复的演示数据 |

一个重要的架构特点是：网络层只负责协议和调度，数据库类才负责跨表业务一致性。Qt 页面中没有 SQL。

## 3. 程序启动流程

入口是 `server/src/main.cpp` 的 `main()`：

1. 创建 `QCoreApplication`，因为服务端是后台程序，不需要 `QApplication`。
2. 创建 `QTcpServer` 和进程级 `AdminSessionStore`。
3. 从环境变量读取数据库、Schema、种子文件和监听地址：
   - `EV_DATABASE_PATH`，默认 `var/ev-charging.db`
   - `EV_SCHEMA_PATH`，默认从当前目录或程序相对目录查找
   - `EV_DATABASE_SEED_PATH`，可选
   - `EV_SERVER_HOST`，默认 `127.0.0.1`
   - `EV_SERVER_PORT`，默认 `45454`
4. 确保数据库父目录存在。
5. 调用 `server.listen(host, listenPort)`。
6. 收到 `newConnection` 时，为每个 TCP 连接创建一个 `ClientConnection`。
7. 进入 `app.exec()`，由 Qt 事件循环持续处理网络事件。

评审时可以说：服务端没有死循环轮询 Socket，而是使用 Qt 的事件驱动模型；新连接、可读数据、断开和错误分别由信号触发。

启动成功日志：

```text
ev-server database "..." schema ".../database/schema/schema.sql"
ev-server listening on "127.0.0.1" 45454
```

## 4. 一个请求是怎样走完的

以用户端登录为例：

```text
用户端构造 Message
  ↓
encodeFrame() 加四字节长度前缀
  ↓
TCP 发送到 127.0.0.1:45454
  ↓
ClientConnection::readAvailable()
  ↓
FrameDecoder::feed() 处理完整帧
  ↓
ClientConnection::handle()
  ↓
handleUserLogin()
  ↓
Database::loginUser()
  ↓
user.login.result 或 error
  ↓
encodeFrame() 返回客户端
```

服务端在 `handle()` 一开始记录：

```text
request "user-..." "user.login"
```

这就是评审现场证明请求到达服务端的最直观证据。

## 5. TCP 帧和协议解析

协议不是直接把 JSON 字符串裸发出去，而是：

```text
4 字节无符号大端 payload 长度 + UTF-8 JSON
```

例如 JSON 长度是 `0x2f`，开头就是：

```text
00 00 00 2f {"v":1,"id":"...","type":"health","payload":{}}
```

`FrameDecoder` 的处理重点：

- 数据不足四字节：等待下一次 TCP 读取。
- 已读到长度但 payload 不完整：保留 buffer，等待后续数据。
- 一个 TCP 读取包含多个帧：循环解析，按顺序返回多个 `Message`。
- 长度为 0 或超过 1 MiB：返回 `INVALID_FRAME`。
- JSON 不是对象或字段类型错误：返回 `INVALID_JSON` 或 `INVALID_REQUEST`。
- 完整帧后面出现坏帧：先处理前面的有效请求，再发送错误并断开当前连接。

消息信封固定包含：

```json
{"v":1,"id":"request-id","type":"operation","payload":{}}
```

`id` 用于请求和响应关联；状态修改请求还用它实现幂等重放。

## 6. ClientConnection 的职责

`ClientConnection` 是一个连接级 QObject，内部持有：

- 当前 `QTcpSocket*`
- 当前连接的 `FrameDecoder`
- 当前连接自己的 `Database` 对象
- 共享的 `AdminSessionStore*`

构造函数建立三个重要信号连接：

1. `readyRead` → `readAvailable()`：读取并解析数据。
2. `disconnected` → `deleteLater()`：连接关闭后安全释放连接对象。
3. `errorOccurred` → 记录 Socket 错误。

当前用户端每个请求完成后会销毁自己的临时 Socket，所以服务端可能看到：

```text
request "..." "station.list"
socket error "The remote host closed the connection"
```

如果客户端已经拿到成功结果，这通常只是短连接正常回收，不是业务失败。

## 7. 请求分发和参数校验

`handle()` 根据 `request.type` 选择处理函数，当前包括：

- 基础：`health`、`echo`
- 用户：`user.login`、`user.profile.get/update`、`wallet.recharge`
- 查询：`station.list`、`pile.list`、`order.active.get`、`order.history.list`
- 预约：`reservation.create/confirm/cancel`
- 充电：`charging.start/stop/settle`
- 管理员：`admin.login`、统计、站点、桩重启、用户状态

分发前后有三类通用校验：

### 7.1 只允许已知字段

`hasOnlyFields()` 拒绝未定义字段，避免客户端悄悄传入服务端没有定义的参数。

### 7.2 正整数 ID 校验

`positiveId()` 要求 JSON 数字是有限值、整数、正数，并限制在 JavaScript 安全整数范围内，防止浮点数或超大 ID 被错误转换。

### 7.3 错误统一映射

数据库层返回 `ErrorKind`，网络层通过 `sendDatabaseError()` 映射成协议错误码：

| 数据库/业务结果 | 协议码 |
|---|---:|
| 参数错误 | 1002 |
| 未认证 | 1100 |
| 用户冻结 | 1101 |
| 找不到资源 | 1200 |
| 状态冲突/重复订单 | 1201 |
| 余额不足 | 1202 |
| 数据库故障 | 1300 |

客户端按错误码分支，不依赖中文 message 文本。

## 8. 用户接口逻辑

### 8.1 `user.login`

`handleUserLogin()` 先用正则校验手机号必须是 11 位 ASCII 数字，然后调用 `Database::loginUser()`。

数据库登录使用 `BEGIN IMMEDIATE`：

1. 按手机号查询用户。
2. 找到则读取用户资料和余额。
3. 找不到则创建默认昵称的新用户。
4. 提交事务。
5. 返回稳定用户 ID、手机号、昵称、余额和状态。

冻结用户仍然可以登录，返回 `status: "frozen"`，后续由具体业务判断是否禁止操作。

### 8.2 资料和钱包

- `user.profile.get`：只读用户资料。
- `user.profile.update`：至少更新昵称或头像之一；使用请求 ID 保存成功响应，重复请求不会重复写入。
- `wallet.recharge`：校验正整数分，在一个事务中更新用户余额、写入充值流水并保存幂等记录。

金额始终使用整数分，例如 100 元在线上传的是 `10000`，数据库字段是 `amount_cents=10000`，避免浮点误差。

## 9. 预约到结算的核心状态机

评审时建议画出这一条：

```text
idle pile
   │ reservation.create
   ▼
pending_reservation order + reserved pile
   │ reservation.confirm
   ▼
reserved order + reserved pile
   │ charging.start
   ▼
charging order + charging pile
   │ charging.stop
   ▼
pending_settlement order + idle pile
   │ charging.settle
   ▼
completed order + charge ledger + updated balance
```

### 9.1 创建预约

`Database::createReservation()` 在一个写事务内：

1. 检查用户存在、状态为 active、没有其他未完成订单。
2. 检查目标桩属于有效站点且状态为 idle。
3. 创建 `pending_reservation` 订单，保存当时的单价。
4. 把桩从 `idle` 改为 `reserved`。
5. 读取订单和桩的完整结果。
6. 保存 `request_records` 并提交。

任何一步失败都回滚，因此不会出现“订单创建了但桩仍空闲”或“桩被占用但没有订单”的半完成状态。

### 9.2 确认和取消

- 确认只把订单从 `pending_reservation` 改为 `reserved`。
- 取消允许取消待确认或已确认预约；订单变成 `cancelled`，桩回到 `idle`。
- 重放相同请求 ID 会返回原成功结果，不重复推进状态。

### 9.3 开始充电

`charging.start` 强制二选一：

- 传 `order_id`：只能启动本用户的 `reserved` 订单。
- 传 `pile_id`：只能直接启动有效站点的 `idle` 桩，并在同一事务中创建 charging 订单。

用户冻结、订单状态不对、桩状态不对或并发抢占失败都会返回明确错误。

### 9.4 停止充电

`charging.stop`：

1. 只接受本用户的 `charging` 订单。
2. 校验结束时间不早于开始时间。
3. 按功率、时长、单价和服务费计算能耗与金额。
4. 订单改为 `pending_settlement`。
5. 桩立即从 `charging` 改为 `idle`。

停止时只生成待结算金额，不扣钱包。

### 9.5 结算

`charging.settle` 在一个事务中完成全部财务动作：

1. 检查订单必须是 `pending_settlement`。
2. 读取余额并检查余额是否足够。
3. 插入一条负的 `charge` 钱包流水。
4. 扣减用户余额。
5. 订单改为 `completed` 并写入 `settled_at`。
6. 增加充电桩累计次数和累计时长。
7. 保存幂等响应并提交。

余额不足或任一步数据库操作失败，整个事务回滚，订单仍保持待结算，避免扣款与订单状态不一致。

## 10. 并发和幂等设计

### 10.1 数据库并发

写事务使用 `BEGIN IMMEDIATE`，并设置 SQLite busy timeout。这样两个客户端同时抢同一个空闲桩时，最多只有一个事务能成功；另一个返回 `CONFLICT`。

Schema 还通过部分唯一索引保证：

- 一个用户最多一个未完成订单。
- 一个桩最多一个占用中的订单。
- 一个已完成订单最多一条 charge 流水。

### 10.2 请求幂等

状态修改请求将以下信息写入 `request_records`：

```text
request_id + operation + payload fingerprint + response_json
```

如果网络响应丢失，客户端用相同 ID 重试：

- 操作和参数相同：返回第一次成功响应。
- 相同 ID 但操作或参数不同：返回 `CONFLICT`。
- 原请求是业务失败或数据库失败：不保存成功回放记录，允许修复后用原 ID 重试。

这解决了“客户端不知道服务端到底扣没扣款”时的重复执行风险。

## 11. 管理员认证逻辑

`AdminSessionStore` 是进程内 Token 表：

1. `admin.login` 由数据库校验用户名和 SHA-256 密码摘要。
2. 登录成功后生成 32 字节随机值，编码成 64 位十六进制 Token。
3. Token 有效期 8 小时，只保存在服务端进程内。
4. 除 `admin.login` 外，所有 `admin.*` 请求先经过 `authorizeAdministrator()`。
5. 鉴权检查 Token 是否存在、是否过期、管理员账号是否仍 active。
6. 写操作还校验 payload 中的 `administrator_id` 必须等于 Token 对应主体。

服务端重启后 Token 全部失效，这是当前 v1 的明确设计。

## 12. 数据库 Schema 的保护层

`database/schema/schema.sql` 不只是建表，还包含：

- 用户、管理员、站点、充电桩、订单、钱包流水、重启日志、请求记录。
- 字段 CHECK：手机号、金额、坐标、状态、功率等。
- 索引：按站点/状态、用户订单、桩订单、结算时间查询。
- `station_pile_status` 视图：派生站点桩状态统计。
- `revenue_daily` 视图：按订单结算日期统计最终收入。
- 完成订单与 charge 流水的触发器：没有匹配扣款流水不能完成订单，完成后的流水不能被删除或篡改。

服务层负责跨表状态转换，Schema 负责最后一道数据库完整性保护。

## 13. 真实代码片段速讲（现场可直接打开文件）

下面的核心代码片段均来自当前仓库。现场建议边滚动源码边讲“这几行解决什么问题”。
代码块中的 `...` 仅表示截去不影响讲解的 `prepare/bindValue` 或错误处理行；涉及 SQL 的片段会明确标注为“SQL 字符串摘录”，完整实现仍以链接文件为准。

### 13.1 启动与连接生命周期

[`server/src/main.cpp:636`](/home/bit/projects/work/ev-charging-platform/server/src/main.cpp:636) 的入口先准备运行时配置，再监听 TCP：

```cpp
QCoreApplication app(argc, argv);
QTcpServer server;
AdminSessionStore adminSessions;
const QString databasePath = qEnvironmentVariable(
    "EV_DATABASE_PATH", QStringLiteral("var/ev-charging.db"));
...
if (!server.listen(host, listenPort)) {
    qCritical() << "listen failed" << host.toString()
               << listenPort << server.errorString();
    return 1;
}
qInfo() << "ev-server listening on" << host.toString() << listenPort;
```

`newConnection` 信号中每个 TCP 连接都会构造一个 `ClientConnection`；这就是“一个客户端连接对应一个连接处理对象”的来源。构造函数（[`server/src/main.cpp:67`](/home/bit/projects/work/ev-charging-platform/server/src/main.cpp:67)）把 Qt 网络事件接到处理函数：

```cpp
connect(socket_, &QTcpSocket::readyRead, this,
        [this] { readAvailable(); });
connect(socket_, &QTcpSocket::disconnected,
        this, &QObject::deleteLater);
connect(socket_, &QTcpSocket::errorOccurred, this,
        [this](QAbstractSocket::SocketError) {
            qWarning() << "socket error" << socket_->errorString();
        });
```

### 13.2 收包、解码、分发

[`ClientConnection::readAvailable()`](/home/bit/projects/work/ev-charging-platform/server/src/main.cpp:83) 不假设一次 `readyRead` 就是一条完整消息：

```cpp
const QList<Message> requests =
    decoder_.feed(socket_->readAll(), &error, &errorCode);
for (const Message &request : requests)
    handle(request);
if (!error.isEmpty()) {
    sendError(QString(), errorCode, error);
    socket_->disconnectFromHost();
}
```

`FrameDecoder::feed()`（[`libs/protocol/src/frame_codec.cpp:18`](/home/bit/projects/work/ev-charging-platform/libs/protocol/src/frame_codec.cpp:18)）把 TCP 字节流转换为消息列表：

```cpp
buffer_.append(bytes);
while (buffer_.size() >= 4) {
    const auto *raw = reinterpret_cast<const uchar *>(buffer_.constData());
    const quint32 size = (quint32(raw[0]) << 24)
                       | (quint32(raw[1]) << 16)
                       | (quint32(raw[2]) << 8) | quint32(raw[3]);
    if (size == 0 || size > kMaxPayloadBytes) { ... }
    if (buffer_.size() < 4 + qint64(size)) return messages;
    const QByteArray payload = buffer_.mid(4, size);
    buffer_.remove(0, 4 + size);
    ...
}
```

现场讲解重点是：前 4 字节是大端长度；长度不够时保留缓存（半包），一次读到多条时循环（粘包）。JSON 信封由 [`Message::fromJson()`](/home/bit/projects/work/ev-charging-platform/libs/protocol/src/message.cpp:25) 检查 `v/id/type/payload` 的类型。

### 13.3 登录：网络校验与数据库事务分层

网络层的登录 handler（[`server/src/main.cpp:185`](/home/bit/projects/work/ev-charging-platform/server/src/main.cpp:185)）只负责协议输入输出：

```cpp
static const QRegularExpression phonePattern(
    QStringLiteral("^[0-9]{11}$"));
if (!phoneValue.isString() || !phonePattern.match(phone).hasMatch()) {
    sendError(request.id, ErrorCode::InvalidRequest,
              QStringLiteral("phone must contain exactly 11 ASCII digits"));
    return;
}
QJsonObject user; QString error; ErrorKind kind = ErrorKind::None;
if (!database_.loginUser(phone, &user, &error, &kind)) {
    sendDatabaseError(request.id, kind, error,
                      QStringLiteral("user login database failure"));
    return;
}
sendResponse(request, QStringLiteral("user.login.result"),
             QJsonObject{{QStringLiteral("user"), user}});
```

数据库层（[`libs/database/src/database.cpp:278`](/home/bit/projects/work/ev-charging-platform/libs/database/src/database.cpp:278)）才执行 `BEGIN IMMEDIATE`、查询和“查不到则插入”：

```cpp
if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) { ... }
query.prepare(QStringLiteral(
    "SELECT id, phone, nickname, avatar_path, balance_cents, status "
    "FROM users WHERE phone = :phone"));
...
if (query.next()) {
    const QJsonValue avatar = query.value(3).isNull()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(query.value(3).toString());
    *user = QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                        {QStringLiteral("phone"), query.value(1).toString()},
                        {QStringLiteral("balance_cents"), query.value(4).toLongLong()},
                        {QStringLiteral("status"), query.value(5).toString()}};
} else {
    query.prepare(QStringLiteral(
        "INSERT INTO users(phone, nickname, balance_cents, status, created_at, updated_at) "
        "VALUES (:phone, :nickname, 0, 'active', :created_at, :updated_at)"));
    query.bindValue(QStringLiteral(":nickname"), QStringLiteral("用户") + phone.right(4));
}
```

这能说明 UI/Socket 层和持久化层边界清晰，且登录创建用户具有事务性。

### 13.4 预约 handler 与跨表事务

[`handleReservationCreate()`](/home/bit/projects/work/ev-charging-platform/server/src/main.cpp:554) 的职责是校验 ID、调用数据库、封装响应：

```cpp
qint64 userId = 0, pileId = 0;
if (!requestIds(request.payload, &userId, &pileId,
               QStringLiteral("pile_id"))) {
    sendError(request.id, ErrorCode::InvalidRequest,
              QStringLiteral("user_id and pile_id must be positive integers"));
    return;
}
QJsonObject order, pile; QString error; ErrorKind kind = ErrorKind::None;
if (!database_.createReservation(request.id, userId, pileId,
                                 &order, &pile, &error, &kind)) { ... }
sendResponse(request, QStringLiteral("reservation.create.result"),
             QJsonObject{{QStringLiteral("order"), order},
                         {QStringLiteral("pile"), pile}});
```

数据库实现（[`Database::createReservation()`](/home/bit/projects/work/ev-charging-platform/libs/database/src/database.cpp:1581)）在同一事务中先插入订单，再条件更新桩。下面是从源码 `QStringLiteral` 中摘出的 SQL 关键句：

```sql
INSERT INTO charging_orders(..., status, ...) VALUES (..., 'pending_reservation', ...)
UPDATE charging_piles SET status = 'reserved'
WHERE id = :id AND status = 'idle'
```

对应的 C++ 竞争检查是：

```cpp
if (!query.exec() || query.numRowsAffected() != 1) {
    rollback();
    setFailure(error, kind, ErrorKind::Conflict,
               QStringLiteral("pile was claimed by another request"));
    return false;
}
```

因此不会出现“订单已建但桩未锁定”的中间结果。

### 13.5 开始、停止、结算：三段代码对应三种状态

开始充电在网络层强制 `order_id` 与 `pile_id` 二选一（[`server/src/main.cpp:585`](/home/bit/projects/work/ev-charging-platform/server/src/main.cpp:585)）：

```cpp
|| (orderId == 0 && pileId == 0)
|| (orderId > 0 && pileId > 0)
```

数据库层再用旧状态条件抢占桩（[`libs/database/src/database.cpp:1806`](/home/bit/projects/work/ev-charging-platform/libs/database/src/database.cpp:1806)）。以下是源码中的 SQL 字符串关键内容：

```sql
UPDATE charging_piles SET status = 'charging'
WHERE id = :id AND status = :expected_status
```

紧接着的 C++ 竞争检查：

```cpp
if (!query.exec() || query.numRowsAffected() != 1) {
    rollback();
    ... // 返回 pile is not idle / pile is not reserved
}
```

停止充电计算金额并进入待结算（[`libs/database/src/database.cpp:1878`](/home/bit/projects/work/ev-charging-platform/libs/database/src/database.cpp:1878)）。金额计算是原始 C++，状态变更是源码中的 SQL 字符串：

```cpp
const qint64 seconds = qMax<qint64>(1, startedTime.secsTo(finishTime));
const qint64 energyWh = qMax<qint64>(1, static_cast<qint64>(
    powerKw * 1000.0 * seconds / 3600.0));
const qint64 total = (energyWh * price + 999) / 1000 + serviceFee;
```

```sql
UPDATE charging_orders SET status='pending_settlement',
    energy_wh=:energy_wh, total_amount_cents=:total ...
UPDATE charging_piles SET status='idle' ...
```

结算才真正扣款（[`libs/database/src/database.cpp:1937`](/home/bit/projects/work/ev-charging-platform/libs/database/src/database.cpp:1937)）：

```cpp
if (balance < total) {
    rollback();
    setFailure(error, kind, ErrorKind::InsufficientBalance,
               QStringLiteral("insufficient balance"));
    return false;
}
const qint64 after = balance - total;
query.bindValue(QStringLiteral(":amount"), -total);
// INSERT wallet_transactions(..., 'charge', ...)
// UPDATE users ...; UPDATE charging_orders SET status='completed' ...
```

现场用这三段代码解释：“停止释放桩但不扣钱；结算把流水、余额、订单和桩统计放在同一事务里。”

### 13.6 幂等、鉴权与统一响应

幂等查询（[`libs/database/src/database.cpp:626`](/home/bit/projects/work/ev-charging-platform/libs/database/src/database.cpp:626)）先比对操作和参数指纹：

```cpp
if (!query.next()) return true;
if (query.value(0).toString() != operation
    || query.value(1).toString() != fingerprint) {
    setFailure(error, kind, ErrorKind::Conflict,
               QStringLiteral("request id was already used for a different operation"));
    return false;
}
*response = QJsonDocument::fromJson(
    query.value(2).toString().toUtf8()).object();
return true;
```

Token 的生成代码在 [`AdminSessionStore::issue()`](/home/bit/projects/work/ev-charging-platform/server/src/main.cpp:29)：

```cpp
for (int i = 0; i < 4; ++i) {
    const quint64 value = QRandomGenerator::system()->generate64();
    for (int shift = 0; shift < 64; shift += 8)
        bytes.append(static_cast<char>((value >> shift) & 0xff));
}
const QString token = QString::fromLatin1(bytes.toHex());
sessions_.insert(token, Session{administratorId,
    QDateTime::currentDateTimeUtc().addSecs(kLifetimeSeconds)});
```

管理员 Token 校验（[`server/src/main.cpp:155`](/home/bit/projects/work/ev-charging-platform/server/src/main.cpp:155)）解析 Token、检查会话，再核对管理员主体 ID；失败直接返回 `UNAUTHORIZED`。成功和错误最终都调用 [`sendResponse()`](/home/bit/projects/work/ev-charging-platform/server/src/main.cpp:619) / `sendError()`，统一走 `encodeFrame()`：

```cpp
socket_->write(encodeFrame(
    Message{kProtocolVersion, request.id, type, payload}));
```

数据库连接初始化（[`libs/database/src/database.cpp:114`](/home/bit/projects/work/ev-charging-platform/libs/database/src/database.cpp:114)）显式打开外键约束和忙等待：

```cpp
connection_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
connection_.setDatabaseName(databasePath_);
if (!connection_.open()) { ... }
QSqlQuery pragma(connection_);
pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
```

这两项分别保证关联删除/引用受约束，以及并发写入时给 SQLite 一个有限的等待窗口。

## 14. 现场建议讲解顺序（约 5 分钟）

1. 打开服务端终端，展示 `ev-server listening on 127.0.0.1:45454`。
2. 说明代码分层：`main.cpp` 网络与分发，`protocol` 帧，`database.cpp` 事务，`schema.sql` 约束。
3. 启动用户端并设置 `EV_USER_CLIENT_TRANSPORT=socket`。
4. 用户端登录，指出服务端出现 `user.login`。
5. 查询站点，指出 `station.list`、`pile.list`。
6. 完成充值、预约、确认、开始、停止、结算，按日志指出对应操作名。
7. 讲解停止和结算的差异：停止释放桩，结算才扣钱包并完成订单。
8. 展示重复请求/余额不足/非空闲桩等异常设计。
9. 最后说明管理员 Token、事务、幂等和数据库触发器。

## 15. 评审时不要说错的三件事

1. 不要说“管理端已经通过 Socket 读取数据库”：当前管理端仍使用 Mock Repository。
2. 不要把 `The remote host closed the connection` 直接说成业务失败：当前用户端是每次请求一个短 TCP 连接，成功响应后主动关闭连接。
3. 不要说“停止充电就已经扣款”：当前停止只进入 `pending_settlement`，结算接口才写 charge 流水并扣余额。
