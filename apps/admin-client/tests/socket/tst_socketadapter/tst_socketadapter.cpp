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
//   成功各接口 / 1100 / fan-out 两站 / 任一站失败整页 error / 动作 1200-1201 映射 /
//   坏帧断连 / 超时 / 中途断连 / context 销毁防悬垂 / 连接失败后可重连。
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

} // namespace

class TestSocketAdapter : public QObject
{
    Q_OBJECT

private slots:
    void loginSucceedsAndAuthenticates();
    void loginRejectedWith1100();
    void fetchOverviewMapsStatisticsPayload();
    void authenticatedRequestCarriesAdministratorIdOnly(); // Q6 冻结契约
    void fetchPilesFanOutAcrossStations();
    void fetchPilesFailsWholePageOnStationError();
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
                  QJsonObject{{QStringLiteral("admin"), kAdmin}});
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

void TestSocketAdapter::authenticatedRequestCarriesAdministratorIdOnly()
{
    // Q6 冻结(2026-09-05, PR #10 代码实证): v1 无 token/连接级会话; 已认证的
    // admin.* 请求(admin.login 除外)payload 携带 administrator_id(= login 响应
    // admin.id), 不再附加 username/password——B hasOnlyFields 严格拒多余字段
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.login"))
            reply(socket, request.id, QStringLiteral("admin.login.result"),
                  QJsonObject{{QStringLiteral("admin"), kAdmin}});
        else if (request.type == QStringLiteral("admin.station.list"))
            reply(socket, request.id, QStringLiteral("admin.station.list.result"),
                  QJsonObject{{QStringLiteral("stations"), QJsonArray()}});
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

    bool done = false;
    ev::ListResult<ev::StationInfo> stations;
    repository.fetchStations(&repository, [&](const ev::ListResult<ev::StationInfo> &out) {
        stations = out;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY2(stations.ok, qPrintable(stations.error));

    QCOMPARE(server.m_requests.size(), 2);
    bool sawLogin = false;
    bool sawList = false;
    for (const ev::protocol::Message &req : server.m_requests) {
        sawLogin = sawLogin || req.type == QStringLiteral("admin.login");
        sawList = sawList || req.type == QStringLiteral("admin.station.list");
        if (req.type == QStringLiteral("admin.station.list")) {
            // 已认证请求: administrator_id = login 响应 admin.id(kAdmin.id=1)
            QCOMPARE(req.payload.value(QLatin1String("administrator_id")).toInt(), 1);
            QVERIFY(!req.payload.contains(QLatin1String("username")));
            QVERIFY(!req.payload.contains(QLatin1String("password")));
        }
    }
    QVERIFY(sawLogin && sawList);
}

void TestSocketAdapter::fetchPilesFanOutAcrossStations()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [&server](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.station.list")) {
            reply(socket, request.id, QStringLiteral("admin.station.list.result"),
                  QJsonObject{{QStringLiteral("stations"), QJsonArray{
                      QJsonObject{{QStringLiteral("id"), 1},
                                  {QStringLiteral("name"), QStringLiteral("站一")},
                                  {QStringLiteral("pile_total"), 3}},
                      QJsonObject{{QStringLiteral("id"), 2},
                                  {QStringLiteral("name"), QStringLiteral("站二")},
                                  {QStringLiteral("pile_total"), 3}},
                  }}});
        } else if (request.type == QStringLiteral("pile.list")) {
            const int stationId = request.payload.value(QStringLiteral("station_id")).toInt();
            QJsonArray piles;
            if (stationId == 1) {
                piles = QJsonArray{
                    QJsonObject{{QStringLiteral("id"), 1},
                                {QStringLiteral("station_id"), 1},
                                {QStringLiteral("pile_code"), QStringLiteral("P-101-A")},
                                {QStringLiteral("status"), QStringLiteral("charging")}},
                    QJsonObject{{QStringLiteral("id"), 2},
                                {QStringLiteral("station_id"), 1},
                                {QStringLiteral("pile_code"), QStringLiteral("P-101-B")},
                                {QStringLiteral("status"), QStringLiteral("idle")}},
                };
            } else if (stationId == 2) {
                piles = QJsonArray{
                    QJsonObject{{QStringLiteral("id"), 4},
                                {QStringLiteral("station_id"), 2},
                                {QStringLiteral("pile_code"), QStringLiteral("P-202-A")},
                                {QStringLiteral("status"), QStringLiteral("fault")}},
                };
            }
            reply(socket, request.id, QStringLiteral("pile.list.result"),
                  QJsonObject{{QStringLiteral("piles"), piles}});
        }
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::ListResult<ev::PileInfo> result;
    repository.fetchPiles(&repository, [&](const ev::ListResult<ev::PileInfo> &out) {
        result = out;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.items.size(), 3); // 2 + 1 逐站聚合

    // fan-out 语义：先 station.list，后两站 pile.list（含各自 station_id）
    QCOMPARE(server.m_requests.size(), 3);
    QCOMPARE(server.m_requests.at(0).type, QStringLiteral("admin.station.list"));
    QHash<int, int> stationRequests;
    for (int i = 1; i < server.m_requests.size(); ++i) {
        QCOMPARE(server.m_requests.at(i).type, QStringLiteral("pile.list"));
        const int stationId =
            server.m_requests.at(i).payload.value(QStringLiteral("station_id")).toInt();
        stationRequests[stationId] += 1;
    }
    QCOMPARE(stationRequests.value(1), 1);
    QCOMPARE(stationRequests.value(2), 1);
}

void TestSocketAdapter::fetchPilesFailsWholePageOnStationError()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.station.list")) {
            reply(socket, request.id, QStringLiteral("admin.station.list.result"),
                  QJsonObject{{QStringLiteral("stations"), QJsonArray{
                      QJsonObject{{QStringLiteral("id"), 1}},
                      QJsonObject{{QStringLiteral("id"), 2}},
                  }}});
        } else if (request.type == QStringLiteral("pile.list")) {
            const int stationId = request.payload.value(QStringLiteral("station_id")).toInt();
            if (stationId == 2)
                replyError(socket, request.id, 1200, QStringLiteral("station not found"));
            else
                reply(socket, request.id, QStringLiteral("pile.list.result"),
                      QJsonObject{{QStringLiteral("piles"), QJsonArray{}}});
        }
    };

    ev::SocketAdminRepository repository(QStringLiteral("127.0.0.1"), server.port());
    bool done = false;
    ev::ListResult<ev::PileInfo> result;
    repository.fetchPiles(&repository, [&](const ev::ListResult<ev::PileInfo> &out) {
        result = out;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, 1200); // 任一站失败 → 整页协议错误（不缺站静默）
    QVERIFY(!result.networkError);
}

void TestSocketAdapter::restartPileUsesCachedIdAndMapsConflict()
{
    FakeAdminServer server;
    QVERIFY(server.listen());
    server.onRequest = [&server](const ev::protocol::Message &request, QTcpSocket *socket) {
        if (request.type == QStringLiteral("admin.station.list")) {
            reply(socket, request.id, QStringLiteral("admin.station.list.result"),
                  QJsonObject{{QStringLiteral("stations"), QJsonArray{
                      QJsonObject{{QStringLiteral("id"), 1}},
                  }}});
        } else if (request.type == QStringLiteral("pile.list")) {
            reply(socket, request.id, QStringLiteral("pile.list.result"),
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
                  QJsonObject{{QStringLiteral("admin"), kAdmin}});
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
}

QTEST_MAIN(TestSocketAdapter)
#include "tst_socketadapter.moc"
