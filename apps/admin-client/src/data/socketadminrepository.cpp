#include "socketadminrepository.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QJsonArray>
#include <QJsonValue>
#include <QTimer>
#include <QTcpSocket>
#include <QtGlobal>

#include "ev_protocol/frame_codec.h"
#include "ev_protocol/message.h"
#include "socketparse.h"

// 管理端 Socket 适配层实现。设计决策(设计稿 D1-D8)与 wire 字段映射见
// socketadminrepository.h 头文件注释 + socketparse.h 映射表。

namespace ev {

namespace {
constexpr int kCodeOk = 0;
constexpr int kCodeUnauthorized = 1100; // 会话 token 缺失/过期/与 administrator_id 不匹配
constexpr int kCodeInvalidRequest = 1002; // 坏信封/响应结构错误的兜底协议码
constexpr int kCodeNotFound = 1200;

// 查询类超时文案(可原样重试——查询无幂等限制)
const QString kQueryTimeoutMessage = QStringLiteral("请求超时，请重试");
// 动作类超时文案(D7 幂等纪律): 不自动重发、不换 id; 服务端按请求 id 幂等,
// 用户原样重试不会重复执行
const QString kActionTimeoutMessage = QStringLiteral(
    "请求超时，操作结果未知；可原样重试（服务端按请求 id 幂等，不会重复执行）");
} // namespace

SocketAdminRepository::SocketAdminRepository(const QString &host, quint16 port,
                                             QObject *parent)
    : QObject(parent)
{
    // host/port: 显式参数(测试注入) > EV_SERVER_HOST/EV_SERVER_PORT 环境变量 > 默认值
    // (与 libs/protocol 与服务端约定一致: 127.0.0.1:45454)
    m_host = host;
    if (m_host.isEmpty()) {
        const QString envHost = qEnvironmentVariable("EV_SERVER_HOST");
        m_host = envHost.isEmpty() ? QStringLiteral("127.0.0.1") : envHost;
    }
    m_port = port;
    if (m_port == 0) {
        bool ok = false;
        const int envPort = qEnvironmentVariable("EV_SERVER_PORT").toInt(&ok);
        m_port = (ok && envPort > 0 && envPort <= 65535) ? quint16(envPort) : 45454;
    }
}

SocketAdminRepository::~SocketAdminRepository() = default;

QString SocketAdminRepository::dataSourceName() const
{
    return QStringLiteral("Socket(%1:%2)").arg(m_host).arg(m_port);
}

// ── 内部传输管理(D2) ──────────────────────────────────────────────────────────

void SocketAdminRepository::sendRequest(
    const QString &type, const QJsonObject &specific, bool isAction, QObject *context,
    std::function<void(const ReplyEnvelope &)> callback)
{
    const QString id = generateRequestId();
    Pending pending;
    pending.context = context;
    pending.rawContext = context;
    pending.requestType = type;
    pending.isAction = isAction;
    pending.callback = std::move(callback);
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    pending.timer->setInterval(m_timeoutMs);
    connect(pending.timer, &QTimer::timeout, this, [this, id] { onPendingTimeout(id); });
    // context 销毁 → 立即移除 pending(防悬垂; 回调前另有 QPointer 双保险)
    if (context) {
        pending.destroyedConnection = QObject::connect(
            context, &QObject::destroyed, this, [this, id] { removePending(id); });
    }
    m_pending.insert(id, pending);
    pending.timer->start();

    ev::protocol::Message message;
    message.id = id;
    message.type = type;
    message.payload = buildPayload(type, specific); // D6 组装单点
    const QByteArray frame = ev::protocol::encodeFrame(message);

    ensureConnected(); // 懒连接: 无连接/已断开 → 发起(连接中则等待, 冲刷在 connected 信号)
    queueOrWrite(id, frame);
}

void SocketAdminRepository::ensureConnected()
{
    if (!m_socket) {
        m_socket = new QTcpSocket(this);
        m_decoder = std::make_unique<ev::protocol::FrameDecoder>();
        connect(m_socket, &QTcpSocket::connected, this,
                [this] { m_connectPhase = false; flushOutbox(); });
        connect(m_socket, &QTcpSocket::readyRead, this, [this] { onSocketReadyRead(); });
        connect(m_socket, &QTcpSocket::disconnected, this,
                [this] { onSocketDisconnected(); });
        connect(m_socket, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) { onSocketError(); });
    }
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        m_decoder->reset();
        m_connectPhase = true;
        m_socket->connectToHost(m_host, m_port);
    }
}

