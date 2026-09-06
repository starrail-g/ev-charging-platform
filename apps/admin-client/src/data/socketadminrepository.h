#ifndef SOCKETADMINREPOSITORY_H
#define SOCKETADMINREPOSITORY_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

#include "adminrepository.h"

class QTcpSocket;
class QTimer;

namespace ev {
namespace protocol {
class FrameDecoder;
struct Message;
} // namespace protocol
} // namespace ev

namespace ev {

// 管理端 Socket 适配层(设计稿 superpowers/plans/2026-09-03-socket-admin-repository-design.md,
// D1-D7; 9/6 Socket 接线, 9/7 闸门前默认仍走 Mock):
//   D1 协议栈复用 libs/protocol(Message/encodeFrame/FrameDecoder), 不复制;
//   D2 单条 QTcpSocket 懒连接 + 请求 id('c-admin-<递增>') 路由 + 10s 超时 + 断线后下次
//      调用自动重连 + context 防悬垂(destroyed 即清 pending, 回调前 QPointer 双保险);
//   D3 错误映射: 协议 error 信封按 payload.code 分支(errorCode), 传输层失败
//      (连接失败/超时/断连/坏帧) → networkError=true + errorCode=0, 不冒充协议码;
//   D5 fetchPiles 逐站聚合: admin.station.list → 每站 pile.list(station_id)(并行独立 id),
//      任一站失败 → 整页 error(不一致的全量视图不可静默缺站);
//   D6 鉴权(Q6 冻结 2026-09-06, PR #12 = main 3d015f7): admin.login 响应发放
//      进程内 8h 会话 token; 除 admin.login 外所有 admin.* 请求 payload 携带
//      token; mutation(admin.station.create/admin.pile.restart/admin.user.status.set)
//      额外携带 administrator_id 且须与 token 主体一致(不匹配 1100);
//      hasOnlyFields 严格拒多余字段 → payload 组装收敛 buildPayload() 单点;
//      login 成功缓存 token+admin, 收到 1100 即清空(会话失效需重登);
//   D7 动作类请求超时**不自动重发、不换 id**(服务端按请求 id 幂等, 结果未知提示),
//      查询类无此限制(由调用方按需重试)。
class SocketAdminRepository : public QObject, public AdminRepository
{
public:
    // host/port 为空时读 EV_SERVER_HOST / EV_SERVER_PORT 环境变量, 默认 127.0.0.1:45454
    // (与 server/README.md 与 libs/protocol 同约定)。
    explicit SocketAdminRepository(const QString &host = QString(), quint16 port = 0,
                                   QObject *parent = nullptr);

    ~SocketAdminRepository() override;

    // ---- ev::AdminRepository ----
    void login(const QString &username, const QString &password,
               QObject *context,
               std::function<void(const LoginResult &)> callback) override;

    void fetchOverview(QObject *context,
                       std::function<void(const OverviewResult &)> callback) override;

    void fetchPiles(QObject *context,
                    std::function<void(const ListResult<PileInfo> &)> callback) override;

    void fetchStations(QObject *context,
                       std::function<void(const ListResult<StationInfo> &)> callback) override;

    void fetchUsers(QObject *context,
                    std::function<void(const ListResult<UserInfo> &)> callback) override;

    // 数据来源标识(状态栏展示): "Socket(127.0.0.1:45454)"(含 host:port 便于区分实例)
    QString dataSourceName() const override;

    void restartPile(const QString &pileCode,
                     QObject *context,
                     std::function<void(const ActionResult &)> callback) override;

    void setUserStatus(int userId, const QString &status,
                       QObject *context,
                       std::function<void(const ActionResult &)> callback) override;

    // ---- 测试/诊断扩展(不进抽象接口) ----
    // 超时注入: 默认 10000ms(对齐 LoginPage 现有 10s 口径); 假服务器测试调小以缩短用例
    void setRequestTimeoutMs(int ms) { m_timeoutMs = ms; }
    int requestTimeoutMs() const { return m_timeoutMs; }
    // 在途请求数(测试断言: 超时/断连/context 销毁后 pending 清空)
    int pendingCount() const { return m_pending.size(); }
    // 认证态(login 成功且未被 1100 拒绝)
    bool isAuthenticated() const { return m_authenticated; }
    // 本端缓存桩数(桩页动作前 fetchPiles 刷新, 正常流程非零)
    int cachedPileCount() const { return m_pileIdByCode.size(); }

private:
    // 内部响应信封: ok=true 为成功 .result(带 payload);
    // ok=false + networkError=true 为传输层失败(不冒充协议码);
    // ok=false + networkError=false + errorCode 为协议 error 信封(code 分支依据)
    struct ReplyEnvelope {
        bool ok = false;
        bool networkError = false;
        int errorCode = 0;
        QString message;      // 仅展示/日志, 不作为分支依据
        QJsonObject payload;  // 成功响应的 payload
    };

