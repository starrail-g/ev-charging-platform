#include <QtTest>

#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <memory>

#include "data/socketadminrepository.h"
#include "ev_protocol/frame_codec.h"
#include "ev_protocol/message.h"
#include "models/adminmodels.h"

// SocketAdminRepository 本地假服务器测试（设计稿 D8：纯本地、不依赖 B）：
//   成功各接口 / 1100 / fetchPiles 游标分页聚合(admin.pile.list) / 整页 error /
//   动作 1200-1201 映射 / 坏帧断连 / 超时 / 中途断连 / context 销毁防悬垂 /
//   连接失败后可重连。
// 假服务器行为经 onRequest 注入（按收到的请求 type 回响应）。

// 本地假管理服务器：按连接持有 FrameDecoder，收到的请求可注入行为
// （Q_OBJECT 类须在全局作用域：匿名 namespace 内 moc 生成代码无法引用）
class FakeAdminServer : public QObject
{
    Q_OBJECT

public:
    explicit FakeAdminServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this,
                &FakeAdminServer::onNewConnection);
    }

    // 清理残留 decoder（socket 由 QTcpServer 持有为 child，随 server 析构统一删除）
    ~FakeAdminServer() override
    {
        qDeleteAll(m_decoders);
        m_decoders.clear();
    }

    bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return m_server.serverPort(); }

    // 每收到一条请求调用；在此按 type 回响应或故意不回/断开
    std::function<void(const ev::protocol::Message &, QTcpSocket *)> onRequest;

private slots:
    void onNewConnection()
    {
        while (m_server.hasPendingConnections()) {
            QTcpSocket *socket = m_server.nextPendingConnection();
            m_sockets.append(socket);
            m_decoders.insert(socket, new ev::protocol::FrameDecoder());
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                const QByteArray chunk = socket->readAll();
                QString error;
                ev::protocol::ErrorCode code = ev::protocol::ErrorCode::Ok;
                const auto messages = m_decoders[socket]->feed(chunk, &error, &code);
                for (const ev::protocol::Message &message : messages) {
                    m_requests.append(message);
                    if (onRequest)
                        onRequest(message, socket);
                }
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                // 只清理本端 decoder；**不得对 socket 调 deleteLater**——
                // Qt6 中 accepted socket 的 parent 是 QTcpServer，server 析构
                // 删除仍在连接中的 socket 时会再次触发 disconnected（栈内调用），
                // 此时 deleteLater 属 UB（Ubuntu 实测 SIGSEGV，Windows 未现）。
                delete m_decoders.take(socket);
            });
        }
    }

public:
    QTcpServer m_server;
    QList<QTcpSocket *> m_sockets;
    QHash<QTcpSocket *, ev::protocol::FrameDecoder *> m_decoders; // 裸指针: QHash 值需可拷贝
    QList<ev::protocol::Message> m_requests; // 收到的全部请求（断言用）
};

namespace {

// 向客户端回一条响应帧（type 由调用方给全名，如 "admin.login.result"）
void reply(QTcpSocket *socket, const QString &id, const QString &type,
           const QJsonObject &payload)
{
    ev::protocol::Message message;
    message.id = id;
    message.type = type;
    message.payload = payload;
    socket->write(ev::protocol::encodeFrame(message));
}

// error 信封（协议 D3：客户端只按 code 分支）
void replyError(QTcpSocket *socket, const QString &id, int code, const QString &message)
{
    reply(socket, id, QStringLiteral("error"),
          QJsonObject{{QStringLiteral("code"), code},
                      {QStringLiteral("name"), QStringLiteral("ERR")},
                      {QStringLiteral("message"), message}});
}

const QJsonObject kAdmin = QJsonObject{
    {QStringLiteral("id"), 1},
    {QStringLiteral("username"), QStringLiteral("admin")},
    {QStringLiteral("role"), QStringLiteral("super_admin")},
    {QStringLiteral("status"), QStringLiteral("active")},
};

// Q6 冻结(2026-09-06, PR #12 = main 3d015f7): admin.login.result = admin 对象 +
// 非空会话 token(服务端 8h 进程内会话)。fake server 用固定 token 模拟。
const QString kSessionToken = QStringLiteral("test-admin-session-token");

QJsonObject loginResultPayload()
{
    return QJsonObject{{QStringLiteral("admin"), kAdmin},
                       {QStringLiteral("token"), kSessionToken}};
}

} // namespace