void SocketAdminRepository::queueOrWrite(const QString &id, const QByteArray &frame)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(frame);
        return;
    }
    m_outbox.append(qMakePair(id, frame)); // 连接建立前暂存, connected 信号后冲刷
}

void SocketAdminRepository::flushOutbox()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    for (const auto &entry : qAsConst(m_outbox))
        m_socket->write(entry.second);
    m_outbox.clear();
}

QString SocketAdminRepository::generateRequestId()
{
    // 'c-admin-<全局递增>': 请求 id 全局唯一且不跨请求复用(B 幂等表按 id 去重, protocol.md)
    return QStringLiteral("c-admin-%1").arg(++m_requestSeq);
}

// ── pending 生命周期(D2/D3/D7) ────────────────────────────────────────────────

void SocketAdminRepository::removePending(const QString &id)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }
    if (it->destroyedConnection)
        QObject::disconnect(it->destroyedConnection);
    m_pending.erase(it);
    // 同步清掉仍在等待发送的帧(超时/取消后不再发往服务器)
    for (int i = m_outbox.size() - 1; i >= 0; --i) {
        if (m_outbox.at(i).first == id)
            m_outbox.removeAt(i);
    }
}

void SocketAdminRepository::failPending(const QString &id, bool networkError,
                                        const QString &message)
{
    const auto it = m_pending.constFind(id);
    if (it == m_pending.cend())
        return;
    const Pending pending = it.value(); // 拷贝: removePending 后仍可安全使用回调
    removePending(id);
    if (pending.rawContext && pending.context.isNull())
        return; // 防悬垂双保险: context 已销毁 → 不再回调
    if (!pending.callback)
        return;
    ReplyEnvelope env;
    env.ok = false;
    env.networkError = networkError; // 传输层失败: errorCode=0, 不冒充协议码(D3)
    env.errorCode = kCodeOk;
    env.message = message;
    pending.callback(env);
}

void SocketAdminRepository::failAllInFlight(bool networkError, const QString &message)
{
    const QList<QString> ids = m_pending.keys();
    for (const QString &id : ids)
        failPending(id, networkError, message);
}

void SocketAdminRepository::onPendingTimeout(const QString &id)
{
    const auto it = m_pending.constFind(id);
    if (it == m_pending.cend())
        return;
    // D7: 动作类超时不自动重发、不换 id(结果未知提示); 查询类超时由调用方按需重试
    const QString message = it->isAction ? kActionTimeoutMessage : kQueryTimeoutMessage;
    failPending(id, true, message);
}

void SocketAdminRepository::onSocketError()
{
    if (!m_socket)
        return;
    // 连接建立阶段失败(拒绝/超时/无监听) vs 会话中断: 文案区分
    const QString message = m_connectPhase
        ? QStringLiteral("无法连接服务器 %1:%2").arg(m_host).arg(m_port)
        : QStringLiteral("与服务器连接中断: %1").arg(m_socket->errorString());
    m_connectPhase = false;
    failAllInFlight(true, message);
    m_socket->abort(); // 重置到 UnconnectedState, 下次请求自动重连(懒连接语义)
}

void SocketAdminRepository::onSocketDisconnected()
{
    m_connectPhase = false;
    // 服务端主动断开/网络中断: 在途请求全部以传输层失败结束(不冒充协议码)
    if (!m_pending.isEmpty())
        failAllInFlight(true, QStringLiteral("与服务器连接已断开"));
}

void SocketAdminRepository::onSocketReadyRead()
{
    if (!m_socket || !m_decoder)
        return;
    const QByteArray chunk = m_socket->readAll();
    QString decodeError;
    ev::protocol::ErrorCode decodeErrorCode = ev::protocol::ErrorCode::Ok;
    const QList<ev::protocol::Message> messages =
        m_decoder->feed(chunk, &decodeError, &decodeErrorCode);
    for (const ev::protocol::Message &message : messages)
        handleIncomingMessage(message);
    if (!decodeError.isEmpty()) {
        // 服务器侧坏帧(协议违规): 在途请求以传输层失败结束, 连接重置
        // (同批好帧已在上方先派发——C-S1-002"坏帧保留好帧")
        failAllInFlight(true, QStringLiteral("收到无法解析的协议帧，连接已重置"));
        if (m_socket)
            m_socket->abort();
    }
}