    struct Pending {
        QPointer<QObject> context;      // 回调投递前的存活校验(防悬垂)
        QObject *rawContext = nullptr;  // context 原始指针(区分"从未给过"与"已销毁")
        QString requestType;            // 期望成功响应 type = requestType + ".result"
        bool isAction = false;          // 动作类: 超时不自动重发(幂等纪律 D7)
        QTimer *timer = nullptr;        // 每请求超时定时器(parent = this)
        QMetaObject::Connection destroyedConnection; // context 销毁 → 移除 pending
        std::function<void(const ReplyEnvelope &)> callback;
    };

    // 统一请求入口: 登记 pending → buildPayload 单点组装 → 编码 → 懒连接发送
    void sendRequest(const QString &type, const QJsonObject &specific, bool isAction,
                     QObject *context, std::function<void(const ReplyEnvelope &)> callback);

    // D6/Q6 冻结(2026-09-06, PR #12): admin.*(除 admin.login)携带 token;
    // mutation 额外携带 administrator_id(buildPayload 单点, 见 cpp 注释)
    QJsonObject buildPayload(const QString &type, const QJsonObject &specific) const;

    QString generateRequestId(); // 'c-admin-<全局递增>' (请求 id 全局唯一, 不跨请求复用)

    void ensureConnected();       // 懒连接: 无连接/已断开时发起; 已连接则冲刷待发帧
    void flushOutbox();
    void queueOrWrite(const QString &id, const QByteArray &frame);

    void removePending(const QString &id);          // 删定时器/断 connect/出 outbox
    void failPending(const QString &id, bool networkError, const QString &message);
    void failAllInFlight(bool networkError, const QString &message);
    void onPendingTimeout(const QString &id);       // D3: 超时 → networkError
    void onSocketError();                           // D3: 连接失败/中断 → 全清 networkError
    void onSocketDisconnected();
    void onSocketReadyRead();                       // FrameDecoder 增量解码 + 派发
    void handleIncomingMessage(const ev::protocol::Message &message); // 按 id 查 pending 派发

    // 登录结果派发后的认证上下文维护(1100/结构错不缓存)
    void applyLoginOutcome(bool ok, int errorCode, const AdminInfo &admin,
                           const QString &token);

    // fetchPiles 逐站聚合内部状态(跨多次 sendRequest 闭包共享, shared_ptr 生命周期托管)
    struct PileFanOutState {
        bool delivered = false;          // 整页结果只派发一次
        int remaining = 0;               // 未回 pile.list 数
        bool failed = false;             // 任一站失败 → 整页 error
        int errorCode = 0;
        bool networkError = false;
        QString error;
        QList<PileInfo> piles;
    };

    void deliverPileFanOut(const std::shared_ptr<PileFanOutState> &state,
                           const QList<StationInfo> &stations,
                           QObject *context,
                           const std::function<void(const ListResult<PileInfo> &)> &callback);
    void failPileFanOut(const std::shared_ptr<PileFanOutState> &state,
                        const ReplyEnvelope &env,
                        const std::function<void(const ListResult<PileInfo> &)> &callback);
    void cachePileIds(const QList<PileInfo> &piles); // pile_code→id(restartPile wire 需 pile_id)

    QTcpSocket *m_socket = nullptr;
    std::unique_ptr<ev::protocol::FrameDecoder> m_decoder; // 增量帧解码(连接生命周期内复用)
    QHash<QString, Pending> m_pending;
    QList<QPair<QString, QByteArray>> m_outbox; // {id, frame}: 连接建立前暂存, 连接后冲刷
    QString m_host;
    quint16 m_port = 0;
    int m_timeoutMs = 10000;
    int m_requestSeq = 0;
    bool m_connectPhase = false; // 连接建立阶段标志: socket error 文案区分"无法连接"vs"中断"

    // D6/Q6 认证上下文(login 成功缓存; 1100/结构错不缓存):
    // 认证上下文(Q6 冻结 2026-09-06): login 成功缓存 token + admin;
    // 1100/失败/响应结构错即清空(buildPayload 只对已认证会话附加凭据)
    bool m_authenticated = false;
    QString m_token;
    AdminInfo m_admin;

    // pile_code → id 快照(restartPile 需要 wire pile_id; fetchPiles/restart 响应时刷新)
    QHash<QString, int> m_pileIdByCode;
};

} // namespace ev

#endif // SOCKETADMINREPOSITORY_H