class TestSocketAdapter : public QObject
{
    Q_OBJECT

private slots:
    void loginSucceedsAndAuthenticates();
    void loginRejectedWith1100();
    void fetchOverviewMapsStatisticsPayload();
    void fetchOverviewMapsEmptyHasDataFlag();
    void authenticatedRequestsCarrySessionToken(); // Q6 冻结契约(2026-09-06)
    void unauthorizedClearsSessionState();
    void fetchPilesAggregatesCursorPages();
    void fetchPilesFailsWholePageOnServerError();
    void restartPileUsesCachedIdAndMapsConflict();
    void requestTimeoutMarksNetworkError();
    void serverAbortFailsInflightAsNetworkError();
    void badFrameResetsConnection();
    void contextDestructionCancelsCallback();
    void reconnectAfterConnectionFailure();
};

void TestSocketAdapter::loginSucceedsAndAuthenticates()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [&server](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.login")) {
            reply(socket, request.id, QStringLiteral("admin.login.result"),
                  loginResultPayload());
        }
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::LoginResult login;
    repository.login(QStringLiteral("admin"), QStringLiteral("123456"), &repository,
                     [&](const ev::LoginResult &result) {
                         login = result;
                         done = true;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY2(login.ok, qPrintable(login.message));
    QCOMPARE(login.errorCode, 0);
    QCOMPARE(login.admin.username, QStringLiteral("admin"));
    QCOMPARE(login.token, kSessionToken); // Q6: 登录响应携带会话 token
    QVERIFY(repository.isAuthenticated());
    QVERIFY(repository.dataSourceName().contains(QStringLiteral("Socket")));
    QCOMPARE(server.m_requests.size(), 1);
}

void TestSocketAdapter::loginRejectedWith1100()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.login"))
            replyError(socket, request.id, 1100, QStringLiteral("invalid credentials"));
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::LoginResult login;
    repository.login(QStringLiteral("admin"), QStringLiteral("wrong"), &repository,
                     [&](const ev::LoginResult &result) {
                         login = result;
                         done = true;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(!login.ok);
    QCOMPARE(login.errorCode, 1100);
    QVERIFY(!login.networkError);
    QVERIFY(!repository.isAuthenticated()); // 1100 不缓存（D6）
}

void TestSocketAdapter::fetchOverviewMapsStatisticsPayload()
{
    // Q3 冻结(2026-09-05, B 1f157de): statistics 无独立 30d 合计键 → fetchOverview
    // 双请求(7d+30d): 7d 为主体(五态/利用率/updated_at), 30d 响应的聚合
    // revenue_cents 填入 revenue30dCents(营收卡副行)
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.statistics.get")) {
            const QString range =
                request.payload.value(QLatin1String("range")).toString();
            QJsonObject body{
                {QStringLiteral("revenue_cents"), 286540},
                {QStringLiteral("pile_idle"), 1},
                {QStringLiteral("pile_reserved"), 1},
                {QStringLiteral("pile_charging"), 2},
                {QStringLiteral("pile_fault"), 1},
                {QStringLiteral("pile_offline"), 1},
                {QStringLiteral("avg_station_utilization"), 0.42},
                {QStringLiteral("updated_at"), QStringLiteral("2026-09-01T10:15:00Z")},
                {QStringLiteral("has_data"), true}, // 冻结 2026-09-07: main 显式返回
            };
            if (range == QLatin1String("30d"))
                body.insert(QStringLiteral("revenue_cents"), 983840); // 30 日聚合值
            reply(socket, request.id, QStringLiteral("admin.statistics.get.result"),
                  QJsonObject{{QStringLiteral("statistics"), body},
                              {QStringLiteral("range"), range}});
        }
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::OverviewResult result;
    repository.fetchOverview(&repository, [&](const ev::OverviewResult &out) {
        result = out;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(result.ok);
    QCOMPARE(result.stats.revenueCents, qint64(286540));      // 7d 主体
    QCOMPARE(result.stats.revenue30dCents, qint64(983840));   // 30d 请求聚合值
    QCOMPARE(result.stats.pileIdle, 1);
    QCOMPARE(result.stats.pileFault, 1);
    QCOMPARE(result.stats.pileOffline, 1);
    QCOMPARE(result.stats.avgStationUtilization, 0.42);
    QCOMPARE(result.stats.updatedAt, QStringLiteral("2026-09-01T10:15:00Z"));
    QVERIFY(result.hasData); // statistics.has_data: true → 有数据(非空库)
    // 双请求: range 7d + 30d 各一次
    QCOMPARE(server.m_requests.size(), 2);
    bool saw7d = false;
    bool saw30d = false;
    for (const ev::protocol::Message &req : server.m_requests) {
        const QString r = req.payload.value(QLatin1String("range")).toString();
        saw7d = saw7d || r == QLatin1String("7d");
        saw30d = saw30d || r == QLatin1String("30d");
    }
    QVERIFY(saw7d && saw30d);
}

void TestSocketAdapter::fetchOverviewMapsEmptyHasDataFlag()
{
    // has_data=false 空库语义(冻结 2026-09-07, main getStatistics):
    // 空库 → OverviewResult.hasData=false(概览页走"暂无概览数据"空态),
    // 不展示一组 0 值指标冒充正常数据
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.statistics.get")) {
            const QJsonObject body{
                {QStringLiteral("revenue_cents"), 0},
                {QStringLiteral("pile_idle"), 0},
                {QStringLiteral("pile_reserved"), 0},
                {QStringLiteral("pile_charging"), 0},
                {QStringLiteral("pile_fault"), 0},
                {QStringLiteral("pile_offline"), 0},
                {QStringLiteral("avg_station_utilization"), 0.0},
                {QStringLiteral("updated_at"), QStringLiteral("2026-09-07T00:00:00Z")},
                {QStringLiteral("has_data"), false}, // 空库: 无桩无完成订单
            };
            reply(socket, request.id, QStringLiteral("admin.statistics.get.result"),
                  QJsonObject{{QStringLiteral("statistics"), body},
                              {QStringLiteral("range"),
                               request.payload.value(QLatin1String("range"))}});
        }
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::OverviewResult result;
    repository.fetchOverview(&repository, [&](const ev::OverviewResult &out) {
        result = out;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.errorCode, 0);
    QVERIFY(!result.hasData); // 空库 has_data=false → 概览空态
}

void TestSocketAdapter::authenticatedRequestsCarrySessionToken()
{
    // Q6 冻结(2026-09-06, PR #12 = main 3d015f7): 除 admin.login 外所有 admin.*
    // 请求携带会话 token; mutation(admin.pile.restart / admin.user.status.set)
    // 额外携带 administrator_id 且 = login 响应 admin.id; 读类(admin.station.list /
    // admin.pile.list / admin.user.list)只带 token; 管理端不再调用用户侧 pile.list
    // (口径 A, 2026-09-07: fetchPiles 走 admin.pile.list 全量);
    // username/password 绝不再现(hasOnlyFields 严格拒多余字段)。
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.login")) {
            reply(socket, request.id, QStringLiteral("admin.login.result"),
                  loginResultPayload());
        } else if (request.type == QStringLiteral("admin.station.list")) {
            reply(socket, request.id, QStringLiteral("admin.station.list.result"),
                  QJsonObject{{QStringLiteral("stations"), QJsonArray{
                      QJsonObject{{QStringLiteral("id"), 1},
                                  {QStringLiteral("status"), QStringLiteral("active")},
                                  {QStringLiteral("pile_total"), 1}}}}});
        } else if (request.type == QStringLiteral("admin.user.list")) {
            reply(socket, request.id, QStringLiteral("admin.user.list.result"),
                  QJsonObject{{QStringLiteral("users"), QJsonArray{}}});
        } else if (request.type == QStringLiteral("admin.pile.list")) {
            reply(socket, request.id, QStringLiteral("admin.pile.list.result"),
                  QJsonObject{{QStringLiteral("piles"), QJsonArray{
                      QJsonObject{{QStringLiteral("id"), 3},
                                  {QStringLiteral("station_id"), 1},
                                  {QStringLiteral("pile_code"), QStringLiteral("P-101-C")},
                                  {QStringLiteral("status"), QStringLiteral("fault")}}}}});
        } else if (request.type == QStringLiteral("admin.pile.restart")) {
            reply(socket, request.id, QStringLiteral("admin.pile.restart.result"),
                  QJsonObject{{QStringLiteral("pile"), QJsonObject{
                      {QStringLiteral("id"), 3},
                      {QStringLiteral("pile_code"), QStringLiteral("P-101-C")},
                      {QStringLiteral("status"), QStringLiteral("idle")}}}});
        } else if (request.type == QStringLiteral("admin.user.status.set")) {
            reply(socket, request.id, QStringLiteral("admin.user.status.set.result"),
                  QJsonObject{{QStringLiteral("user"), QJsonObject{
                      {QStringLiteral("id"), 1},
                      {QStringLiteral("status"), QStringLiteral("frozen")}}}});
        }
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool loggedIn = false;
    ev::LoginResult login;
    repository.login(QStringLiteral("admin"), QStringLiteral("123456"), &repository,
                     [&](const ev::LoginResult &result) {
                         login = result;
                         loggedIn = true;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(loggedIn, 3000);
    QVERIFY2(login.ok, qPrintable(login.message));
    QVERIFY(repository.isAuthenticated());

    // 读类：admin.station.list / admin.user.list
    bool stationsDone = false;
    repository.fetchStations(&repository,
                             [&](const ev::ListResult<ev::StationInfo> &) {
                                 stationsDone = true;
                             });
    QTRY_VERIFY_WITH_TIMEOUT(stationsDone, 3000);
    bool usersDone = false;
    repository.fetchUsers(&repository, [&](const ev::ListResult<ev::UserInfo> &) {
        usersDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(usersDone, 3000);

    // fetchPiles 拿桩缓存(restartPile 需要 pile_id): admin.pile.list 游标页聚合
    bool pilesDone = false;
    repository.fetchPiles(&repository, [&](const ev::ListResult<ev::PileInfo> &) {
        pilesDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(pilesDone, 5000);

    // mutation：restartPile / setUserStatus
    bool restartDone = false;
    repository.restartPile(QStringLiteral("P-101-C"), &repository,
                           [&](const ev::ActionResult &) {
                               restartDone = true;
                           });
    QTRY_VERIFY_WITH_TIMEOUT(restartDone, 3000);
    bool statusDone = false;
    repository.setUserStatus(1, QStringLiteral("frozen"), &repository,
                             [&](const ev::ActionResult &) {
                                 statusDone = true;
                             });
    QTRY_VERIFY_WITH_TIMEOUT(statusDone, 3000);

    // 请求级断言（token/mutation administrator_id 附加规则）
    bool sawLogin = false;
    bool sawRead = false;
    bool sawMutation = false;
    for (const ev::protocol::Message &req : server.m_requests) {
        if (req.type == QStringLiteral("admin.login")) {
            sawLogin = true;
            QVERIFY(!req.payload.contains(QLatin1String("token"))); // login 本身不带 token
            continue;
        }
        if (req.type == QStringLiteral("admin.station.list")
            || req.type == QStringLiteral("admin.pile.list")
            || req.type == QStringLiteral("admin.user.list")) {
            sawRead = true;
            // 读类：token 必须携带、不带 administrator_id/username/password
            QCOMPARE(req.payload.value(QLatin1String("token")).toString(), kSessionToken);
            QVERIFY(!req.payload.contains(QLatin1String("administrator_id")));
            QVERIFY(!req.payload.contains(QLatin1String("username")));
            QVERIFY(!req.payload.contains(QLatin1String("password")));
            continue;
        }
        if (req.type == QStringLiteral("admin.pile.restart")
            || req.type == QStringLiteral("admin.user.status.set")) {
            sawMutation = true;
            // mutation：token + administrator_id(= login admin.id=1)
            QCOMPARE(req.payload.value(QLatin1String("token")).toString(), kSessionToken);
            QCOMPARE(req.payload.value(QLatin1String("administrator_id")).toInt(), 1);
            continue;
        }
    }
    // 管理端全部请求均为 admin.*(口径 A 后不再调用用户侧 pile.list)
    QVERIFY(sawLogin && sawRead && sawMutation);
    for (const ev::protocol::Message &req : server.m_requests)
        QVERIFY2(req.type.startsWith(QLatin1String("admin.")),
                 "管理端不得再调用用户侧接口");
}

void TestSocketAdapter::unauthorizedClearsSessionState()
{
    // Q6(2026-09-06): 登录后的 admin.* 请求收到 1100(会话过期/不匹配) →
    // 数据层立即清除 token 与认证状态; 后续请求不再携带 token(服务端会再次 1100,
    // 页面应回登录, 数据层只保证不残留凭据)。
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [&server](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.login")) {
            reply(socket, request.id, QStringLiteral("admin.login.result"),
                  loginResultPayload());
        } else if (request.type == QStringLiteral("admin.statistics.get")) {
            replyError(socket, request.id, 1100,
                       QStringLiteral("administrator token is missing or expired"));
        } else if (request.type == QStringLiteral("admin.user.list")) {
            reply(socket, request.id, QStringLiteral("admin.user.list.result"),
                  QJsonObject{{QStringLiteral("users"), QJsonArray{}}});
        }
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool loggedIn = false;
    repository.login(QStringLiteral("admin"), QStringLiteral("123456"), &repository,
                     [&](const ev::LoginResult &) {
                         loggedIn = true;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(loggedIn, 3000);
    QVERIFY(repository.isAuthenticated());

    // 概览请求被服务端 1100 拒绝 → 会话清除
    bool overviewDone = false;
    ev::OverviewResult overview;
    repository.fetchOverview(&repository, [&](const ev::OverviewResult &out) {
        overview = out;
        overviewDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(overviewDone, 5000); // 双请求任一失败即整页 1100
    QVERIFY(!overview.ok);
    QCOMPARE(overview.errorCode, 1100);
    QVERIFY(!repository.isAuthenticated()); // 1100 → 认证上下文已清

    // 之后的 admin.* 请求不再携带 token(fake server 记录断言)
    bool usersDone = false;
    repository.fetchUsers(&repository, [&](const ev::ListResult<ev::UserInfo> &) {
        usersDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(usersDone, 3000);
    for (const ev::protocol::Message &req : server.m_requests) {
        if (req.type == QStringLiteral("admin.user.list"))
            QVERIFY2(!req.payload.contains(QLatin1String("token")),
                     "会话失效后请求不得携带旧 token");
    }
}

void TestSocketAdapter::fetchPilesAggregatesCursorPages()
{
    // 口径 A 保持管理员全量视图（含 inactive 站桩），但 wire 层必须在 1 MiB
    // 帧上限内用 next_after_id 游标分页；适配层对页面仍返回聚合后的全量列表。
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.pile.list")) {
            const qint64 afterId = request.payload.value(QStringLiteral("after_id")).toInteger();
            if (afterId == 0) {
                reply(socket, request.id, QStringLiteral("admin.pile.list.result"),
                      QJsonObject{{QStringLiteral("piles"), QJsonArray{
                          QJsonObject{{QStringLiteral("id"), 1},
                                      {QStringLiteral("station_id"), 1},
                                      {QStringLiteral("pile_code"), QStringLiteral("P-101-A")},
                                      {QStringLiteral("status"), QStringLiteral("charging")}},
                          QJsonObject{{QStringLiteral("id"), 2},
                                      {QStringLiteral("station_id"), 1},
                                      {QStringLiteral("pile_code"), QStringLiteral("P-101-B")},
                                      {QStringLiteral("status"), QStringLiteral("idle")}},
                      }}, {QStringLiteral("next_after_id"), 2}});
                return;
            }
            QCOMPARE(afterId, qint64(2));
            reply(socket, request.id, QStringLiteral("admin.pile.list.result"),
                  QJsonObject{{QStringLiteral("piles"), QJsonArray{
                      QJsonObject{{QStringLiteral("id"), 3},
                                  {QStringLiteral("station_id"), 2}, // 停运站桩也全量返回
                                  {QStringLiteral("pile_code"), QStringLiteral("P-202-C")},
                                  {QStringLiteral("status"), QStringLiteral("fault")}},
                  }}});
        }
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::ListResult<ev::PileInfo> result;
    repository.fetchPiles(&repository, [&](const ev::ListResult<ev::PileInfo> &out) {
        result = out;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.items.size(), 3); // 全量: 不含站状态过滤

    // 两页均只用管理员桩接口；不再先 station.list 或逐站 pile.list。
    QCOMPARE(server.m_requests.size(), 2);
    QCOMPARE(server.m_requests.at(0).type, QStringLiteral("admin.pile.list"));
    QCOMPARE(server.m_requests.at(1).type, QStringLiteral("admin.pile.list"));
    QCOMPARE(server.m_requests.at(0).payload.value(QStringLiteral("after_id")).toInteger(),
             qint64(0));
    QCOMPARE(server.m_requests.at(1).payload.value(QStringLiteral("after_id")).toInteger(),
             qint64(2));
    QCOMPARE(server.m_requests.at(0).payload.value(QStringLiteral("limit")).toInteger(),
             qint64(100));

    // 桩缓存(restartPile 的 pile_code→id 前置)来自同一响应
    QVERIFY(repository.cachedPileCount() >= 3);
}

void TestSocketAdapter::fetchPilesFailsWholePageOnServerError()
{
    // admin.pile.list 失败 → 整页 error 透传(协议码分支不丢失)
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.pile.list"))
            replyError(socket, request.id, 1100,
                       QStringLiteral("administrator token is missing or expired"));
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::ListResult<ev::PileInfo> result;
    repository.fetchPiles(&repository, [&](const ev::ListResult<ev::PileInfo> &out) {
        result = out;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, 1100); // 整页协议错误透传
    QVERIFY(!result.networkError);
}

void TestSocketAdapter::restartPileUsesCachedIdAndMapsConflict()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [&server](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.pile.list")) {
            // 全量桩列表(口径 A): 缓存桩 code→id 供 restartPile 使用
            reply(socket, request.id, QStringLiteral("admin.pile.list.result"),
                  QJsonObject{{QStringLiteral("piles"), QJsonArray{
                      QJsonObject{{QStringLiteral("id"), 3},
                                  {QStringLiteral("station_id"), 1},
                                  {QStringLiteral("pile_code"), QStringLiteral("P-101-C")},
                                  {QStringLiteral("status"), QStringLiteral("fault")}},
                  }}});
        } else if (request.type == QStringLiteral("admin.pile.restart")) {
            // 服务端按状态机拒绝（fault 桩重启冲突场景之外的桩）→ 1201 透传
            replyError(socket, request.id, 1201, QStringLiteral("pile state conflict"));
        }
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());

    // 先刷新桩列表 → 缓存 pile_code→id（restartPile 的 wire 前置）
    bool fetched = false;
    repository.fetchPiles(&repository, [&](const ev::ListResult<ev::PileInfo> &) {
        fetched = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(fetched, 5000);
    QVERIFY(repository.cachedPileCount() >= 1);

    // 重启缓存外的桩 → 1200 + 本地提示（不产生网络请求）
    bool done = false;
    ev::ActionResult localMissing;
    repository.restartPile(QStringLiteral("P-XXX"), &repository,
                           [&](const ev::ActionResult &out) {
                               localMissing = out;
                               done = true;
                           });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(!localMissing.ok);
    QCOMPARE(localMissing.errorCode, 1200);
    QVERIFY(localMissing.message.contains(QStringLiteral("刷新")));

    // 重启缓存内桩 → admin.pile.restart{pile_id:3} → 服务端 1201 透传
    done = false;
    ev::ActionResult conflict;
    repository.restartPile(QStringLiteral("P-101-C"), &repository,
                           [&](const ev::ActionResult &out) {
                               conflict = out;
                               done = true;
                           });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(!conflict.ok);
    QCOMPARE(conflict.errorCode, 1201);
    QVERIFY(!conflict.networkError);
}

void TestSocketAdapter::requestTimeoutMarksNetworkError()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    // 不回任何响应（超时路径；动作类超时不自动重发、不换 id —— 只发一帧）
    server.onRequest = [](const ev::protocol::Message &, QTcpSocket *) {};

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    repository.setRequestTimeoutMs(150);
    bool done = false;
    ev::ActionResult result;
    repository.setUserStatus(1, QStringLiteral("frozen"), &repository,
                             [&](const ev::ActionResult &out) {
                                 result = out;
                                 done = true;
                             });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(!result.ok);
    QVERIFY(result.networkError);
    QCOMPARE(result.errorCode, 0); // 传输层失败不冒充协议码（D3）
    QCOMPARE(repository.pendingCount(), 0); // 超时后 pending 清空
    QCOMPARE(server.m_requests.size(), 1);  // 动作不自动重发（D7）
}

void TestSocketAdapter::serverAbortFailsInflightAsNetworkError()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &, QTcpSocket *socket) {
        socket->abort(); // 响应前中断
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::LoginResult login;
    repository.login(QStringLiteral("admin"), QStringLiteral("123456"), &repository,
                     [&](const ev::LoginResult &result) {
                         login = result;
                         done = true;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(!login.ok);
    QVERIFY(login.networkError);
    QCOMPARE(login.errorCode, 0);
    QCOMPARE(repository.pendingCount(), 0);
}

void TestSocketAdapter::badFrameResetsConnection()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.login"))
            socket->write(QByteArray("\x00\x00\x00\x01!", 5)); // 坏帧：长度前缀合法但 JSON 无效
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::LoginResult login;
    repository.login(QStringLiteral("admin"), QStringLiteral("123456"), &repository,
                     [&](const ev::LoginResult &result) {
                         login = result;
                         done = true;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(!login.ok);
    QVERIFY(login.networkError); // 坏帧 → 传输层失败 + 连接重置（D3/D8）
    QCOMPARE(repository.pendingCount(), 0);
}

void TestSocketAdapter::contextDestructionCancelsCallback()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &, QTcpSocket *) {}; // 不回

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool called = false;
    {
        QObject context;
        // 双请求(7d+30d)均登记 pending; context 销毁 → 两个一起移除
        repository.fetchOverview(&context, [&](const ev::OverviewResult &) {
            called = true;
        });
        QTRY_COMPARE_WITH_TIMEOUT(repository.pendingCount(), 2, 2000);
    } // context 销毁 → pending 移除
    QTest::qWait(300);
    QVERIFY(!called);
    QCOMPARE(repository.pendingCount(), 0); // 防悬垂：不再持有已销毁 context 的请求
}

void TestSocketAdapter::reconnectAfterConnectionFailure()
{
    // 第一次：无服务监听 → 连接失败（networkError）
    bool failed = false;
    ev::LoginResult first;
    {
        ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), 1); // 端口 1 必无监听
        repository.login(QStringLiteral("admin"), QStringLiteral("123456"), &repository,
                         [&](const ev::LoginResult &result) {
                             first = result;
                             failed = true;
                         });
        // Windows 上连接被拒的 errorOccurred 可能延迟数秒（TCP 栈行为）
        QTRY_VERIFY_WITH_TIMEOUT(failed, 8000);
        QVERIFY(!first.ok);
        QVERIFY(first.networkError);
    }

    // 随后服务上线 → 新实例（QObject 不可拷贝）连接成功
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.login"))
            reply(socket, request.id, QStringLiteral("admin.login.result"),
                  loginResultPayload());
    };
    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::LoginResult second;
    repository.login(QStringLiteral("admin"), QStringLiteral("123456"), &repository,
                     [&](const ev::LoginResult &result) {
                         second = result;
                         done = true;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY2(second.ok, qPrintable(second.message));
    QCOMPARE(second.token, kSessionToken);
}

QTEST_MAIN(TestSocketAdapter)
#include "tst_socketadapter.moc"