void SocketAdminRepository::handleIncomingMessage(const ev::protocol::Message &message)
{
    if (message.id.isEmpty()) {
        qWarning().noquote() << "SocketAdminRepository: 丢弃无 id 的响应"
                             << message.type;
        return;
    }
    const auto it = m_pending.find(message.id);
    if (it == m_pending.end()) {
        qWarning().noquote() << "SocketAdminRepository: 丢弃未知请求 id 的响应"
                             << message.id << message.type;
        return;
    }
    // 响应 type 必须为 请求 type+".result" 或 "error"; 不匹配 → 丢弃并记日志,
    // 不做类型假设、不杀在途请求(D2)
    const QString expectedResult = it->requestType + QLatin1String(".result");
    if (message.type != expectedResult && message.type != QLatin1String("error")) {
        qWarning().noquote() << "SocketAdminRepository: 丢弃 type 不匹配的响应, 期望"
                             << expectedResult << "或 error, 实际" << message.type;
        return;
    }
    const bool isSuccess = (message.type == expectedResult);
    const Pending pending = it.value();
    removePending(message.id);
    if (pending.rawContext && pending.context.isNull())
        return; // 防悬垂: 响应到达时 context 已销毁
    if (!pending.callback)
        return;
    ReplyEnvelope env;
    env.ok = isSuccess;
    env.errorCode = kCodeOk;
    if (isSuccess) {
        env.payload = message.payload;
    } else {
        // 协议 error 信封(D3): payload.code → errorCode(客户端只按 code 分支);
        // message 仅展示/日志, 不作为分支依据
        QStringList issues;
        env.errorCode = socketparse::parseErrorCode(message.payload, &issues);
        env.message = message.payload.value(QLatin1String("message")).toString();
        if (env.errorCode == kCodeUnauthorized) {
            // Q6(2026-09-06): 会话 token 缺失/过期/与 administrator_id 不匹配 →
            // 服务端 1100; 清除本地认证上下文(token+admin), 下次登录重新获取
            // (页面级跳登录属 UI 策略, 数据层只负责失效即清, 见设计稿 D3)。
            applyLoginOutcome(false, env.errorCode, AdminInfo(), QString());
            qWarning().noquote()
                << "SocketAdminRepository: 会话失效(1100), 已清除认证上下文";
        }
    }
    pending.callback(env);
}

// ── D6 鉴权预留: buildPayload 单点(Q6 已冻结 2026-09-05) ───────────────────────

QJsonObject SocketAdminRepository::buildPayload(const QString &type,
                                                const QJsonObject &specific) const
{
    QJsonObject payload = specific;
    // D6/Q6 冻结(2026-09-06, PR #12 = main 3d015f7, 服务端 authorizeAdministrator):
    //   - 除 admin.login 外所有 admin.* 请求携带会话 token(8h 进程内, 缺失/过期 1100);
    //   - mutation(admin.station.create / admin.pile.restart / admin.user.status.set)
    //     额外携带 administrator_id, 服务端校验与 token 主体一致(不匹配 1100);
    //   - 读类(statistics/station.list/pile.list/user.list)只带 token —— 多余字段会被
    //     hasOnlyFields 拒(1002), 故不附加 administrator_id;
    //   - 未认证(m_token 空)不附加任何凭据: 服务端按 1100 拒绝, 客户端清会话状态。
    // payload 组装只收敛本函数, 服务端契约再变只改这里。
    if (m_authenticated && type.startsWith(QLatin1String("admin."))
        && type != QLatin1String("admin.login")) {
        payload.insert(QLatin1String("token"), m_token);
        if (type == QLatin1String("admin.station.create")
            || type == QLatin1String("admin.pile.restart")
            || type == QLatin1String("admin.user.status.set")) {
            payload.insert(QLatin1String("administrator_id"), m_admin.id);
        }
    }
    return payload;
}

void SocketAdminRepository::applyLoginOutcome(bool ok, int errorCode,
                                              const AdminInfo &admin,
                                              const QString &token)
{
    Q_UNUSED(errorCode);
    if (ok) {
        // Q6: login 成功才缓存会话 token + admin 对象(buildPayload 凭据来源)
        m_authenticated = true;
        m_token = token;
        m_admin = admin;
    } else {
        // 失败(1100/凭据错)/响应结构错/会话失效: 清空认证上下文,
        // 防止旧 token/身份附到后续请求(收到 1100 需重新登录)
        m_authenticated = false;
        m_token.clear();
        m_admin = AdminInfo();
    }
}

// ── 数据层接口 ────────────────────────────────────────────────────────────────

void SocketAdminRepository::login(
    const QString &username, const QString &password, QObject *context,
    std::function<void(const LoginResult &)> callback)
{
    QJsonObject specific;
    specific.insert(QLatin1String("username"), username);
    specific.insert(QLatin1String("password"), password);
    sendRequest(QStringLiteral("admin.login"), specific, /*isAction=*/false, context,
                [this, callback](const ReplyEnvelope &env) {
                    LoginResult result;
                    result.ok = env.ok;
                    result.networkError = env.networkError;
                    result.errorCode = env.errorCode;
                    result.message = env.message;
                    if (env.ok) {
                        QStringList issues;
                        QString reason;
                        if (socketparse::parseAdminLoginPayload(env.payload, &result,
                                                                &issues, &reason)) {
                            // Q6: 缓存会话 token + admin(后续 admin.* 凭据)
                            applyLoginOutcome(true, 0, result.admin, result.token);
                        } else {
                            // 成功信封但 admin/token 结构错: 按响应内容错误处理
                            result.ok = false;
                            result.errorCode = kCodeInvalidRequest;
                            result.message = reason;
                            applyLoginOutcome(false, kCodeInvalidRequest, AdminInfo(),
                                              QString());
                        }
                    } else {
                        // 1100 等协议错误/传输层失败: 不缓存认证上下文
                        applyLoginOutcome(false, env.errorCode, AdminInfo(), QString());
                    }
                    if (callback)
                        callback(result);
                });
}

void SocketAdminRepository::fetchOverview(
    QObject *context, std::function<void(const OverviewResult &)> callback)
{
    // Q3 冻结(2026-09-05, B 1f157de + docs/api README「管理端统计响应」样例): statistics
    // 无独立 30d 合计键; revenue_daily 固定长度(7d→7/30d→30 条), 聚合 revenue_cents =
    // 同序列和, updated_at 同快照。→ 双请求: 7d 为主体(桩五态/avg_station_utilization
    // 恒 7d 口径、与 range 无关), 30d 只取 revenue_cents 填 revenue30dCents(营收卡副行)。
    // 任一失败 → 整页 error(与 D5 整页一致性同哲学: 指标半页不可静默展示), 已派发后
    // 迟到的另一请求响应被忽略。
    struct MergeState {
        bool delivered = false;   // 整页结果只派发一次
        int remaining = 2;        // 未回请求数
        OverviewStats stats7;
        qint64 revenue30dCents = 0;
        bool hasData = true;      // 7d 主体 has_data(空库=false, 服务端权威)
    };
    auto state = std::make_shared<MergeState>();

    const auto deliverFailure = [state, callback](const ReplyEnvelope &env) {
        if (state->delivered)
            return;
        state->delivered = true;
        OverviewResult result;
        result.ok = false;
        result.errorCode = env.errorCode;
        result.networkError = env.networkError;
        result.error = env.message;
        if (callback)
            callback(result);
    };
    const auto maybeDeliver = [state, callback] {
        if (state->delivered || state->remaining > 0)
            return;
        state->delivered = true;
        OverviewResult result;
        result.ok = true;
        result.errorCode = kCodeOk;
        result.stats = state->stats7;
        result.stats.revenue30dCents = state->revenue30dCents;
        // has_data(冻结 2026-09-07, main getStatistics): 服务端显式区分空库(false)
        // 与"有数据但指标为 0"(true); 以 7d 主体响应为准(两 range 同库同刻一致),
        // 空库场景由概览页走"暂无概览数据"空态, 不展示 0 值指标页
        result.hasData = state->hasData;
        if (callback)
            callback(result);
    };

    for (const QString &range : {QStringLiteral("7d"), QStringLiteral("30d")}) {
        QJsonObject specific;
        specific.insert(QLatin1String("range"), range);
        sendRequest(QStringLiteral("admin.statistics.get"), specific, /*isAction=*/false,
                    context,
                    [state, deliverFailure, maybeDeliver,
                     range](const ReplyEnvelope &env) {
                        if (state->delivered)
                            return; // 另一请求已失败并派发整页错误
                        if (!env.ok) {
                            deliverFailure(env);
                            return;
                        }
                        QStringList issues;
                        QString reason;
                        OverviewStats stats;
                        bool hasData = true;
                        if (!socketparse::parseStatisticsPayload(env.payload, &stats,
                                                                  &hasData, &issues,
                                                                  &reason)) {
                            ReplyEnvelope bad;
                            bad.ok = false;
                            bad.errorCode = kCodeInvalidRequest;
                            bad.message = reason;
                            deliverFailure(bad);
                            return;
                        }
                        --state->remaining;
                        if (range == QLatin1String("7d")) {
                            state->stats7 = stats; // 主体: 五态/利用率/updated_at 取 7d
                            state->hasData = hasData; // has_data 以主体为准
                        } else {
                            // 30d 响应聚合 revenue_cents = 30 条 revenue_daily 之和
                            state->revenue30dCents = stats.revenueCents;
                        }
                        maybeDeliver();
                    });
    }
}

void SocketAdminRepository::fetchStations(
    QObject *context, std::function<void(const ListResult<StationInfo> &)> callback)
{
    sendRequest(QStringLiteral("admin.station.list"), QJsonObject(), /*isAction=*/false,
                context, [callback](const ReplyEnvelope &env) {
                    ListResult<StationInfo> result;
                    result.ok = env.ok;
                    result.errorCode = env.errorCode;
                    result.networkError = env.networkError;
                    result.error = env.message;
                    if (env.ok) {
                        QStringList issues;
                        QString reason;
                        if (!socketparse::parseStationsPayload(env.payload, &result.items,
                                                               &issues, &reason)) {
                            result.ok = false;
                            result.errorCode = kCodeInvalidRequest;
                            result.error = reason;
                        }
                    }
                    if (callback)
                        callback(result);
                });
}

void SocketAdminRepository::fetchUsers(
    QObject *context, std::function<void(const ListResult<UserInfo> &)> callback)
{
    sendRequest(QStringLiteral("admin.user.list"), QJsonObject(), /*isAction=*/false,
                context, [callback](const ReplyEnvelope &env) {
                    ListResult<UserInfo> result;
                    result.ok = env.ok;
                    result.errorCode = env.errorCode;
                    result.networkError = env.networkError;
                    result.error = env.message;
                    if (env.ok) {
                        QStringList issues;
                        QString reason;
                        if (!socketparse::parseUsersPayload(env.payload, &result.items,
                                                            &issues, &reason)) {
                            result.ok = false;
                            result.errorCode = kCodeInvalidRequest;
                            result.error = reason;
                        }
                    }
                    if (callback)
                        callback(result);
                });
}

// 2026-09-07 评审口径 A(管理员全量视图, 与 B 对齐): fetchPiles 单请求
// admin.pile.list(全量桩 = 全部站点含 inactive 站, 与 admin.statistics.get 的
// 全库桩计数 / admin.station.list 的站级聚合同范围)——替换原 D5 fan-out
// (admin.station.list → 逐站 pile.list): pile.list 仅允许查 active 站, fan-out
// 会漏停运站桩并使概览/站页/桩页数字口径分裂。接口契约见回复 B 的评审评论
// (读类: 仅 token; 响应 {piles:[readPile 11 列]})
void SocketAdminRepository::fetchPiles(
    QObject *context, std::function<void(const ListResult<PileInfo> &)> callback)
{
    sendRequest(QStringLiteral("admin.pile.list"), QJsonObject(), /*isAction=*/false,
                context, [this, callback](const ReplyEnvelope &env) {
                    ListResult<PileInfo> result;
                    result.ok = env.ok;
                    result.errorCode = env.errorCode;
                    result.networkError = env.networkError;
                    result.error = env.message;
                    if (env.ok) {
                        QStringList issues;
                        QString reason;
                        QList<PileInfo> piles;
                        if (!socketparse::parsePilesPayload(env.payload, &piles, &issues,
                                                            &reason)) {
                            result.ok = false;
                            result.errorCode = kCodeInvalidRequest;
                            result.error = reason;
                        } else {
                            cachePileIds(piles); // restartPile 的 pile_code→id 前置
                            result.items = piles;
                        }
                    }
                    if (callback)
                        callback(result);
                });
}

// ── 动作类接口(D7 幂等纪律; 超时由 pending 统一处理: 不自动重发、不换 id) ───────

void SocketAdminRepository::restartPile(
    const QString &pileCode, QObject *context,
    std::function<void(const ActionResult &)> callback)
{
    // wire 契约 admin.pile.restart 传 pile_id(protocol.md 已冻结), 接口层只有 pile_code
    // → 用本端 fetchPiles 缓存做 code→id 映射(桩页动作行必然来自刷新后的列表, 正常不缺失)
    const int pileId = m_pileIdByCode.value(pileCode, 0);
    if (pileId <= 0) {
        ActionResult result;
        result.ok = false;
        result.errorCode = kCodeNotFound;
        result.message =
            QStringLiteral("桩 %1 不在本端缓存，请先刷新桩列表后重试").arg(pileCode);
        // 与 Mock 同构: 事件循环派发(不产生网络请求; 无 id 可用无从构造合法 wire payload)
        QTimer::singleShot(0, context, [result, callback] {
            if (callback)
                callback(result);
        });
        return;
    }
    QJsonObject specific;
    specific.insert(QLatin1String("pile_id"), pileId);
    sendRequest(QStringLiteral("admin.pile.restart"), specific, /*isAction=*/true,
                context, [this, pileCode, callback](const ReplyEnvelope &env) {
                    ActionResult result;
                    result.ok = env.ok;
                    result.errorCode = env.errorCode;
                    result.networkError = env.networkError;
                    result.message = env.message;
                    if (env.ok) {
                        result.errorCode = kCodeOk;
                        // 成功响应 payload {pile:{...}}: 刷新本端 code→id 快照
                        const QJsonValue pileValue =
                            env.payload.value(QLatin1String("pile"));
                        if (pileValue.isObject()) {
                            QStringList issues;
                            const PileInfo pile =
                                socketparse::parsePile(pileValue.toObject(), &issues);
                            if (pile.id > 0 && !pile.pileCode.isEmpty())
                                m_pileIdByCode.insert(pile.pileCode, pile.id);
                        }
                        if (result.message.isEmpty())
                            result.message =
                                QStringLiteral("桩 %1 重启成功（服务端确认）").arg(pileCode);
                    }
                    if (callback)
                        callback(result);
                });
}

void SocketAdminRepository::setUserStatus(
    int userId, const QString &status, QObject *context,
    std::function<void(const ActionResult &)> callback)
{
    // 状态值合法性校验与"重复提交同状态"等业务规则在服务端(协议 1002/1201), 适配层不重复
    QJsonObject specific;
    specific.insert(QLatin1String("user_id"), userId);
    specific.insert(QLatin1String("status"), status);
    sendRequest(QStringLiteral("admin.user.status.set"), specific, /*isAction=*/true,
                context, [callback](const ReplyEnvelope &env) {
                    ActionResult result;
                    result.ok = env.ok;
                    result.errorCode = env.errorCode;
                    result.networkError = env.networkError;
                    result.message = env.message;
                    if (env.ok) {
                        result.errorCode = kCodeOk;
                        if (result.message.isEmpty())
                            result.message = QStringLiteral("操作成功");
                    }
                    if (callback)
                        callback(result);
                });
}

void SocketAdminRepository::cachePileIds(const QList<PileInfo> &piles)
{
    for (const PileInfo &pile : piles) {
        if (pile.id > 0 && !pile.pileCode.isEmpty())
            m_pileIdByCode.insert(pile.pileCode, pile.id);
    }
}

} // namespace ev
