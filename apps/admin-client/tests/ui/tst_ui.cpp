#include <QtTest>

#include <QtCharts/QCategoryAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QBuffer>
#include <QComboBox>
#include <QDate>
#include <QElapsedTimer>
#include <QGraphicsTextItem>
#include <QHash>
#include <QHostAddress>
#include <QImage>
#include <QLabel>

#include <limits>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QQueue>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QToolButton>

#include "app/mainwindow.h"
#include "data/mockadminrepository.h"
#include "models/adminmodels.h"
#include "pages/loginpage.h"
#include "pages/overviewpage.h"
#include "pages/pilepage.h"
#include "pages/revenuepage.h"
#include "pages/stationpage.h"
#include "pages/userpage.h"
#include "theme/generated/theme_tokens.h"
#include "theme/theme.h"
#include "widgets/aurorabackdrop.h"
#include "widgets/stationtopologywidget.h"
#include "widgets/statusglyphwidget.h"
#include "widgets/statuspulsewidget.h"
#include "widgets/statustag.h"
#include "widgets/revenuechartwidget.h"
#include "widgets/revenuemetriccard.h"
#include "widgets/statestack.h"
#include "widgets/staticmapimageprovider.h"
#include "widgets/staticmapviewport.h"

// ---- T2 假静态图 HTTP 服务器（本地回环，绝不对真实网络发请求）----
// 按入队顺序派发预置响应；响应可延迟写（取消场景）或完全不回（超时场景）。
class FakeStaticMapServer : public QObject
{
    Q_OBJECT

public:
    struct Response {
        int status = 200;
        QByteArray body;
        QByteArray contentType = QByteArrayLiteral("image/png");
        int delayMs = 0;    // 收到请求后延迟毫秒再回包
        bool silent = false; // true = 永不回包（连接挂着等客户端超时）
        int dripMs = 0;      // >0 = 滴答模式：按 dripMs 周期逐 dripChunk 字节发送
        int dripChunk = 256; // （模拟持续慢传输——activity 超时不触发，测总超时）
    };

    explicit FakeStaticMapServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] { acceptPending(); });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1/").arg(port()); }

    void enqueue(const Response &response) { m_responses.enqueue(response); }
    int requestCount = 0;

private:
    void acceptPending()
    {
        // 逐个取完所有等待连接（多请求并发场景）
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            m_buffers.insert(socket, QByteArray());
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                QByteArray &buffer = m_buffers[socket];
                buffer += socket->readAll();
                const int headerEnd = buffer.indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return; // 请求头未收全
                m_buffers.remove(socket);
                ++requestCount;
                serve(socket);
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                m_buffers.remove(socket);
                socket->deleteLater();
            });
        }
    }

    void serve(QTcpSocket *socket)
    {
        const Response response = m_responses.isEmpty()
            ? Response{200, QByteArray(), QByteArrayLiteral("image/png"), 0, true}
            : m_responses.dequeue();
        if (response.silent)
            return; // 不回包：客户端走超时路径
        if (response.dripMs > 0) {
            // 滴答发送：先发完整响应头（QNAM 进入 body 传输态），
            // 再按 dripMs 周期逐 dripChunk 字节发 body——activity 超时不触发，
            // 客户端总超时（deadline）必须兜底
            const QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status)
                + " X\r\nContent-Type: " + response.contentType
                + "\r\nContent-Length: " + QByteArray::number(response.body.size())
                + "\r\n\r\n";
            socket->write(head);
            socket->flush();
            auto *ticker = new QTimer(socket);
            int sent = 0;
            ticker->setInterval(response.dripMs);
            connect(ticker, &QTimer::timeout, socket, [ticker, socket, response, sent]() mutable {
                if (socket->state() != QAbstractSocket::ConnectedState) {
                    ticker->stop();
                    return;
                }
                const int remaining = response.body.size() - sent;
                if (remaining <= 0) {
                    ticker->stop();
                    socket->disconnectFromHost();
                    return;
                }
                const int chunk = qMin(response.dripChunk, remaining);
                socket->write(response.body.constData() + sent, chunk);
                socket->flush();
                sent += chunk;
            });
            ticker->start();
            return;
        }
        const QPointer<QTcpSocket> socketGuard(socket);
        const auto send = [socketGuard, response] {
            // QPointer 保护：客户端可能先 abort 断开——socket 被 disconnected 路径
            // deleteLater 后，延迟回包 lambda 到期不得访问悬垂指针（Ubuntu SIGSEGV，
            // Windows 断开时序不同未现——双平台测试必踩的平台差异）
            if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState)
                return;
            const QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status)
                + " X\r\nContent-Type: " + response.contentType
                + "\r\nContent-Length: " + QByteArray::number(response.body.size())
                + "\r\nConnection: close\r\n\r\n";
            socketGuard->write(head + response.body);
            socketGuard->flush();
            socketGuard->disconnectFromHost();
        };

        if (response.delayMs > 0)
            QTimer::singleShot(response.delayMs, send);
        else
            send();
    }

    QTcpServer m_server;
    QQueue<Response> m_responses;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

// 2×2 PNG（provider 成功路径的响应体）
static QByteArray tinyPngBytes()
{
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// ---- T3 假地图提供者 ----
// 成功模式：fetch 入队 pending，由测试手动 deliver（慢响应/迟到交付可控）；
// 可配置同步失败。cancelAll 故意保留 pending —— 模拟 provider 违约的迟到交付，
// 用于验证 widget 侧 generation 防御（真 provider 的 cancelAll 语义由 T2 用例锁定）。
class FakeMapProvider : public QObject, public ev::MapImageProvider
{
    Q_OBJECT

public:
    struct Request {
        int seq = 0;
        ev::StaticMapView view;
        ev::MapImageProvider::Callback callback;
    };

    explicit FakeMapProvider(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    // ev::MapImageProvider
    bool canFetch() const override { return true; }
    void fetch(const ev::StaticMapView &view, int seq,
               ev::MapImageProvider::Callback callback) override
    {
        ++fetchCount;
        if (m_autoFailure != ev::MapImageProvider::Failure::None) {
            callback(seq, false, QImage(), m_autoFailure); // 同步失败
            return;
        }
        m_requests.append({seq, view, callback});
    }
    void cancelAll() override
    {
        ++cancelCount;
        // 故意不清 pending：迟到交付由测试驱动，验证 widget 的 seq 防御
    }

    void setImage(const QImage &image) { m_image = image; }
    void setAutoFailure(ev::MapImageProvider::Failure failure) { m_autoFailure = failure; }
    int pendingCount() const { return m_requests.size(); }

    void deliverLastSuccess()
    {
        if (m_requests.isEmpty())
            return;
        const Request request = m_requests.takeLast();
        request.callback(request.seq, true, m_image, ev::MapImageProvider::Failure::None);
    }
    void deliverLastFailure(ev::MapImageProvider::Failure failure)
    {
        if (m_requests.isEmpty())
            return;
        const Request request = m_requests.takeLast();
        request.callback(request.seq, false, QImage(), failure);
    }
    void deliverSeq(int seq) // 交付指定序号（superseded 测试：旧响应迟到）
    {
        for (int i = 0; i < m_requests.size(); ++i) {
            if (m_requests.at(i).seq == seq) {
                const Request request = m_requests.takeAt(i);
                request.callback(request.seq, true, m_image,
                                 ev::MapImageProvider::Failure::None);
                return;
            }
        }
    }

    int fetchCount = 0;
    int cancelCount = 0;

private:
    QList<Request> m_requests;
    QImage m_image;
    ev::MapImageProvider::Failure m_autoFailure = ev::MapImageProvider::Failure::None;
};

// 构造站（T3 测试数据）
static ev::StationInfo testStation(int id, double lat, double lng,
                                   const QString &name = QString())
{
    ev::StationInfo station;
    station.id = id;
    station.name = name.isEmpty() ? QStringLiteral("站%1").arg(id) : name;
    station.latitude = lat;
    station.longitude = lng;
    station.status = QStringLiteral("active");
    return station;
}

// 校准演示站对 S1'/S2'（计划附录 B：640×480 取景 → zoom12、已知投影向量）
static QList<ev::StationInfo> demoStationPair()
{
    return {testStation(1, 41.714729, 123.449597),
            testStation(2, 41.805727, 123.440030)};
}

// 与 widget 请求尺寸完全一致的独立期望视图（测试内不复用 widget 内部路径）
static ev::StaticMapView demoMapView(int width, int height)
{
    const QList<ev::StationInfo> stations = demoStationPair();
    QVector<ev::MapLatLng> coordinates;
    for (const ev::StationInfo &station : stations)
        coordinates.append({station.latitude, station.longitude});
    ev::StaticMapView view;
    const bool ok = ev::computeMapView(coordinates, QSize(width, height), &view);
    Q_ASSERT(ok);
    return view;
}

// RevenuePage 测试替身: 每次 fetchOverview 依序派发预置结果(30ms 异步),
// 供空态/错误/零营收/坏序列/过期回包等场景注入
class RevenueFixtureRepository : public ev::AdminRepository
{
public:
    QList<ev::OverviewResult> results;
    int fetchCount = 0;

    void fetchOverview(QObject *,
                       std::function<void(const ev::OverviewResult &)> callback) override
    {
        const ev::OverviewResult result =
            results.at(qMin(fetchCount, results.size() - 1));
        ++fetchCount;
        QTimer::singleShot(30, [callback, result] { callback(result); });
    }

    void login(const QString &, const QString &, QObject *,
               std::function<void(const ev::LoginResult &)>) override {}
    void fetchPiles(QObject *,
                    std::function<void(const ev::ListResult<ev::PileInfo> &)>) override {}
    void fetchStations(QObject *,
                       std::function<void(const ev::ListResult<ev::StationInfo> &)>) override {}
    void fetchUsers(QObject *,
                    std::function<void(const ev::ListResult<ev::UserInfo> &)>) override {}
    void restartPile(const QString &, QObject *,
                     std::function<void(const ev::ActionResult &)>) override {}
    void setUserStatus(int, const QString &, QObject *,
                       std::function<void(const ev::ActionResult &)>) override {}
    QString dataSourceName() const override { return QStringLiteral("Fixture"); }
};

// 手动派发替身: 登录即时成功; fetchOverview 入队由测试手动 deliver(验证过期回包);
// piles/stations 即时返回 mock 正常数据(概览三路可齐备)
class ManualRevenueRepository : public ev::AdminRepository
{
public:
    QList<ev::OverviewResult> results;
    int fetchIndex = 0;
    QList<std::function<void(const ev::OverviewResult &)>> pending;

    void login(const QString &, const QString &, QObject *,
               std::function<void(const ev::LoginResult &)> callback) override
    {
        ev::LoginResult ok;
        ok.ok = true;
        ok.errorCode = 0;
        ok.admin.id = 1;
        ok.admin.username = QStringLiteral("admin");
        ok.admin.role = QStringLiteral("super_admin");
        ok.admin.status = QStringLiteral("active");
        QTimer::singleShot(0, [callback, ok] { callback(ok); });
    }
    void fetchOverview(QObject *,
                       std::function<void(const ev::OverviewResult &)> callback) override
    {
        pending.append(callback);
    }
    void fetchPiles(QObject *,
                    std::function<void(const ev::ListResult<ev::PileInfo> &)> callback) override
    {
        QTimer::singleShot(0, [callback] {
            callback(ev::mockdata::piles(ev::mockdata::DataMode::Normal));
        });
    }
    void fetchStations(QObject *,
                       std::function<void(const ev::ListResult<ev::StationInfo> &)> callback) override
    {
        QTimer::singleShot(0, [callback] {
            callback(ev::mockdata::stations(ev::mockdata::DataMode::Normal));
        });
    }
    void fetchUsers(QObject *,
                    std::function<void(const ev::ListResult<ev::UserInfo> &)>) override {}
    void restartPile(const QString &, QObject *,
                     std::function<void(const ev::ActionResult &)>) override {}
    void setUserStatus(int, const QString &, QObject *,
                       std::function<void(const ev::ActionResult &)>) override {}
    QString dataSourceName() const override { return QStringLiteral("Mock 演示"); }

    void deliverNext()
    {
        const ev::OverviewResult result =
            results.at(qMin(fetchIndex, results.size() - 1));
        ++fetchIndex;
        const auto callback = pending.takeFirst();
        callback(result);
    }
};

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void statusTagsExposeProtocolState();
    void unknownProtocolStateIsRepresentable();
    void chargingAndFaultAnimateWhenMotionEnabled();
    void reducedMotionStopsAnimation();
    void themeLoadsGeneratedQss();
    void shellExposesProductAndSessionContext();
    void loginFieldsHaveAccessibleNames();
    void overviewShowsTraceableMockMetrics();
    void availabilityRateFormulaIsIndependent();
    void topologyUsesInformativeLineAndEmitsStationSelection();
    void topologyPulseFollowsStationAttentionState();
    void topologyKeyboardActivatesStation();
    void attentionItemSwitchesToPilePage();
    void auroraBackdropAnimatesWhenMotionEnabled();
    void reducedMotionFreezesAuroraBackdrop();
    void pilePageFiltersByAttentionStateAndCode();
    void pileRowsExposeCumulativeMetrics();
    void stationAndUserPagesRenderMockRows();
    void mockActionsEnforcePileRestartStateRules();
    void mockSetUserStatusFlipsStateAndIsIdempotent();
    void pilePageRestartButtonAppliesSimulatedRestart();
    void userPageStatusButtonFlipsSelectedUser();
    void mockRevenueSeriesAreConsistent();
    void revenueChartWidgetLifecycle();
    void revenueFullChartKeepsYTitleAnchorAcrossResize();
    void revenueMetricCardSwapsFixedRowsOnClick();
    void revenueMetricCardUnavailableSeriesShowsRetry();
    void revenuePageShowsSummaryChartAndDailyTable();
    void revenuePageEmptyErrorAndZeroRevenueStates();
    void revenuePageCorruptSeriesAllowsRangeSwitch();
    void revenuePageDropsStaleRefreshResults();
    void revenueFlowKeepsRangeAndDropsStaleAfterRelogin();
    // ---- T1 静态图取景/投影纯函数（docs/role-c-admin-map-renderer-plan.md §3.2/§4 T1）----
    void viewportDemoPairLocksToZoom12AndCenter();
    void viewportSingleStationFallsBackToZoom14();
    void viewportZeroSpanAxisExcludesThatAxisFromZoomConstraint();
    void viewportEmptyOrAllInvalidReturnsFalse();
    void viewportSpanTooLargeReturnsFalse();
    void viewportSkipsInvalidStations();
    void projectionCenterMapsToImageCenter();
    void projectionKnownOffsetsMatchLockedFormula();
    void projectionExactFormulaDiffersFromDerivativeApproximationOnWideSpan();
    void projectionNorthIsAboveAndSouthIsBelowCenter();
    void viewportRejectsPolesAndOutOfProjectionRange();
    void viewportRejectsNonPositiveSize();
    void fingerprintStableAndChangesOnViewChange();
    // ---- T2 静态图图片提供者（docs/role-c-admin-map-renderer-plan.md §4 T2）----
    void urlBuilderMatchesProbeParameters();
    void providerNoKeyShortCircuitsWithoutNetwork();
    void providerMapsQuotaErrorToReason();
    void providerHandlesHttpErrorAndTimeout();
    void providerRejectsNonImageBody();
    void providerCancelAllStopsDelivery();
    void providerCancelAllMultipleInflightSafe(); // 评审 P1-3 回归
    void providerTotalDeadlineFiresOnDripTransfer(); // 评审 P2-2 回归
    // ---- T3 拓扑控件双模式（docs/role-c-admin-map-renderer-plan.md §4 T3）----
    void topologyModeWhenNoProviderKeepsLegacyBehavior();
    void mapModeRendersProjectedPointsFromFakeImage();
    void mapModeKeepsClickAndKeyboardActivation();
    void degradedFallsBackToTopologyWithNote();
    void retryBlockedWithinTimeGateThenAllowedAfterClockAdvance();
    void staleRegionCacheNotReusedForDifferentViewport();
    void supersededResponseAfterViewInvalidationIsDiscarded();
    void sameViewportRefreshKeepsInFlightRequest();
    void outOfProjectionRangeStationRemainsInTopology();
    void resizeBucketChangeTriggersRefetch();
    void degradedNoteClearsOnNextSuccessfulFetch();
    void invalidStationCoordinatesSanitizedBeforeLayout();
    void debounceStoppedWhenTargetBecomesUnreachable(); // 评审 P1-1 回归
    void staleImageRetiredWhileSwitchingRegion();       // 评审 P1-2 回归
    void smallWidgetStaysTopologyWithoutRequest();      // 评审 P2-1 回归
    void backToCachedViewCancelsPendingDifferentViewFetch(); // 评审 P1 补充回归
};

void TestUi::statusTagsExposeProtocolState()
{
    struct Case {
        ev::PileStatus state;
        StatusGlyphWidget::GlyphKind glyph;
    };
    const QList<Case> cases = {
        {ev::PileStatus::Idle, StatusGlyphWidget::GlyphKind::CheckCircle},
        {ev::PileStatus::Reserved, StatusGlyphWidget::GlyphKind::ClockDashed},
        {ev::PileStatus::Charging, StatusGlyphWidget::GlyphKind::BoltDot},
        {ev::PileStatus::Fault, StatusGlyphWidget::GlyphKind::WarningTriangle},
        {ev::PileStatus::Offline, StatusGlyphWidget::GlyphKind::LinkOff},
        {ev::PileStatus::Unknown, StatusGlyphWidget::GlyphKind::QuestionDiamond},
    };
    for (const auto &entry : cases) {
        StatusTag tag(entry.state);
        QCOMPARE(tag.property("state").toString(), ev::pileStatusToProtocol(entry.state));
        QCOMPARE(tag.state(), entry.state);
        QCOMPARE(tag.text(), ev::pileStatusToDisplay(entry.state));
        QCOMPARE(tag.glyphKind(), entry.glyph);
        // 组合内确实存在图形与文字两个子组件（不是颜色文字伪装图形）
        QVERIFY(tag.findChild<StatusGlyphWidget *>());
        QVERIFY(tag.findChild<QLabel *>());
    }
}

void TestUi::unknownProtocolStateIsRepresentable()
{
    QCOMPARE(ev::parsePileStatus(QStringLiteral("future-state")), ev::PileStatus::Unknown);
    QCOMPARE(ev::pileStatusToProtocol(ev::PileStatus::Unknown), QStringLiteral("unknown"));
    QCOMPARE(ev::pileStatusToDisplay(ev::PileStatus::Unknown), QStringLiteral("未知"));
}

void TestUi::chargingAndFaultAnimateWhenMotionEnabled()
{
    ev::Theme::setMotionEnabled(true);
    StatusPulseWidget charging(ev::PileStatus::Charging);
    QVERIFY(charging.isAnimationRunning());
    StatusPulseWidget fault(ev::PileStatus::Fault);
    QVERIFY(fault.isAnimationRunning());
    StatusPulseWidget idle(ev::PileStatus::Idle);
    QVERIFY(!idle.isAnimationRunning());
}

void TestUi::reducedMotionStopsAnimation()
{
    StatusPulseWidget widget(ev::PileStatus::Charging);
    widget.setMotionEnabled(false);
    QVERIFY(!widget.isAnimationRunning());
}

void TestUi::themeLoadsGeneratedQss()
{
    const QString qss = ev::Theme::loadDayStyleSheet();
    QVERIFY(!qss.isEmpty());
    QVERIFY(qss.contains(QStringLiteral("#2A7442")));
    QVERIFY(qss.contains(QStringLiteral("#A94B38")));
}

void TestUi::shellExposesProductAndSessionContext()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.findChild<QLabel *>("productMark"));
    QVERIFY(window.findChild<QLabel *>("sessionBadge"));

    // 登录(布局在业务区激活后才有效)
    auto *loginPage = window.findChild<LoginPage *>();
    QVERIFY(loginPage);
    loginPage->findChild<QLineEdit *>("usernameEdit")->setText(QStringLiteral("admin"));
    loginPage->findChild<QLineEdit *>("passwordEdit")->setText(QStringLiteral("123456"));
    QTest::mouseClick(loginPage->findChild<QPushButton *>("loginButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(window.isLoggedIn(), 3000);

    auto *nav = window.findChild<QListWidget *>("navList");
    QVERIFY(nav);
    QCOMPARE(nav->count(), 5); // 概览/销售业绩/充电桩/充电站/用户管理
    // 五项在默认窗口(1024×700)内完整可见且可点击(不再断言 spacing 数值)
    const QRect lastItem = nav->visualItemRect(nav->item(4));
    QVERIFY2(nav->viewport()->rect().contains(lastItem.center()),
             "第五导航项应在默认窗口内完整可见");
    nav->setCurrentRow(4);
    QCOMPARE(nav->currentRow(), 4);
}

void TestUi::loginFieldsHaveAccessibleNames()
{
    MockAdminRepository repository;
    LoginPage page(&repository);
    QCOMPARE(page.findChild<QLineEdit *>("usernameEdit")->accessibleName(),
             QStringLiteral("管理员账号"));
    QCOMPARE(page.findChild<QLineEdit *>("passwordEdit")->accessibleName(),
             QStringLiteral("管理员密码"));
}

void TestUi::overviewShowsTraceableMockMetrics()
{
    // 无参构造自建 Mock（与 MainWindow 同策略）；数据经 Repository 异步返回，
    // QTRY 等待 500ms 模拟往返后渲染（断言值保持与 mockdataset 可追溯一致）。
    OverviewPage page;
    page.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(
        page.findChild<QLabel *>("metricPileTotal")->text(),
        QStringLiteral("6"), 3000);
    QCOMPARE(page.findChild<QLabel *>("metricFaultCount")->text(),
             QStringLiteral("2"));
    // 第四卡已换为营收融合卡: 金额经可访问文案暴露(替换旧 metricRevenue QLabel 断言,
    // 不保留隐藏 QLabel 仅为骗过测试); 默认 7 日折线就位
    auto *revenueCard = page.findChild<RevenueMetricCard *>("revenueCard");
    auto *revenueChart = page.findChild<ev::RevenueChartWidget *>("revenueChart");
    QVERIFY(revenueCard);
    QVERIFY(revenueChart);
    QCOMPARE(revenueCard->selectedDays(), 7);
    QCOMPARE(revenueChart->pointCount(), 7);
    auto *revenue7 = page.findChild<QToolButton *>("revenue7dButton");
    auto *revenue30 = page.findChild<QToolButton *>("revenue30dButton");
    QVERIFY(revenue7 && revenue30);
    QVERIFY(revenue7->accessibleName().contains(QStringLiteral("近 7 日营收")));
    QVERIFY(revenue7->accessibleName().contains(QStringLiteral("¥2,865.40")));
    QVERIFY(revenue7->accessibleName().contains(QStringLiteral("当前选中")));
    QVERIFY(revenue30->accessibleName().contains(QStringLiteral("近 30 日营收")));
    QVERIFY(revenue30->accessibleName().contains(QStringLiteral("¥9,838.40")));
    QVERIFY(revenue30->accessibleName().contains(QStringLiteral("点击切换")));
    // A-02：近 30 日合计与 demo.json revenue30dCents 数组和同值（983840 分 = ¥9,838.40）
    const auto overviewStats = ev::mockdata::overview(ev::mockdata::DataMode::Normal).stats;
    QCOMPARE(overviewStats.revenue30dCents, qint64(983840));
}

void TestUi::mockRevenueSeriesAreConsistent()
{
    const auto stats = ev::mockdata::overview(ev::mockdata::DataMode::Normal).stats;
    const auto &seven = stats.revenue7dSeries;
    const auto &thirty = stats.revenue30dSeries;
    QVERIFY(seven.available && thirty.available);
    QCOMPARE(seven.days.size(), 7);
    QCOMPARE(thirty.days.size(), 30);
    QCOMPARE(seven.totalCents, qint64(286540));
    QCOMPARE(thirty.totalCents, qint64(983840));
    QCOMPARE(thirty.days.first().date, QDate(2026, 8, 3));
    QCOMPARE(seven.days.first().date, QDate(2026, 8, 26));
    QCOMPARE(seven.days.last().date, QDate(2026, 9, 1));
    for (int i = 0; i < 7; ++i) {
        QCOMPARE(seven.days.at(i).date, thirty.days.at(i + 23).date);
        QCOMPARE(seven.days.at(i).revenueCents, thirty.days.at(i + 23).revenueCents);
    }
}

void TestUi::revenueChartWidgetLifecycle()
{
    const auto stats = ev::mockdata::overview(ev::mockdata::DataMode::Normal).stats;
    const auto &seven = stats.revenue7dSeries;
    const auto &thirty = stats.revenue30dSeries;

    // Mini: 真实比例折线; 点数/首尾 X 位置 = 儒略日; Y = 整数分→元(仅图表边界换算);
    // Y 量程聚焦数据带(上下 15% 余量, 折线垂直居中), 轻量网格手绘
    ev::RevenueChartWidget mini(ev::RevenueChartWidget::Mode::Mini);
    mini.setSeries(seven);
    QCOMPARE(mini.pointCount(), 7);
    auto *miniX =
        qobject_cast<QCategoryAxis *>(mini.chart()->axes(Qt::Horizontal).first());
    QVERIFY(miniX);
    QCOMPARE(miniX->count(), 7); // 每点一类(修前空 label 重复被拒只剩 1 类)
    QCOMPARE(mini.displayedRange(), QStringLiteral("7d"));
    auto *line = qobject_cast<QLineSeries *>(mini.chart()->series().first());
    QVERIFY(line);
    QCOMPARE(mini.chart()->series().size(), 1); // 只有一条线
    const QList<QPointF> points = line->points();
    QCOMPARE(points.size(), 7);
    QCOMPARE(qRound64(points.first().x()),
             qint64(seven.days.first().date.toJulianDay()));
    QCOMPARE(qRound64(points.last().x()),
             qint64(seven.days.last().date.toJulianDay()));
    QCOMPARE(points.first().y(), qreal(seven.days.first().revenueCents) / 100.0);
    auto *yAxis =
        qobject_cast<QValueAxis *>(mini.chart()->axes(Qt::Vertical).first());
    QVERIFY(yAxis);
    qreal yuanMin = std::numeric_limits<qreal>::max();
    qreal yuanMax = 0.0;
    for (const auto &day : seven.days) {
        yuanMin = qMin(yuanMin, qreal(day.revenueCents) / 100.0);
        yuanMax = qMax(yuanMax, qreal(day.revenueCents) / 100.0);
    }
    // Mini 聚焦带: 下界贴近数据最小日(>0)、上界 = 峰值 + 15% 带余量(折线居中)
    QVERIFY(yAxis->min() > 0.0 && yAxis->min() <= yuanMin);
    QVERIFY(yAxis->max() > yuanMax && yAxis->max() <= yuanMax * 1.2);

    // Full: 30 点; X 稀疏标签 6 个(约 5-7)且含首尾, 类目总数仍为 30
    ev::RevenueChartWidget full(ev::RevenueChartWidget::Mode::Full);
    full.setSeries(thirty);
    QCOMPARE(full.pointCount(), 30);
    QCOMPARE(full.displayedRange(), QStringLiteral("30d"));
    auto *xAxis =
        qobject_cast<QCategoryAxis *>(full.chart()->axes(Qt::Horizontal).first());
    QVERIFY(xAxis);
    // 类目结构完整: 每点一类(重复空 label 曾被静默拒绝 → 实测 30 类只剩 7 类,
    // 日期标签随之错位)。隐藏位为唯一空格串: 30 类齐 + 真日期稀疏标签 6 个含首尾。
    QCOMPARE(xAxis->count(), 30);
    QVERIFY(!xAxis->truncateLabels()); // 默认 true 会把 M/d 截成 '...'(30 类配额不足)
    QStringList labels = xAxis->categoriesLabels();
    QCOMPARE(labels.size(), 30);
    QStringList visibleLabels;
    for (const QString &l : labels) {
        if (!l.trimmed().isEmpty())
            visibleLabels.append(l);
    }
    QCOMPARE(visibleLabels.size(), 6); // 稀疏位 6 个(0,6,12,18,24,29, 含首尾)
    QCOMPARE(visibleLabels.first(), QStringLiteral("8/3")); // 含首
    QCOMPARE(visibleLabels.last(), QStringLiteral("9/1"));  // 含尾
    // 隐藏占位全唯一(QSet 去重后仍 30)
    QCOMPARE(QSet<QString>(labels.begin(), labels.end()).size(), 30);
    // 类边界几何: 首类起点 = 首日儒略日 - xPad(30d 稀疏留白 1.5 天, 防 QtCharts
    // 边缘 forceHide 整条首标签; 默认 0 会把首标签甩出可视区); 末类终点 = 末日
    // 儒略日 + 0.5; 相邻真标签跨度 = 步长 × 1 天
    const QDate firstDate(2026, 8, 3);
    const QDate lastDate(2026, 9, 1);
    QCOMPARE(xAxis->startValue(visibleLabels.first()),
             qreal(firstDate.toJulianDay()) - 1.5);
    QCOMPARE(xAxis->endValue(visibleLabels.last()),
             qreal(lastDate.toJulianDay()) + 0.5);
    QCOMPARE(xAxis->endValue(visibleLabels.at(1)) - xAxis->endValue(visibleLabels.first()),
             6.0); // 8/9 与 8/3 类终点差 6 天(每类 1 天宽)
    // Full 30 点 Y 上限按 30 日峰值(468 元)放 15%
    auto *fullY = qobject_cast<QValueAxis *>(full.chart()->axes(Qt::Vertical).first());
    QVERIFY(fullY);
    QCOMPARE(fullY->min(), 0.0);
    QVERIFY(fullY->max() > 468.0 && fullY->max() <= 468.0 * 1.16);

    // 重复 setSeries(7→30)只替换点, 不累积 series/连接
    mini.setSeries(thirty);
    QCOMPARE(mini.chart()->series().size(), 1);
    QCOMPARE(mini.pointCount(), 30);
    QCOMPARE(mini.displayedRange(), QStringLiteral("30d"));
    QCOMPARE(miniX->count(), 30); // 旧类清空后重建 30 类, 不累积

    // 全零序列: 合法零线, 轴不退化
    ev::RevenueSeries zeros;
    zeros.range = QStringLiteral("7d");
    zeros.available = true;
    zeros.updatedAt = QStringLiteral("2026-09-01T10:15:00Z");
    for (int i = 0; i < 7; ++i)
        zeros.days.append({QDate(2026, 8, 26).addDays(i), 0});
    mini.setSeries(zeros);
    QCOMPARE(mini.pointCount(), 7);
    auto *zeroY =
        qobject_cast<QValueAxis *>(mini.chart()->axes(Qt::Vertical).first());
    QCOMPARE(zeroY->min(), 0.0);
    QCOMPARE(zeroY->max(), 1.0); // 峰值 0 → 1.0 非退化量程

    // clearSeries: 点/范围/提示状态清空, 无旧图残留
    mini.clearSeries();
    QCOMPARE(mini.pointCount(), 0);
    QVERIFY(mini.displayedRange().isEmpty());
    QCOMPARE(mini.chart()->series().size(), 1);
    auto *clearedLine =
        qobject_cast<QLineSeries *>(mini.chart()->series().first());
    QVERIFY(clearedLine->points().isEmpty());

    // clearSeries 后轴状态一并复位: 类目(旧日期标签)清空 + X/Y 量程回初始
    // (修前只清折线点, 轴残留上次日期/网格 → 评审 B-3)
    full.clearSeries();
    QCOMPARE(full.pointCount(), 0);
    QCOMPARE(xAxis->count(), 0);
    QVERIFY(xAxis->categoriesLabels().isEmpty());
    QCOMPARE(xAxis->min(), 0.0);
    QCOMPARE(xAxis->max(), 1.0);
    auto *clearedFullY =
        qobject_cast<QValueAxis *>(full.chart()->axes(Qt::Vertical).first());
    QVERIFY(clearedFullY);
    QCOMPARE(clearedFullY->min(), 0.0);
    QCOMPARE(clearedFullY->max(), 1.0);
}

void TestUi::revenueFullChartKeepsYTitleAnchorAcrossResize()
{
    // 回归(评审 Blocking): ¥ 正立自绘不得隐藏 QtCharts 纵轴标题 item。Qt 6.2.4
    // VerticalAxis::sizeHint()/updateGeometry() 以 titleItem()->isVisible() 为闸:
    // 隐藏后布局不再为标题预留空间(sizeHint 归零)、标题几何不再被维护 —— 首次
    // 显示正常(隐藏发生在首帧绘制后), 但 show→resize/relayout 后轴宽塌缩、
    // plotArea 横向漂移, 自绘锚点(sceneBoundingRect)冻结在旧位置 → ¥ 偏移/重叠。
    // 正解 = setOpacity(0): item 仍 visible, 标题空间与几何照常维护, 自绘每帧
    // 跟随更新后的包围盒。本用例断言: 布局往返后 plotArea 复原(轴宽不塌)、¥ 锚点
    // 与 plotArea 左缘间距不变(自绘不漂移)、标题 item 可见且透明(不回归隐藏方案)。
    const auto stats = ev::mockdata::overview(ev::mockdata::DataMode::Normal).stats;
    const auto &seven = stats.revenue7dSeries;
    const auto &thirty = stats.revenue30dSeries;

    ev::RevenueChartWidget full(ev::RevenueChartWidget::Mode::Full);
    full.setSeries(thirty);
    full.resize(560, 280);
    full.show();
    QVERIFY(QTest::qWaitForWindowExposed(&full));
    QCoreApplication::processEvents();
    full.grab(); // 强制首帧渲染: 旧实现(隐藏方案)在此帧执行 setVisible(false),
    // 新实现(透明方案)在此帧执行 setOpacity(0) —— 后续 relayout 的起点状态由此定

    // 与组件同一规则定位 QtCharts 内部 ¥ 标题 item(私有实现依赖, 项目固定
    // Qt 6.2.4 —— 升级 QtCharts 需先复核定位规则与布局闸门)
    QGraphicsTextItem *titleItem = nullptr;
    const auto items = full.chart()->scene()->items();
    for (QGraphicsItem *it : items) {
        auto *txt = dynamic_cast<QGraphicsTextItem *>(it);
        if (txt && txt->toPlainText() == QStringLiteral("¥")) {
            titleItem = txt;
            break;
        }
    }
    QVERIFY(titleItem);
    QVERIFY(full.chart()->plotArea().width() > 100.0); // 布局已就绪(非退化)

    // 自绘锚点 = 标题 item 场景包围盒中心(转 chart 局部坐标后与 plotArea 同系)。
    // QtCharts 把纵轴标题垂直居中于绘图区(gridRect.center), 布局正常维护时
    // 任何尺寸下锚点都应贴合 plotArea 垂直中心 —— 锚点与刻度(网格)同动。
    const auto anchorCenter = [&]() -> QPointF {
        return titleItem->sceneBoundingRect().center() - full.chart()->scenePos();
    };
    const auto centerDrift = [&]() -> qreal {
        return qAbs(anchorCenter().y() - full.chart()->plotArea().center().y());
    };
    const qreal plot0Left = full.chart()->plotArea().left();
    const qreal plot0Width = full.chart()->plotArea().width();
    const qreal gap0 = plot0Left - anchorCenter().x();
    QVERIFY(centerDrift() < 1.0); // 初始: 标题垂直居中于绘图区
    QVERIFY(gap0 > 10.0);         // ¥ 在 plotArea 左侧独立空间内(不与刻度/绘图区重叠)

    // show → resize → 再次 setSeries → resize 回原尺寸(评审建议流程):
    // 每轮 relayout 后标题 item 几何必须与 plotArea 同步更新
    full.resize(420, 320);
    QTest::qWait(50);
    // 高度 280→320: 标题几何必须随布局重排(修前隐藏标题 → 几何冻结, 垂直
    // 偏差 ≈ 高度差一半, 与 Y 轴刻度重叠)
    QVERIFY(centerDrift() < 1.0);
    full.setSeries(seven);
    QTest::qWait(50);
    QVERIFY(centerDrift() < 1.0);
    full.resize(560, 280);
    QTest::qWait(50);
    QVERIFY(centerDrift() < 1.0);

    QVERIFY(qAbs(full.chart()->plotArea().left() - plot0Left) < 1.0);
    QVERIFY(qAbs(full.chart()->plotArea().width() - plot0Width) < 1.0);
    QVERIFY(qAbs(plot0Left - anchorCenter().x() - gap0) < 1.0);

    full.grab(); // 强制同步渲染一次: drawForeground 定位并处理标题 item
    QVERIFY(titleItem->isVisible());            // 隐藏会令 sizeHint 归零(见上)
    QVERIFY(titleItem->opacity() < 1.0);        // 原字形透明、自绘正立替代
}

void TestUi::revenueMetricCardSwapsFixedRowsOnClick()
{
    ev::Theme::setMotionEnabled(true);
    const auto stats = ev::mockdata::overview(ev::mockdata::DataMode::Normal).stats;

    RevenueMetricCard card;
    card.resize(240, 150); // 与概览四卡之一同量级; 热区几何按行位给出
    card.show();
    QVERIFY(QTest::qWaitForWindowExposed(&card));
    card.setStatistics(stats);
    QTRY_VERIFY_WITH_TIMEOUT(
        card.findChild<QToolButton *>("revenue7dButton")->geometry().width() > 0,
        1000);

    auto *chart = card.findChild<ev::RevenueChartWidget *>("revenueChart");
    auto *button7 = card.findChild<QToolButton *>("revenue7dButton");
    auto *button30 = card.findChild<QToolButton *>("revenue30dButton");
    auto *details = card.findChild<QToolButton *>("revenueDetailsButton");
    auto *retry = card.findChild<QToolButton *>("revenueRetryButton");
    QVERIFY(chart && button7 && button30 && details && retry);

    // 默认 7 日: 7 点曲线; 可访问文案含范围/金额/选中状态
    QCOMPARE(card.selectedDays(), 7);
    QCOMPARE(chart->pointCount(), 7);
    QVERIFY(button7->accessibleName().contains(QStringLiteral("近 7 日营收")));
    QVERIFY(button7->accessibleName().contains(QStringLiteral("¥2,865.40")));
    QVERIFY(button7->accessibleName().contains(QStringLiteral("当前选中")));
    QVERIFY(button30->accessibleName().contains(QStringLiteral("近 30 日营收")));
    QVERIFY(button30->accessibleName().contains(QStringLiteral("¥9,838.40")));
    QVERIFY(button30->accessibleName().contains(QStringLiteral("点击切换")));

    // 真实鼠标点击 30 日: 图切 30 点、主次行内容交换、信号带 days=30
    QSignalSpy rangeSpy(&card, &RevenueMetricCard::rangeChanged);
    QSignalSpy detailsSpy(&card, &RevenueMetricCard::detailsRequested);
    QSignalSpy retrySpy(&card, &RevenueMetricCard::retryRequested);
    const QRect mainBand = button7->geometry();
    const QRect secondaryBand = button30->geometry();
    QTest::mouseMove(button30);
    QTest::qWait(250);
    QCOMPARE(card.selectedDays(), 7);
    QCOMPARE(rangeSpy.count(), 0);
    QTest::mousePress(button30, Qt::LeftButton);
    QCOMPARE(card.selectedDays(), 7);
    QTest::mouseRelease(button30, Qt::LeftButton);
    QCOMPARE(card.selectedDays(), 30);
    QCOMPARE(button30->geometry(), mainBand);
    QCOMPARE(button7->geometry(), secondaryBand);
    QCOMPARE(chart->pointCount(), 30);
    QCOMPARE(rangeSpy.count(), 1);
    QCOMPARE(rangeSpy.takeFirst().at(0).toInt(), 30);

    // 重复点击当前项: 不重复发信号
    QTest::mouseClick(button30, Qt::LeftButton);
    QCOMPARE(rangeSpy.count(), 0);
    QCOMPARE(card.selectedDays(), 30);

    // 键盘(空格)等效切换回 7 日
    button7->setFocus();
    QTest::keyClick(button7, Qt::Key_Space);
    QCOMPARE(card.selectedDays(), 7);
    QCOMPARE(chart->pointCount(), 7);
    QCOMPARE(button7->geometry(), mainBand);
    QCOMPARE(button30->geometry(), secondaryBand);
    QCOMPARE(rangeSpy.count(), 1);
    QCOMPARE(rangeSpy.takeFirst().at(0).toInt(), 7);

    // 快速连点 30→7→30: 最终态 = 最后一次选择
    QTest::mouseClick(button30, Qt::LeftButton);
    QTest::mouseClick(button7, Qt::LeftButton);
    QTest::mouseClick(button30, Qt::LeftButton);
    QCOMPARE(card.selectedDays(), 30);
    QCOMPARE(chart->pointCount(), 30);
    QCOMPARE(card.selectedDays(), 30);
    QCOMPARE(chart->pointCount(), 30);

    // 刷新(setStatistics)保留当前 30 日选择, 不重置回 7
    card.setStatistics(stats);
    QCOMPARE(card.selectedDays(), 30);

    // 详情入口携带当前范围
    QTest::mouseClick(details, Qt::LeftButton);
    QCOMPARE(detailsSpy.count(), 1);
    QCOMPARE(detailsSpy.takeFirst().at(0).toInt(), 30);

    // reset: 清统计/曲线、回 7 日
    card.reset();
    QCOMPARE(card.selectedDays(), 7);
    QCOMPARE(chart->pointCount(), 0);
    QVERIFY(!retry->isVisible());

    // 关闭动效同样仅在点击后直接交换内容
    ev::Theme::setMotionEnabled(false);
    card.setStatistics(stats);
    QTest::mouseClick(button30, Qt::LeftButton);
    QCOMPARE(card.selectedDays(), 30);
    QCOMPARE(chart->pointCount(), 30);
    ev::Theme::setMotionEnabled(true); // 恢复, 不影响其他用例
}

void TestUi::revenueMetricCardUnavailableSeriesShowsRetry()
{
    ev::OverviewStats stats =
        ev::mockdata::overview(ev::mockdata::DataMode::Normal).stats;
    // 7d 序列损坏(摘要级金额仍在) → 默认 7 日不可画: 清曲线 + 重试入口; 30 日仍可用
    stats.revenue7dSeries.available = false;
    stats.revenue7dSeries.days.clear();
    stats.revenue7dSeries.error = QStringLiteral("营收日期数量不完整");

    RevenueMetricCard card;
    card.resize(240, 150);
    card.show();
    QVERIFY(QTest::qWaitForWindowExposed(&card));
    card.setStatistics(stats);

    auto *chart = card.findChild<ev::RevenueChartWidget *>("revenueChart");
    auto *button30 = card.findChild<QToolButton *>("revenue30dButton");
    auto *retry = card.findChild<QToolButton *>("revenueRetryButton");
    QVERIFY(chart && button30 && retry);

    // 坏序列不清金额(摘要级合计照常), 曲线不画 0
    QCOMPARE(card.selectedDays(), 7);
    QCOMPARE(chart->pointCount(), 0);
    QVERIFY(retry->isVisible());
    QSignalSpy retrySpy(&card, &RevenueMetricCard::retryRequested);
    QTest::mouseClick(retry, Qt::LeftButton);
    QCOMPARE(retrySpy.count(), 1);

    // 切到 30 日(好序列): 曲线恢复, 重试隐藏
    QTest::mouseClick(button30, Qt::LeftButton);
    QCOMPARE(chart->pointCount(), 30);
    QVERIFY(!retry->isVisible());
}

void TestUi::revenuePageShowsSummaryChartAndDailyTable()
{
    RevenueFixtureRepository repo;
    repo.results.append(ev::mockdata::overview(ev::mockdata::DataMode::Normal));

    RevenuePage page(&repo);
    page.refresh();

    auto *chart = page.findChild<ev::RevenueChartWidget *>("revenueChart");
    auto *combo = page.findChild<QComboBox *>("revenueRangeCombo");
    auto *table = page.findChild<QTableWidget *>("revenueDailyTable");
    auto *updated = page.findChild<QLabel *>("revenueUpdatedLabel");
    auto *card7 = page.findChild<QLabel *>("metricRevenue7d");
    auto *card30 = page.findChild<QLabel *>("metricRevenue30d");
    QVERIFY(chart && combo && table && updated && card7 && card30);

    // 一次 fetchOverview 供全页; 默认 7 日
    QTRY_COMPARE_WITH_TIMEOUT(chart->pointCount(), 7, 3000);
    QCOMPARE(repo.fetchCount, 1);
    QCOMPARE(page.selectedDays(), 7);
    QCOMPARE(card7->text(), QStringLiteral("¥2,865.40"));
    QCOMPARE(card30->text(), QStringLiteral("¥9,838.40"));
    QCOMPARE(table->rowCount(), 7);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("2026-08-26")); // UTC 升序
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("¥356.00"));
    QCOMPARE(table->item(6, 1)->text(), QStringLiteral("¥398.00"));
    QVERIFY(updated->text().contains(QStringLiteral("2026-09-01T10:15:00Z")));

    // 本地切 30 日: 无新请求, 图/表/卡一致
    combo->setCurrentIndex(1);
    QTRY_COMPARE_WITH_TIMEOUT(chart->pointCount(), 30, 3000);
    auto *pageX =
        qobject_cast<QCategoryAxis *>(chart->chart()->axes(Qt::Horizontal).first());
    QVERIFY(pageX);
    QCOMPARE(pageX->count(), 30); // 页面路径同样每点一类(修前空 label 重复只剩 7)
    QCOMPARE(repo.fetchCount, 1);
    QCOMPARE(page.selectedDays(), 30);
    QCOMPARE(table->rowCount(), 30);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("2026-08-03"));
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("¥254.00"));
    QCOMPARE(table->item(29, 1)->text(), QStringLiteral("¥398.00"));

    // setRange 反向同步下拉(不重复触发)
    page.setRange(7);
    QCOMPARE(combo->currentIndex(), 0);
    QCOMPARE(chart->pointCount(), 7);
    QCOMPARE(page.selectedDays(), 7);
}

void TestUi::revenuePageEmptyErrorAndZeroRevenueStates()
{
    // 空库: Empty 态, 内容(卡/图/表)不冒充数据
    RevenueFixtureRepository emptyRepo;
    emptyRepo.results.append(ev::mockdata::overview(ev::mockdata::DataMode::Empty));
    RevenuePage emptyPage(&emptyRepo);
    emptyPage.refresh();
    auto *emptyStack = emptyPage.findChild<StateStack *>();
    QVERIFY(emptyStack);
    QTRY_VERIFY_WITH_TIMEOUT(
        emptyStack->currentState() == StateStack::State::Empty, 3000);
    auto *emptyChart = emptyPage.findChild<ev::RevenueChartWidget *>("revenueChart");
    QCOMPARE(emptyChart->pointCount(), 0);

    // 接口失败: Error 态(不显示旧图冒充当前)
    RevenueFixtureRepository errorRepo;
    errorRepo.results.append(ev::mockdata::overview(ev::mockdata::DataMode::Error));
    RevenuePage errorPage(&errorRepo);
    errorPage.refresh();
    auto *errorStack = errorPage.findChild<StateStack *>();
    QTRY_VERIFY_WITH_TIMEOUT(
        errorStack->currentState() == StateStack::State::Error, 3000);

    // 零营收(序列可用、合计 0): 正常内容 + 零线 + 逐日 ¥0.00 + 辅助文案
    RevenueFixtureRepository zeroRepo;
    auto zeroResult = ev::mockdata::overview(ev::mockdata::DataMode::Normal);
    const auto rebuildZero = [](ev::RevenueSeries series) {
        ev::RevenueSeries zeros = series;
        zeros.days.clear();
        zeros.totalCents = 0;
        for (const auto &day : series.days)
            zeros.days.append({day.date, 0});
        zeros.available = true;
        return zeros;
    };
    zeroResult.stats.revenueCents = 0;
    zeroResult.stats.revenue30dCents = 0;
    zeroResult.stats.revenue7dSeries = rebuildZero(zeroResult.stats.revenue7dSeries);
    zeroResult.stats.revenue30dSeries = rebuildZero(zeroResult.stats.revenue30dSeries);
    zeroRepo.results.append(zeroResult);
    RevenuePage zeroPage(&zeroRepo);
    zeroPage.resize(1000, 700);
    zeroPage.show();
    QVERIFY(QTest::qWaitForWindowExposed(&zeroPage));
    zeroPage.refresh();
    auto *zeroChart = zeroPage.findChild<ev::RevenueChartWidget *>("revenueChart");
    auto *zeroTable = zeroPage.findChild<QTableWidget *>("revenueDailyTable");
    auto *zeroHint = zeroPage.findChild<QLabel *>("revenueZeroHint");
    QTRY_COMPARE_WITH_TIMEOUT(zeroChart->pointCount(), 7, 3000);
    QCOMPARE(zeroTable->rowCount(), 7);
    QCOMPARE(zeroTable->item(0, 1)->text(), QStringLiteral("¥0.00"));
    QVERIFY(zeroHint->isVisible());
}

void TestUi::revenuePageCorruptSeriesAllowsRangeSwitch()
{
    // 7d 序列损坏: 摘要两卡照常 + 图表错误提示 + 表格清空; 30d 可用可切换
    RevenueFixtureRepository repo;
    auto result = ev::mockdata::overview(ev::mockdata::DataMode::Normal);
    result.stats.revenue7dSeries.available = false;
    result.stats.revenue7dSeries.days.clear();
    result.stats.revenue7dSeries.error = QStringLiteral("营收日期数量不完整");
    repo.results.append(result);

    RevenuePage page(&repo);
    page.resize(1000, 700);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    page.refresh();

    auto *chart = page.findChild<ev::RevenueChartWidget *>("revenueChart");
    auto *combo = page.findChild<QComboBox *>("revenueRangeCombo");
    auto *table = page.findChild<QTableWidget *>("revenueDailyTable");
    auto *errorLabel = page.findChild<QLabel *>("revenueSeriesError");
    auto *card7 = page.findChild<QLabel *>("metricRevenue7d");
    QVERIFY(chart && combo && table && errorLabel && card7);

    // 默认 7 日坏序列: 不画 0、错误提示可见、摘要金额照常
    QTRY_VERIFY_WITH_TIMEOUT(errorLabel->isVisible(), 3000);
    QCOMPARE(chart->pointCount(), 0);
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(errorLabel->text().contains(QStringLiteral("近 7 日")));
    QVERIFY(errorLabel->text().contains(QStringLiteral("营收日期数量不完整")));
    QCOMPARE(card7->text(), QStringLiteral("¥2,865.40"));

    // 切 30 日(正常): 图/表恢复、错误提示隐藏
    combo->setCurrentIndex(1);
    QTRY_COMPARE_WITH_TIMEOUT(chart->pointCount(), 30, 3000);
    QVERIFY(!errorLabel->isVisible());
    QCOMPARE(table->rowCount(), 30);
    QCOMPARE(page.selectedDays(), 30);

    // 顺序反转: 30 日已画过 → 切回损坏的 7 日 → 错误提示出现且轴零残留
    // (修前 clearSeries 只清点不清轴, 30 日日期标签/量程仍挂图上)
    combo->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(errorLabel->isVisible(), 3000);
    QCOMPARE(chart->pointCount(), 0);
    QCOMPARE(table->rowCount(), 0);
    auto *corruptX =
        qobject_cast<QCategoryAxis *>(chart->chart()->axes(Qt::Horizontal).first());
    QVERIFY(corruptX);
    QCOMPARE(corruptX->count(), 0);           // 旧 30 日类目已清
    QVERIFY(corruptX->categoriesLabels().isEmpty());
    QCOMPARE(corruptX->min(), 0.0);           // X 量程复位
    QCOMPARE(corruptX->max(), 1.0);
}

void TestUi::revenuePageDropsStaleRefreshResults()
{
    // 连续两次刷新: 旧 generation 回包丢弃, 终态 = 最后一次请求的结果
    RevenueFixtureRepository repo;
    auto first = ev::mockdata::overview(ev::mockdata::DataMode::Normal);
    auto second = first;
    second.stats.revenueCents = 111; // 唯一标记: ¥1.11
    second.stats.updatedAt = QStringLiteral("2026-09-02T00:00:00Z");
    second.stats.revenue7dSeries.updatedAt = QStringLiteral("2026-09-02T00:00:00Z");
    second.stats.revenue30dSeries.updatedAt = QStringLiteral("2026-09-02T00:00:00Z");
    repo.results.append(first);
    repo.results.append(second);

    RevenuePage page(&repo);
    page.refresh();
    page.refresh(); // 第一次回包尚未到达即再次刷新

    auto *card7 = page.findChild<QLabel *>("metricRevenue7d");
    auto *updated = page.findChild<QLabel *>("revenueUpdatedLabel");
    QVERIFY(card7 && updated);
    QTRY_VERIFY_WITH_TIMEOUT(
        updated->text().contains(QStringLiteral("2026-09-02T00:00:00Z")), 3000);
    QCOMPARE(repo.fetchCount, 2);
    QCOMPARE(card7->text(), QStringLiteral("¥1.11")); // 旧回包(¥2,865.40)未覆盖
}

void TestUi::revenueFlowKeepsRangeAndDropsStaleAfterRelogin()
{
    // 端到端生命周期(计划 Task 9): 登录→概览选 30 日→详情进销售页(携带范围)→
    // 退出(在途回包作废)→ 再登录显示新数据; 旧会话迟到回包不可见。
    ManualRevenueRepository repo;
    auto base = ev::mockdata::overview(ev::mockdata::DataMode::Normal);
    auto afterRelogin = base;
    const QString newTime = QStringLiteral("2026-09-02T08:00:00Z");
    afterRelogin.stats.updatedAt = newTime;
    afterRelogin.stats.revenue7dSeries.updatedAt = newTime;
    afterRelogin.stats.revenue30dSeries.updatedAt = newTime;
    // 派发顺序: 0=首次登录概览 1=销售页首次(将滞留作废) 2=再登录概览 3=再登录销售页
    repo.results = {base, base, afterRelogin, afterRelogin};

    MainWindow window(&repo);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *loginPage = window.findChild<LoginPage *>();
    const auto login = [&] {
        loginPage->findChild<QLineEdit *>("usernameEdit")->setText(QStringLiteral("admin"));
        loginPage->findChild<QLineEdit *>("passwordEdit")->setText(QStringLiteral("123456"));
        QTest::mouseClick(loginPage->findChild<QPushButton *>("loginButton"),
                          Qt::LeftButton);
    };
    login();
    QTRY_VERIFY_WITH_TIMEOUT(window.isLoggedIn(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(repo.pending.size(), 1, 1000); // 概览首次已挂起

    auto *overview = window.findChild<OverviewPage *>();
    auto *revenue = window.findChild<RevenuePage *>();
    auto *nav = window.findChild<QListWidget *>("navList");
    auto *pageStack = window.findChild<QStackedWidget *>("pageStack");
    QVERIFY(overview && revenue && nav && pageStack);

    repo.deliverNext(); // 概览内容就绪
    QTRY_COMPARE_WITH_TIMEOUT(
        overview->findChild<ev::RevenueChartWidget *>("revenueChart")->pointCount(),
        7, 3000);

    // 概览卡: 选 30 日 → 点详情 → 销售页携带 30 且触发刷新(挂起 #2)
    QTest::mouseClick(overview->findChild<QToolButton *>("revenue30dButton"),
                      Qt::LeftButton);
    QTest::mouseClick(overview->findChild<QToolButton *>("revenueDetailsButton"),
                      Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(pageStack->currentIndex(), 1, 1000); // 1 = 销售业绩
    QCOMPARE(revenue->selectedDays(), 30); // 范围携带, 不闪回 7
    QTRY_COMPARE_WITH_TIMEOUT(repo.pending.size(), 1, 1000); // 销售页刷新 #2 挂起

    // 退出登录: 在途 #2 作废; 页面回 Loading/复位(7 日)
    QTest::mouseClick(window.findChild<QPushButton *>("logoutButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isLoggedIn(), 3000);
    auto *revenueStack = revenue->findChild<StateStack *>();
    QVERIFY(revenueStack);
    QCOMPARE(int(revenueStack->currentState()), int(StateStack::State::Loading));
    QCOMPARE(revenue->selectedDays(), 7);

    // 旧会话迟到回包(#2)此刻到达: 必须被丢弃(页面仍 Loading、无任何数据可见)
    repo.deliverNext();
    QTest::qWait(120);
    QCOMPARE(int(revenueStack->currentState()), int(StateStack::State::Loading));
    auto *revenueChart = revenue->findChild<ev::RevenueChartWidget *>("revenueChart");
    QCOMPARE(revenueChart->pointCount(), 0);

    // 再登录: 概览取新数据(#3)
    login();
    QTRY_VERIFY_WITH_TIMEOUT(window.isLoggedIn(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(repo.pending.size(), 1, 1000);
    repo.deliverNext();
    QTRY_VERIFY_WITH_TIMEOUT(
        overview->findChild<QLabel *>("updatedLabel")
            ->text().contains(newTime), 3000);

    // 进销售页: 取最新(#4), 金额为再登录会话数据且默认 7 日
    nav->setCurrentRow(1);
    QTRY_COMPARE_WITH_TIMEOUT(pageStack->currentIndex(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(repo.pending.size(), 1, 1000);
    repo.deliverNext();
    QTRY_VERIFY_WITH_TIMEOUT(
        revenue->findChild<QLabel *>("revenueUpdatedLabel")
            ->text().contains(newTime), 3000);
    QCOMPARE(revenue->selectedDays(), 7);
    QCOMPARE(revenueChart->pointCount(), 7);
}

void TestUi::availabilityRateFormulaIsIndependent()
{
    QCOMPARE(ev::availabilityRate(QList<ev::PileInfo>()), 0.0);
    const auto piles = ev::mockdata::piles(ev::mockdata::DataMode::Normal).items;
    QCOMPARE(piles.size(), 6);
    // Normal：故障 1 + 离线 1 需关注，可用率 (6-2)/6
    QVERIFY2(qAbs(ev::availabilityRate(piles) - 4.0 / 6.0) < 1e-9,
             "availabilityRate 应等于 (total - attention) / total");
}

void TestUi::topologyUsesInformativeLineAndEmitsStationSelection()
{
    StationTopologyWidget topology;
    topology.resize(600, 360);
    topology.setStations(ev::mockdata::stations(ev::mockdata::DataMode::Normal).items);
    QCOMPARE(topology.stationCount(), 2);
    QCOMPARE(topology.informationLineColor(), ev::theme::kDayTopologyLine);
    QSignalSpy spy(&topology, &StationTopologyWidget::stationActivated);
    topology.activateStationForTest(1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
}

void TestUi::topologyPulseFollowsStationAttentionState()
{
    // 真实集成：拓扑内部按站点最高关注状态挂载/隐藏脉冲子组件，
    // 不得单独 new 一个 StatusPulseWidget 冒充集成。
    ev::Theme::setMotionEnabled(true);
    StationTopologyWidget topology;
    topology.resize(600, 360);
    topology.setStations(ev::mockdata::stations(ev::mockdata::DataMode::Normal).items);
    topology.setPiles(ev::mockdata::piles(ev::mockdata::DataMode::Normal).items);

    const auto pulses = topology.findChildren<StatusPulseWidget *>();
    QCOMPARE(pulses.size(), 1); // S1 故障(有呼吸)；S2 离线(静态)
    StatusPulseWidget *pulse = pulses.first();
    QVERIFY(!pulse->isHidden());
    QVERIFY2(pulse->isAnimationRunning(), "故障站脉冲应在动效开启时运行");

    // 数据刷新为全空闲后脉冲消失
    topology.setPiles(QList<ev::PileInfo>());
    QCOMPARE(topology.findChildren<StatusPulseWidget *>().size(), 0);
}

void TestUi::topologyKeyboardActivatesStation()
{
    StationTopologyWidget topology;
    topology.resize(600, 360);
    topology.setStations(ev::mockdata::stations(ev::mockdata::DataMode::Normal).items);
    topology.setPiles(ev::mockdata::piles(ev::mockdata::DataMode::Normal).items);
    // offscreen 平台无窗口管理器焦点；QTest::keyClick 直接向控件派发键盘事件，
    // 与真实按键走同一 keyPressEvent 路径。
    QSignalSpy spy(&topology, &StationTopologyWidget::stationActivated);
    QTest::keyClick(&topology, Qt::Key_Right); // 焦点从无到 0 号站（S1）
    QTest::keyClick(&topology, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
    QTest::keyClick(&topology, Qt::Key_Right); // 1 号站（S2）
    QTest::keyClick(&topology, Qt::Key_Space);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
}

void TestUi::attentionItemSwitchesToPilePage()
{
    // 端到端联动（Task 5 Step 6）：概览"需关注"项点击 → MainWindow 切到充电桩页。
    // 登录走真实 UI 流（同 launchsmoke）；列表行通过真实鼠标事件激活。
    MainWindow window;
    window.show();
    auto *loginPage = window.findChild<LoginPage *>();
    QVERIFY(loginPage);
    loginPage->findChild<QLineEdit *>("usernameEdit")->setText(QStringLiteral("admin"));
    loginPage->findChild<QLineEdit *>("passwordEdit")->setText(QStringLiteral("123456"));
    QTest::mouseClick(loginPage->findChild<QPushButton *>("loginButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(window.isLoggedIn(), 3000);

    auto *overview = window.findChild<OverviewPage *>();
    QVERIFY(overview);
    auto *attentionList = overview->findChild<QListWidget *>("attentionList");
    QVERIFY(attentionList);
    QTRY_COMPARE_WITH_TIMEOUT(attentionList->count(), 2, 3000); // P-101-C 故障 / P-202-C 离线

    auto *pageStack = window.findChild<QStackedWidget *>("pageStack");
    QVERIFY(pageStack);
    QCOMPARE(pageStack->currentIndex(), 0); // 0 = 概览

    const QRect firstRow = attentionList->visualItemRect(attentionList->item(0));
    QTest::mouseClick(attentionList->viewport(), Qt::LeftButton, Qt::NoModifier,
                      firstRow.center());
    QTRY_COMPARE_WITH_TIMEOUT(pageStack->currentIndex(), 2, 1000); // 2 = 充电桩页(销售业绩插入后)
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("P-101-C")));
}

void TestUi::auroraBackdropAnimatesWhenMotionEnabled()
{
    // 背景呼吸与状态点同受 Theme 动效开关控制（spec §5.6 减少动态约束）
    ev::Theme::setMotionEnabled(true);
    AuroraBackdrop backdrop;
    QVERIFY(backdrop.isAnimationRunning());
}

void TestUi::reducedMotionFreezesAuroraBackdrop()
{
    AuroraBackdrop backdrop;
    backdrop.setMotionEnabled(false);
    QVERIFY(!backdrop.isAnimationRunning());
}

void TestUi::pilePageFiltersByAttentionStateAndCode()
{
    // 页面经 Repository 链路异步加载（Task 5 方案 A 模式），QTRY 等待渲染
    PilePage page;
    page.refresh(ev::mockdata::DataMode::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(page.visibleRowCount(), 6, 3000);
    page.setStatusFilter(QStringLiteral("attention"));
    QCOMPARE(page.visibleRowCount(), 2); // 故障 P-101-C + 离线 P-202-C
    page.focusPile(QStringLiteral("P-101-C"));
    QCOMPARE(page.currentPileCode(), QStringLiteral("P-101-C"));
}

void TestUi::pileRowsExposeCumulativeMetrics()
{
    // A-04：桩表格八列，累计次数/时长与 mockdataset 同口径可追溯
    PilePage page;
    page.refresh(ev::mockdata::DataMode::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(page.visibleRowCount(), 6, 3000);

    auto *table = page.findChild<QTableWidget *>(QStringLiteral("pileTable"));
    QVERIFY(table);
    QCOMPARE(table->columnCount(), 8);
    QCOMPARE(table->horizontalHeaderItem(6)->text(), QStringLiteral("累计次数"));
    QCOMPARE(table->horizontalHeaderItem(7)->text(), QStringLiteral("累计时长"));

    // P-101-A：132 次 / 316800 s（88h 0m）；P-202-A：187 次 / 392700 s（109h 5m）
    QCOMPARE(table->item(0, 6)->text(), QStringLiteral("132"));
    QCOMPARE(table->item(0, 7)->text(), QStringLiteral("88h 0m"));
    QCOMPARE(table->item(3, 6)->text(), QStringLiteral("187"));
    QCOMPARE(table->item(3, 7)->text(), QStringLiteral("109h 5m"));

    // 时长格式纯函数锁定（秒 → "Xh Ym"）
    QCOMPARE(ev::formatChargeDuration(121500), QStringLiteral("33h 45m"));
    QCOMPARE(ev::formatChargeDuration(153600), QStringLiteral("42h 40m"));
    QCOMPARE(ev::formatChargeDuration(-1), QStringLiteral("0h 0m"));

    // 金额格式纯函数锁定（整数分 → "¥2,865.40"，千分位/零/负/大额；口径同 Web formatCents）
    QCOMPARE(ev::formatYuanCents(286540), QStringLiteral("¥2,865.40"));
    QCOMPARE(ev::formatYuanCents(983840), QStringLiteral("¥9,838.40"));
    QCOMPARE(ev::formatYuanCents(80), QStringLiteral("¥0.80"));
    QCOMPARE(ev::formatYuanCents(0), QStringLiteral("¥0.00"));
    QCOMPARE(ev::formatYuanCents(-1), QStringLiteral("-¥0.01"));
    QCOMPARE(ev::formatYuanCents(-150), QStringLiteral("-¥1.50"));
    QCOMPARE(ev::formatYuanCents(123456789), QStringLiteral("¥1,234,567.89"));
}

void TestUi::stationAndUserPagesRenderMockRows()
{
    StationPage stations;
    stations.refresh(ev::mockdata::DataMode::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(stations.visibleRowCount(), 2, 3000);

    // A-06：五列；两站均 3 桩中 1 台故障/离线 → 在线率 66.7%（mockdataset 同快照派生）
    auto *stationTable = stations.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    QVERIFY(stationTable);
    QCOMPARE(stationTable->columnCount(), 5);
    QCOMPARE(stationTable->horizontalHeaderItem(4)->text(), QStringLiteral("在线率"));
    QCOMPARE(stationTable->item(0, 2)->text(), QStringLiteral("3"));
    QCOMPARE(stationTable->item(0, 4)->text(), QStringLiteral("66.7%"));
    QCOMPARE(stationTable->item(1, 2)->text(), QStringLiteral("3"));
    QCOMPARE(stationTable->item(1, 4)->text(), QStringLiteral("66.7%"));

    UserPage users;
    users.refresh(ev::mockdata::DataMode::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(users.visibleRowCount(), 3, 3000);

    // A-07：五列；注册时间 UTC ISO-8601 原串（mockdataset 同口径可追溯）
    auto *userTable = users.findChild<QTableWidget *>(QStringLiteral("userTable"));
    QVERIFY(userTable);
    QCOMPARE(userTable->columnCount(), 5);
    QCOMPARE(userTable->horizontalHeaderItem(4)->text(), QStringLiteral("注册时间 (UTC)"));
    QCOMPARE(userTable->item(0, 4)->text(), QStringLiteral("2026-08-15T03:24:00Z"));
    QCOMPARE(userTable->item(1, 4)->text(), QStringLiteral("2026-08-02T11:40:00Z"));
}

void TestUi::mockActionsEnforcePileRestartStateRules()
{
    // C-S1-005 数据层规则：仅 fault/offline 可重启（成功 → 桩转 idle 且快照
    // 持久）；运行/空闲/预约 → 1201 CONFLICT 且状态不变；未知桩 → 1200。
    MockAdminRepository repository;

    // 同步等待异步回调（lambda 内不使用 QTRY 宏：其失败分支含裸 return，
    // 与有返回值的 lambda 不兼容；改事件循环轮询 + 外层断言）
    auto waitDone = [](const bool &done) {
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 3000)
            QTest::qWait(20);
    };

    auto restartPileSync = [&repository, &waitDone](const QString &pileCode) {
        QObject context;
        bool done = false;
        ev::ActionResult out;
        repository.restartPile(
            pileCode, &context, [&](const ev::ActionResult &result) {
                out = result;
                done = true;
            });
        waitDone(done);
        return out;
    };
    auto pileStatusSync = [&repository, &waitDone](const QString &pileCode) {
        QObject context;
        bool done = false;
        ev::PileStatus out = ev::PileStatus::Unknown;
        repository.fetchPiles(
            &context, [&](const ev::ListResult<ev::PileInfo> &result) {
                for (const ev::PileInfo &pile : result.items) {
                    if (pile.pileCode == pileCode) {
                        out = pile.status;
                        break;
                    }
                }
                done = true;
            });
        waitDone(done);
        return out;
    };

    // 初始快照：P-101-C 故障、P-202-C 离线（mockdataset 同口径）
    QCOMPARE(pileStatusSync(QStringLiteral("P-101-C")), ev::PileStatus::Fault);

    // 故障桩重启成功 → idle（模拟自检通过），快照持久
    const ev::ActionResult restartFault = restartPileSync(QStringLiteral("P-101-C"));
    QVERIFY2(restartFault.ok, qPrintable(restartFault.message));
    QCOMPARE(restartFault.errorCode, 0);
    QCOMPARE(pileStatusSync(QStringLiteral("P-101-C")), ev::PileStatus::Idle);

    // 离线桩同样允许
    QVERIFY(restartPileSync(QStringLiteral("P-202-C")).ok);
    QCOMPARE(pileStatusSync(QStringLiteral("P-202-C")), ev::PileStatus::Idle);

    // 充电中桩重启 → 1201 CONFLICT，状态不变
    const ev::ActionResult restartCharging = restartPileSync(QStringLiteral("P-101-A"));
    QVERIFY(!restartCharging.ok);
    QCOMPARE(restartCharging.errorCode, 1201);
    QCOMPARE(pileStatusSync(QStringLiteral("P-101-A")), ev::PileStatus::Charging);

    // 空闲桩同样冲突（重启动作必须可观察、可拒绝，不静默）
    QVERIFY(!restartPileSync(QStringLiteral("P-101-B")).ok);
    QCOMPARE(restartPileSync(QStringLiteral("P-101-B")).errorCode, 1201);

    // 不存在的桩 → 1200 NOT_FOUND
    const ev::ActionResult restartMissing = restartPileSync(QStringLiteral("P-XXX"));
    QVERIFY(!restartMissing.ok);
    QCOMPARE(restartMissing.errorCode, 1200);
}

void TestUi::mockSetUserStatusFlipsStateAndIsIdempotent()
{
    // C-S1-007 数据层规则：active↔frozen 翻转成功且快照持久；
    // 重复提交相同状态 → 幂等成功（与 main 服务端一致，无 1201）；
    // 非法状态值 → 1002；用户不存在 → 1200。
    MockAdminRepository repository;

    // 同步等待异步回调（lambda 内不使用 QTRY 宏：失败分支裸 return 与
    // 有返回值 lambda 不兼容；事件循环轮询 + 外层断言）
    auto waitDone = [](const bool &done) {
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 3000)
            QTest::qWait(20);
    };

    auto setStatusSync = [&repository, &waitDone](int userId, const QString &status) {
        QObject context;
        bool done = false;
        ev::ActionResult out;
        repository.setUserStatus(
            userId, status, &context, [&](const ev::ActionResult &result) {
                out = result;
                done = true;
            });
        waitDone(done);
        return out;
    };
    auto userStatusSync = [&repository, &waitDone](int userId) {
        QObject context;
        bool done = false;
        QString out;
        repository.fetchUsers(
            &context, [&](const ev::ListResult<ev::UserInfo> &result) {
                for (const ev::UserInfo &user : result.items) {
                    if (user.id == userId) {
                        out = user.status;
                        break;
                    }
                }
                done = true;
            });
        waitDone(done);
        return out;
    };

    // 初始：用户 1 active（mockdataset 同口径）
    QCOMPARE(userStatusSync(1), QStringLiteral("active"));

    // 冻结成功 → 快照持久
    const ev::ActionResult freeze = setStatusSync(1, QStringLiteral("frozen"));
    QVERIFY2(freeze.ok, qPrintable(freeze.message));
    QCOMPARE(freeze.errorCode, 0);
    QCOMPARE(userStatusSync(1), QStringLiteral("frozen"));

    // 重复冻结同一用户 → 幂等成功（与 main 服务端同态设置直接成功一致）
    const ev::ActionResult refreeze = setStatusSync(1, QStringLiteral("frozen"));
    QVERIFY2(refreeze.ok, qPrintable(refreeze.message));
    QCOMPARE(refreeze.errorCode, 0);
    QCOMPARE(userStatusSync(1), QStringLiteral("frozen"));

    // 解冻成功（frozen → active）
    const ev::ActionResult unfreeze = setStatusSync(1, QStringLiteral("active"));
    QVERIFY2(unfreeze.ok, qPrintable(unfreeze.message));
    QCOMPARE(userStatusSync(1), QStringLiteral("active"));

    // 非法状态值 → 1002 INVALID_REQUEST
    const ev::ActionResult bogus = setStatusSync(1, QStringLiteral("banned"));
    QVERIFY(!bogus.ok);
    QCOMPARE(bogus.errorCode, 1002);

    // 用户不存在 → 1200 NOT_FOUND
    const ev::ActionResult missing = setStatusSync(99, QStringLiteral("frozen"));
    QVERIFY(!missing.ok);
    QCOMPARE(missing.errorCode, 1200);
}

void TestUi::pilePageRestartButtonAppliesSimulatedRestart()
{
    // UI 集成：选中故障桩 → "重启选中桩" → 成功后提示行可观察 + 列表刷新，
    // 桩转 idle 后按钮自动禁用；选中充电中桩 → 按钮直接禁用
    // （UI 不展示业务上不可执行的操作；数据层 1201 由
    // mockActionsEnforcePileRestartStateRules 单独覆盖）。
    MockAdminRepository repository;
    PilePage page(&repository);
    page.refresh(ev::mockdata::DataMode::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(page.visibleRowCount(), 6, 3000);

    auto *button = page.findChild<QPushButton *>(QStringLiteral("pileRestartButton"));
    auto *hint = page.findChild<QLabel *>(QStringLiteral("pilePageHint"));
    QVERIFY(button);
    QVERIFY(hint);
    QVERIFY(!button->isEnabled()); // 无选中行不可用

    // 选中故障桩 P-101-C（focusPile 清除筛选并选中）
    page.focusPile(QStringLiteral("P-101-C"));
    QTRY_COMPARE_WITH_TIMEOUT(page.currentPileCode(), QStringLiteral("P-101-C"), 1000);
    QVERIFY(button->isEnabled());

    QTest::mouseClick(button, Qt::LeftButton);
    // 动作(500ms) + 自动刷新(500ms) 后提示行展示成功结果
    QTRY_VERIFY_WITH_TIMEOUT(hint->text().contains(QStringLiteral("已重启")), 3000);

    // 数据层快照已持久：再次拉取该桩为 idle
    {
        QObject context;
        bool done = false;
        bool idleAfterRestart = false;
        repository.fetchPiles(
            &context, [&](const ev::ListResult<ev::PileInfo> &result) {
                for (const ev::PileInfo &pile : result.items) {
                    if (pile.pileCode == QStringLiteral("P-101-C"))
                        idleAfterRestart = pile.status == ev::PileStatus::Idle;
                }
                done = true;
            });
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 3000)
            QTest::qWait(20);
        QVERIFY2(idleAfterRestart, "重启后桩状态应为 idle");
    }

    // 重启成功后桩已转 idle（上面数据层校验）：列表刷新后按钮自动禁用，
    // 不残留"可点但必被 1201 拒绝"的窗口
    QVERIFY(!button->isEnabled());

    // 充电中桩 → 按钮禁用：UI 不展示业务上不可执行的操作
    page.focusPile(QStringLiteral("P-101-A"));
    QTRY_COMPARE_WITH_TIMEOUT(page.currentPileCode(), QStringLiteral("P-101-A"), 1000);
    QVERIFY(!button->isEnabled());
}

void TestUi::userPageStatusButtonFlipsSelectedUser()
{
    // UI 集成：选中用户 → 按钮文案随状态切换（正常→冻结 / 冻结→解冻），
    // 点击后列表刷新展示新状态，模拟操作可观察、不冒充真实服务端。
    MockAdminRepository repository;
    UserPage page(&repository);
    page.refresh(ev::mockdata::DataMode::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(page.visibleRowCount(), 3, 3000);

    auto *button = page.findChild<QPushButton *>(QStringLiteral("userStatusButton"));
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("userTable"));
    QVERIFY(button);
    QVERIFY(table);
    QVERIFY(!button->isEnabled()); // 无选中行不可用

    // 用户 1（active）→ 冻结
    table->selectRow(0);
    QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 1000);
    QCOMPARE(button->text(), QStringLiteral("冻结选中用户"));
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(table->item(0, 3)->text(), QStringLiteral("冻结"), 3000);

    // 重选后文案切到"解冻" → 解冻回正常
    table->selectRow(0);
    QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 1000);
    QCOMPARE(button->text(), QStringLiteral("解冻选中用户"));
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(table->item(0, 3)->text(), QStringLiteral("正常"), 3000);

    // 预置冻结用户 2 → 按钮直接是"解冻"
    table->selectRow(1);
    QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 1000);
    QCOMPARE(button->text(), QStringLiteral("解冻选中用户"));
}

// ---- T1 静态图取景/投影纯函数（docs/role-c-admin-map-renderer-plan.md §4 T1）----
// 校准演示站对（附录 B）：S1' (41.714729,123.449597)、S2' (41.805727,123.440030)

void TestUi::viewportDemoPairLocksToZoom12AndCenter()
{
    // 已知向量（计划 §3.3）：640×480 → center≈(41.760228,123.444814)、zoom=12
    const QVector<ev::MapLatLng> stations = {
        {41.714729, 123.449597},
        {41.805727, 123.440030},
    };
    ev::StaticMapView view;
    QVERIFY(ev::computeMapView(stations, QSize(640, 480), &view));
    QCOMPARE(view.zoom, 12);
    QCOMPARE(view.width, 640);
    QCOMPARE(view.height, 480);
    QVERIFY2(qAbs(view.centerLat - 41.760228) < 1e-5, "centerLat 应为 bbox 纬度中点");
    QVERIFY2(qAbs(view.centerLng - 123.4448135) < 1e-5, "centerLng 应为 bbox 经度中点");
}

void TestUi::viewportSingleStationFallsBackToZoom14()
{
    const QVector<ev::MapLatLng> stations = {{41.714729, 123.449597}};
    ev::StaticMapView view;
    QVERIFY(ev::computeMapView(stations, QSize(640, 480), &view));
    QCOMPARE(view.zoom, 14);
    QCOMPARE(view.centerLat, 41.714729);
    QCOMPARE(view.centerLng, 123.449597);
}

void TestUi::viewportZeroSpanAxisExcludesThatAxisFromZoomConstraint()
{
    // 同经度异纬度两站（跨度 0.1°）：zoom 只按纬度轴约束（z12 适配、z13 超出），
    // center = bbox 中点——不得退化为单站 zoom14（评审 P1）
    const QVector<ev::MapLatLng> stations = {
        {41.7, 123.44},
        {41.8, 123.44},
    };
    ev::StaticMapView view;
    QVERIFY(ev::computeMapView(stations, QSize(640, 480), &view));
    QCOMPARE(view.zoom, 12);
    QCOMPARE(view.centerLat, 41.75);
    QCOMPARE(view.centerLng, 123.44);
}

void TestUi::viewportEmptyOrAllInvalidReturnsFalse()
{
    ev::StaticMapView view;
    QVERIFY(!ev::computeMapView({}, QSize(640, 480), &view));

    // 全部坐标非法：NaN / 纬度越界 / 经度越界 / 无穷
    const QVector<ev::MapLatLng> allInvalid = {
        {qQNaN(), 0.0},
        {91.0, 0.0},
        {-91.0, 0.0},
        {0.0, 181.0},
        {0.0, -181.0},
        {std::numeric_limits<double>::infinity(), 0.0},
    };
    QVERIFY(!ev::computeMapView(allInvalid, QSize(640, 480), &view));

    // 失败后 view 被写回默认无效值，调用方不得消费陈旧值
    view.zoom = 99;
    view.width = 99;
    view.height = 99;
    QVERIFY(!ev::computeMapView({}, QSize(640, 480), &view));
    QCOMPARE(view.zoom, 10);
    QCOMPARE(view.width, 0);
    QCOMPARE(view.height, 0);
}

void TestUi::viewportSpanTooLargeReturnsFalse()
{
    // 经度跨度 170°：最小 zoom=10 也远超 640px 请求宽度 → 区域过大不可取景
    const QVector<ev::MapLatLng> stations = {
        {0.0, 0.0},
        {0.0, 170.0},
    };
    ev::StaticMapView view;
    QVERIFY(!ev::computeMapView(stations, QSize(640, 480), &view));
}

void TestUi::viewportSkipsInvalidStations()
{
    // 混入 NaN / 纬度越界 / 经度越界站 → 净化剔除后按合法站取景，结果同纯合法对
    const QVector<ev::MapLatLng> mixed = {
        {41.714729, 123.449597},
        {qQNaN(), 0.0},
        {91.0, 0.0},
        {0.0, 200.0},
        {41.805727, 123.440030},
    };
    ev::StaticMapView mixedView;
    QVERIFY(ev::computeMapView(mixed, QSize(640, 480), &mixedView));

    const QVector<ev::MapLatLng> clean = {
        {41.714729, 123.449597},
        {41.805727, 123.440030},
    };
    ev::StaticMapView cleanView;
    QVERIFY(ev::computeMapView(clean, QSize(640, 480), &cleanView));
    QCOMPARE(mixedView.zoom, cleanView.zoom);
    QCOMPARE(mixedView.centerLat, cleanView.centerLat);
    QCOMPARE(mixedView.centerLng, cleanView.centerLng);
}

void TestUi::projectionCenterMapsToImageCenter()
{
    const ev::StaticMapView view = {41.72, 123.44, 13, 640, 480};
    const QPointF p = ev::lonLatToWidgetPoint({view.centerLat, view.centerLng}, view);
    QCOMPARE(p.x(), 320.0);
    QCOMPARE(p.y(), 240.0);
}

void TestUi::projectionKnownOffsetsMatchLockedFormula()
{
    // 固定中心 (41.72°,123.44°)、640×480：z13 +0.01°lng → +58.254222px、
    // +0.01°lat → −78.052375px（计划 §1 D7 精确式期望，容差 0.0001px；v1.3 修正方向）
    const ev::StaticMapView view13 = {41.72, 123.44, 13, 640, 480};
    const QPointF east = ev::lonLatToWidgetPoint({41.72, 123.45}, view13);
    QVERIFY2(qAbs(east.x() - (320.0 + 58.254222)) < 0.0001, "z13 东移 0.01° x 偏移");
    QCOMPARE(east.y(), 240.0); // 同纬度 → 精确图中心行

    const QPointF north = ev::lonLatToWidgetPoint({41.73, 123.44}, view13);
    QCOMPARE(north.x(), 320.0); // 同经度 → 精确图中心列
    QVERIFY2(qAbs(north.y() - (240.0 - 78.052375)) < 0.0001, "z13 北移 0.01° y 偏移（图像 y 向下为正，北侧更小）");

    // z12 两者减半（同输入同实现，除以 2 无舍入误差 → 直接断言半值窗口）
    const ev::StaticMapView view12 = {41.72, 123.44, 12, 640, 480};
    const QPointF east12 = ev::lonLatToWidgetPoint({41.72, 123.45}, view12);
    const QPointF north12 = ev::lonLatToWidgetPoint({41.73, 123.44}, view12);
    QVERIFY2(qAbs(east12.x() - (320.0 + 58.254222 / 2.0)) < 0.0001, "z12 东移 x 偏移减半");
    QVERIFY2(qAbs(north12.y() - (240.0 - 78.052375 / 2.0)) < 0.0001, "z12 北移 y 偏移减半");
}

void TestUi::projectionExactFormulaDiffersFromDerivativeApproximationOnWideSpan()
{
    // ±0.045° 跨度点 (41.765,123.395)：实现必须按 D7 精确式（不得用导数 × Δ 近似）
    constexpr double kPi = 3.141592653589793238462643383279502884;
    const ev::StaticMapView view = {41.72, 123.44, 13, 640, 480};
    const QPointF p = ev::lonLatToWidgetPoint({41.765, 123.395}, view);

    // 测试内独立实现（不复用被测代码）：
    //   W=256·2^z；y_world(φ)=W/2·(1−asinh(tan(πφ/180))/π)
    const double w = 256.0 * 8192.0; // = 256·2^13（z13）
    const auto yWorld = [kPi](double latDeg, double widthPx) {
        const double half = widthPx * 0.5;
        const double phi = latDeg * kPi / 180.0;
        return half * (1.0 - std::asinh(std::tan(phi)) / kPi);
    };
    const double yCenter = yWorld(41.72, w);
    const double xExact = 320.0 + (123.395 - 123.44) * w / 360.0;
    const double yExact = 240.0 + yWorld(41.765, w) - yCenter;
    QVERIFY2(qAbs(p.x() - xExact) < 1e-6, "x 必须与精确式一致");
    QVERIFY2(qAbs(p.y() - yExact) < 1e-6, "y 必须与精确式一致（防偷用导数近似）");

    // 导数近似（仅核对用，非实现）：y ≈ 240 − Δlat·W/(360·cosφ0)
    const double yApprox = 240.0 - 0.045 * w / (360.0 * std::cos(41.72 * kPi / 180.0));
    QVERIFY2(std::fabs(yExact - yApprox) > 0.0, "大跨度下精确式与导数近似必须可区分（测试非平凡）");
    qDebug("wide-span exact-vs-derivative y diff = %.6f px", yExact - yApprox);
}

void TestUi::projectionNorthIsAboveAndSouthIsBelowCenter()
{
    // 独立方向断言（不调用被测投影生成期望）：北侧点 y 更小（图上更靠上）、南侧更大
    const ev::StaticMapView view = {41.72, 123.44, 13, 640, 480};
    const QPointF north = ev::lonLatToWidgetPoint({41.73, 123.44}, view);
    const QPointF south = ev::lonLatToWidgetPoint({41.71, 123.44}, view);
    QVERIFY2(north.y() < 240.0, "北侧点必须位于图中心上方（y 更小）");
    QVERIFY2(south.y() > 240.0, "南侧点必须位于图中心下方（y 更大）");
    QVERIFY2(north.y() < south.y(), "北侧点必须在南侧点上方");
    QCOMPARE(north.x(), south.x()); // 同经度 → 同一列
}

void TestUi::viewportRejectsPolesAndOutOfProjectionRange()
{
    ev::StaticMapView view;
    // 纬度 ±90°（业务合法但超出地图投影范围）→ 不可取景（不静默剔除，整体失败）
    QVERIFY(!ev::computeMapView({{90.0, 0.0}}, QSize(640, 480), &view));
    QVERIFY(!ev::computeMapView({{-90.0, 0.0}}, QSize(640, 480), &view));
    // 超出保护边界 ±85.05112878° → 失败
    QVERIFY(!ev::computeMapView({{85.2, 0.0}}, QSize(640, 480), &view));
    QVERIFY(!ev::computeMapView({{-85.2, 0.0}}, QSize(640, 480), &view));

    // 边界上（恰好 85.05112878°）→ 成功且 view 与投影点有限（y 应 ≈ 0 世界行 → 图中心）
    QVERIFY(ev::computeMapView({{85.05112878, 0.0}}, QSize(640, 480), &view));
    QCOMPARE(view.zoom, 14);
    const QPointF p = ev::lonLatToWidgetPoint({85.05112878, 0.0}, view);
    QVERIFY(qIsFinite(p.x()) && qIsFinite(p.y()));
    QVERIFY2(qAbs(p.y() - 240.0) < 1e-6, "边界纬度应投影到 Web-Mercator 世界行 0（图中心行）");

    // 边界内 → 成功
    QVERIFY(ev::computeMapView({{85.0, 0.0}}, QSize(640, 480), &view));
    QVERIFY(ev::computeMapView({{-85.0, 0.0}}, QSize(640, 480), &view));
}

void TestUi::viewportRejectsNonPositiveSize()
{
    // 尺寸边界先于一切（含单站 zoom14 分支，计划 §3.5）
    const QVector<ev::MapLatLng> single = {{41.72, 123.44}};
    ev::StaticMapView view;
    QVERIFY(!ev::computeMapView(single, QSize(0, 480), &view));
    QVERIFY(!ev::computeMapView(single, QSize(640, 0), &view));
    QVERIFY(!ev::computeMapView(single, QSize(-640, 480), &view));
    QVERIFY(!ev::computeMapView(single, QSize(640, -480), &view));
    QVERIFY(!ev::computeMapView(single, QSize(), &view)); // 默认 0×0
}

void TestUi::fingerprintStableAndChangesOnViewChange()
{
    const QVector<ev::MapLatLng> stations = {
        {41.714729, 123.449597},
        {41.805727, 123.440030},
    };
    ev::StaticMapView view;
    QVERIFY(ev::computeMapView(stations, QSize(640, 480), &view));

    const quint64 f1 = ev::requestFingerprint(view);
    QCOMPARE(ev::requestFingerprint(view), f1); // 同视图稳定

    // center 亚量化微扰（1e-7 度 << 半桶 5e-6）→ 指纹不变（double 尾数抖动免疫）
    ev::StaticMapView tiny = view;
    tiny.centerLat += 1e-7;
    tiny.centerLng -= 1e-7;
    QCOMPARE(ev::requestFingerprint(tiny), f1);

    // 任一字段跨量化变化 → 指纹必变
    ev::StaticMapView changed = view;
    changed.zoom = view.zoom + 1;
    QVERIFY(ev::requestFingerprint(changed) != f1);
    changed = view;
    changed.width += 1;
    QVERIFY(ev::requestFingerprint(changed) != f1);
    changed = view;
    changed.height += 1;
    QVERIFY(ev::requestFingerprint(changed) != f1);
    changed = view;
    changed.centerLat += 0.001;
    QVERIFY(ev::requestFingerprint(changed) != f1);
    changed = view;
    changed.centerLng -= 0.001;
    QVERIFY(ev::requestFingerprint(changed) != f1);
}

// ---- T2 静态图图片提供者（docs/role-c-admin-map-renderer-plan.md §4 T2）----
// 全部用例显式传 key + baseUrl=本地假服务器（零真实网络；假服务器只监听 127.0.0.1）

void TestUi::urlBuilderMatchesProbeParameters()
{
    // 探针形态：center=纬度,经度 & zoom & size=宽*高 & key（T0 实证参数名）
    ev::StaticMapView view;
    view.centerLat = 41.760228;
    view.centerLng = 123.4448135;
    view.zoom = 12;
    view.width = 640;
    view.height = 480;
    const QString url = ev::buildStaticMapUrl(QStringLiteral("K1"), view);
    QVERIFY2(url.startsWith(QStringLiteral("https://apis.map.qq.com/ws/staticmap/v2/")),
             "默认端点应为腾讯官方静态图 v2");
    QVERIFY2(url.contains(QStringLiteral("center=41.76023,123.44481")), "center=lat,lng 精度 1e-5");
    QVERIFY2(url.contains(QStringLiteral("zoom=12")), "zoom 整数");
    QVERIFY2(url.contains(QStringLiteral("size=640*480")), "size 用 * 分隔");
    QVERIFY2(url.contains(QStringLiteral("key=K1")), "key 在 URL 尾部");

    // center 打印精度与 requestFingerprint 量化一致：指纹不变 ⇒ URL 不变
    view.centerLat += 1e-7;
    view.centerLng -= 1e-7;
    QCOMPARE(ev::buildStaticMapUrl(QStringLiteral("K1"), view), url);
}

void TestUi::providerNoKeyShortCircuitsWithoutNetwork()
{
    // 空 key：canFetch=false、fetch 同步 NotConfigured——不建网络对象、不发请求
    ev::StaticMapImageProvider provider{QString()};
    QVERIFY(!provider.canFetch());

    ev::StaticMapView view;
    view.width = 640;
    view.height = 480;
    int seqGot = -1;
    bool okGot = true;
    ev::MapImageProvider::Failure failureGot = ev::MapImageProvider::Failure::None;
    bool delivered = false;
    provider.fetch(view, 3, [&](int seq, bool ok, const QImage &,
                                ev::MapImageProvider::Failure failure) {
        seqGot = seq;
        okGot = ok;
        failureGot = failure;
        delivered = true;
    });
    QVERIFY2(delivered, "空 key 必须同步短路（不发请求）");
    QCOMPARE(seqGot, 3);
    QVERIFY(!okGot);
    QCOMPARE(failureGot, ev::MapImageProvider::Failure::NotConfigured);
}

void TestUi::providerMapsQuotaErrorToReason()
{
    // 配额拒绝（腾讯 status 121）→ Failure::Quota；回调无文本参数 + 实现零日志，
    // URL/key 不可能进入任何日志或错误串（计划 §3.2/§4 T2）
    FakeStaticMapServer server;
    QVERIFY2(server.listen(), "本地假服务器监听失败");
    server.enqueue({403, QByteArrayLiteral("{\"status\":121,\"message\":\"daily quota\"}"),
                    QByteArrayLiteral("application/json")});

    ev::StaticMapImageProvider provider(QStringLiteral("TEST-KEY"), nullptr, server.baseUrl());
    ev::StaticMapView view;
    view.centerLat = 41.76;
    view.centerLng = 123.44;
    view.zoom = 12;
    view.width = 640;
    view.height = 480;

    bool okGot = true;
    ev::MapImageProvider::Failure failureGot = ev::MapImageProvider::Failure::None;
    bool delivered = false;
    provider.fetch(view, 7, [&](int, bool ok, const QImage &,
                                ev::MapImageProvider::Failure failure) {
        okGot = ok;
        failureGot = failure;
        delivered = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(delivered, 3000);
    QVERIFY(!okGot);
    QCOMPARE(failureGot, ev::MapImageProvider::Failure::Quota);
    QCOMPARE(server.requestCount, 1);
}

void TestUi::providerHandlesHttpErrorAndTimeout()
{
    FakeStaticMapServer server;
    QVERIFY2(server.listen(), "本地假服务器监听失败");
    ev::StaticMapImageProvider provider(QStringLiteral("TEST-KEY"), nullptr, server.baseUrl());
    provider.setTransferTimeoutMs(200); // 超时测试不等待默认 5s
    ev::StaticMapView view;
    view.centerLat = 41.76;
    view.centerLng = 123.44;
    view.zoom = 12;
    view.width = 640;
    view.height = 480;

    // HTTP 500 → Failure::Http
    server.enqueue({500, QByteArrayLiteral("boom"), QByteArrayLiteral("text/plain")});
    bool okGot = true;
    ev::MapImageProvider::Failure failureGot = ev::MapImageProvider::Failure::None;
    bool delivered = false;
    provider.fetch(view, 1, [&](int, bool ok, const QImage &,
                                ev::MapImageProvider::Failure failure) {
        okGot = ok;
        failureGot = failure;
        delivered = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(delivered, 3000);
    QVERIFY(!okGot);
    QCOMPARE(failureGot, ev::MapImageProvider::Failure::Http);

    // 服务器静默不回包 → 传输超时 → Failure::Network
    server.enqueue({200, tinyPngBytes(), QByteArrayLiteral("image/png"), 0, true});
    delivered = false;
    okGot = true;
    provider.fetch(view, 2, [&](int, bool ok, const QImage &,
                                ev::MapImageProvider::Failure failure) {
        okGot = ok;
        failureGot = failure;
        delivered = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(delivered, 3000);
    QVERIFY(!okGot);
    QCOMPARE(failureGot, ev::MapImageProvider::Failure::Network);
}

void TestUi::providerRejectsNonImageBody()
{
    FakeStaticMapServer server;
    QVERIFY2(server.listen(), "本地假服务器监听失败");
    // HTTP 200 但响应体不是可解码图像 → Failure::BadImage
    server.enqueue({200, QByteArrayLiteral("definitely not a png"),
                    QByteArrayLiteral("text/plain")});

    ev::StaticMapImageProvider provider(QStringLiteral("TEST-KEY"), nullptr, server.baseUrl());
    ev::StaticMapView view;
    view.centerLat = 41.76;
    view.centerLng = 123.44;
    view.zoom = 12;
    view.width = 640;
    view.height = 480;

    bool okGot = true;
    ev::MapImageProvider::Failure failureGot = ev::MapImageProvider::Failure::None;
    bool delivered = false;
    provider.fetch(view, 4, [&](int, bool ok, const QImage &,
                                ev::MapImageProvider::Failure failure) {
        okGot = ok;
        failureGot = failure;
        delivered = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(delivered, 3000);
    QVERIFY(!okGot);
    QCOMPARE(failureGot, ev::MapImageProvider::Failure::BadImage);
}

void TestUi::providerCancelAllStopsDelivery()
{
    FakeStaticMapServer server;
    QVERIFY2(server.listen(), "本地假服务器监听失败");
    // 响应延迟 400ms 才回包：cancelAll 必须在回包前取消（测试间隔内确定性）
    server.enqueue({200, tinyPngBytes(), QByteArrayLiteral("image/png"), 400});

    ev::StaticMapImageProvider provider(QStringLiteral("TEST-KEY"), nullptr, server.baseUrl());
    ev::StaticMapView view;
    view.centerLat = 41.76;
    view.centerLng = 123.44;
    view.zoom = 12;
    view.width = 640;
    view.height = 480;

    bool delivered = false;
    provider.fetch(view, 5, [&](int, bool, const QImage &,
                                ev::MapImageProvider::Failure) { delivered = true; });
    QTest::qWait(50); // 让请求发出（QNAM 建连毫秒级）
    provider.cancelAll();
    QTest::qWait(700); // 超过服务器 400ms 回包延迟
    QVERIFY2(!delivered, "cancelAll 后旧回调不得再触发");
}

// ---- T3 拓扑控件双模式（docs/role-c-admin-map-renderer-plan.md §4 T3）----

void TestUi::topologyModeWhenNoProviderKeepsLegacyBehavior()
{
    // 回归锁：无 provider → 拓扑模式，零请求、零标注、绘制不崩
    StationTopologyWidget topology;
    topology.resize(640, 480);
    topology.setStations(demoStationPair());
    QCOMPARE(topology.stationCount(), 2);
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);
    QCOMPARE(topology.mapDegradedNoteForTest(), QString());
    QCOMPARE(topology.fetchAttemptsForTest(), 0);
    QVERIFY(topology.mapImageForTest().isNull());
    QVERIFY(!topology.grab().isNull()); // paint 全路径（拓扑分支）
}

void TestUi::mapModeRendersProjectedPointsFromFakeImage()
{
    // Fake 成功：模式=Map、投影点 == T1 独立期望（同视图 640×480）、底图已存
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    topology.setStations(demoStationPair());

    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000); // debounce 300ms 后发出
    QCOMPARE(fake->pendingCount(), 1);
    fake->deliverLastSuccess();

    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);
    QVERIFY(!topology.mapImageForTest().isNull());
    QCOMPARE(topology.mapDegradedNoteForTest(), QString());
    QCOMPARE(topology.fetchAttemptsForTest(), 1);

    // 投影点 == 独立 computeMapView + lonLatToWidgetPoint 期望（T1 同一公式与尺寸）
    const ev::StaticMapView view = demoMapView(640, 480);
    const QList<ev::StationInfo> stations = demoStationPair();
    for (int i = 0; i < stations.size(); ++i) {
        const QPointF expected = ev::lonLatToWidgetPoint(
            {stations.at(i).latitude, stations.at(i).longitude}, view);
        const QPointF actual = topology.mapPointForTest(i);
        QVERIFY2(qAbs(actual.x() - expected.x()) < 1e-6 && qAbs(actual.y() - expected.y()) < 1e-6,
                 "真图模式投影点必须与 D7 精确式一致");
    }
}

void TestUi::mapModeKeepsClickAndKeyboardActivation()
{
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    topology.setStations(demoStationPair());
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);
    fake->deliverLastSuccess();
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);

    // 点击命中地图投影点 → 与拓扑模式同一信号路径
    QSignalSpy spy(&topology, &StationTopologyWidget::stationActivated);
    const QPointF node = topology.mapPointForTest(0);
    QTest::mouseClick(&topology, Qt::LeftButton, Qt::NoModifier, node.toPoint());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 1); // S1' id=1（点击同步焦点到 0 号）

    // 键盘：焦点已在 0 号（点击同步）→ Enter 激活；再 Right 移到 1 号后 Space
    QTest::keyClick(&topology, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
    QTest::keyClick(&topology, Qt::Key_Right); // 0 → 1 号（S2'）
    QTest::keyClick(&topology, Qt::Key_Space);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
}

void TestUi::degradedFallsBackToTopologyWithNote()
{
    // Fake 失败（网络）→ 拓扑 + 服务降级标注（曾尝试真图失败才标注，§3.1）
    auto *fake = new FakeMapProvider;
    fake->setAutoFailure(ev::MapImageProvider::Failure::Network);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    topology.setStations(demoStationPair());
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);

    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);
    QCOMPARE(topology.mapDegradedNoteForTest(),
             StationTopologyWidget::kDegradedServiceNote);
    QVERIFY(topology.mapImageForTest().isNull());
    QCOMPARE(topology.stationCount(), 2); // 拓扑布局不受影响
    QVERIFY(!topology.grab().isNull());   // 拓扑分支绘制（含降级标注）不崩
}

void TestUi::retryBlockedWithinTimeGateThenAllowedAfterClockAdvance()
{
    auto *fake = new FakeMapProvider;
    fake->setAutoFailure(ev::MapImageProvider::Failure::Network);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    const QList<ev::StationInfo> stations = demoStationPair();
    topology.setStations(stations);
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);
    QCOMPARE(topology.mapDegradedNoteForTest(),
             StationTopologyWidget::kDegradedServiceNote);

    // 时间门内重复刷新（同数据）→ 不重试
    topology.setStations(stations);
    QTest::qWait(500); // 超过 debounce
    QCOMPARE(fake->fetchCount, 1);
    QCOMPARE(topology.fetchAttemptsForTest(), 1);

    // 推进假时钟 ≥30s 后再刷新 → 过门重试
    topology.advanceClockForTest(31000);
    topology.setStations(stations);
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 2, 2000);
    QCOMPARE(topology.fetchAttemptsForTest(), 2);
}

void TestUi::staleRegionCacheNotReusedForDifferentViewport()
{
    // A 视图成功缓存 → 切 B 视图且 B 拉取失败 → 拓扑+降级标注，
    // A 底图不得承载 B 标记（缓存仅指纹完全匹配才复用，§3.4）
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    topology.setStations(demoStationPair()); // A：校准站对
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);
    fake->deliverLastSuccess();
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);

    // B：不同区域（北京附近，中心/缩放必不同）
    topology.setStations({testStation(11, 39.90, 116.40),
                          testStation(12, 39.96, 116.46)});
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 2, 2000);
    fake->deliverLastFailure(ev::MapImageProvider::Failure::Network);

    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);
    QCOMPARE(topology.mapDegradedNoteForTest(),
             StationTopologyWidget::kDegradedServiceNote);

    // 失败后切回 A：时间门内不重试（缓存标记已随失败清除，A 图留在缓存但不可复用）
    topology.setStations(demoStationPair());
    QTest::qWait(500);
    QCOMPARE(fake->fetchCount, 2);
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);
}

void TestUi::supersededResponseAfterViewInvalidationIsDiscarded()
{
    // 慢请求在途 A → 数据换成 B（目标失效）→ 迟到交付旧 seq 必须丢弃；
    // 新 seq 正常应用（§3.4 generation 语义）
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    topology.setStations(demoStationPair()); // A：在途（未交付）
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);
    QCOMPARE(fake->pendingCount(), 1);

    // 目标变化（B）→ 立即作废 + 新请求
    topology.setStations({testStation(11, 39.90, 116.40),
                          testStation(12, 39.96, 116.46)});
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 2, 2000);
    QCOMPARE(fake->cancelCount, 1); // 作废时调用过 cancelAll

    // 旧响应（seq=1，A 视图）迟到 → 丢弃：不落地、不切模式
    fake->deliverSeq(1);
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);
    QVERIFY(topology.mapImageForTest().isNull());

    // 新响应（seq=2，B 视图）→ 正常应用
    fake->deliverLastSuccess();
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);
    QVERIFY(!topology.mapImageForTest().isNull());
}

void TestUi::sameViewportRefreshKeepsInFlightRequest()
{
    // 慢请求在途，重复刷新相同坐标（仅业务状态变化）→ 不取消/不重发，
    // 原响应仍可应用（§3.4 同视图复用）
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    const QList<ev::StationInfo> stations = demoStationPair();
    topology.setStations(stations);
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);

    topology.setStations(stations); // 同坐标刷新（状态更新场景）
    QTest::qWait(500);              // 超过 debounce：应无新请求、无取消
    QCOMPARE(fake->fetchCount, 1);
    QCOMPARE(fake->cancelCount, 0);

    fake->deliverLastSuccess(); // 原响应仍可应用
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);
    QCOMPARE(topology.fetchAttemptsForTest(), 1);
}

void TestUi::outOfProjectionRangeStationRemainsInTopology()
{
    // 业务合法极区站（85.5° 超 Web-Mercator 保护边界）→ 保留在净化后列表
    // （stationCount/键盘/点击映射），禁止拉图，拓扑布局有限（§3.5）
    auto *fake = new FakeMapProvider; // 不应产生任何请求

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    topology.setStations({testStation(1, 41.71, 123.44),
                          testStation(2, 85.5, 123.44)});
    QTest::qWait(500);

    QCOMPARE(topology.stationCount(), 2); // 极区站业务合法 → 保留
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);
    QCOMPARE(topology.mapDegradedNoteForTest(),
             StationTopologyWidget::kDegradedProjectionNote);
    QCOMPARE(fake->fetchCount, 0); // 禁止拉图
    QVERIFY(!topology.grab().isNull());

    // 键盘映射含极区站（净化后列表有序、可激活）
    QSignalSpy spy(&topology, &StationTopologyWidget::stationActivated);
    QTest::keyClick(&topology, Qt::Key_Right);
    QTest::keyClick(&topology, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
    QTest::keyClick(&topology, Qt::Key_Right);
    QTest::keyClick(&topology, Qt::Key_Space);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
    // 拓扑投影点有限（归一化不吃进非法几何）
    const QPointF p1 = topology.mapPointForTest(1);
    QVERIFY(qIsFinite(p1.x()) && qIsFinite(p1.y()));
}

void TestUi::resizeBucketChangeTriggersRefetch()
{
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    topology.show(); // offscreen：使后续 resize() 同步派发 resizeEvent（不可见时事件延迟）
    topology.setStations(demoStationPair());
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);
    fake->deliverLastSuccess();
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);

    // resize 跨尺寸桶（640×480 → 704×512）→ 新目标 → 新请求
    topology.resize(704, 512);
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 2, 2000);

    // resize 回原桶 → 缓存指纹匹配 → 直接应用缓存，不重发
    topology.resize(640, 480);
    QTest::qWait(500);
    QCOMPARE(fake->fetchCount, 2);
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);
}

void TestUi::degradedNoteClearsOnNextSuccessfulFetch()
{
    auto *fake = new FakeMapProvider;
    fake->setAutoFailure(ev::MapImageProvider::Failure::Network);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    const QList<ev::StationInfo> stations = demoStationPair();
    topology.setStations(stations);
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);
    QCOMPARE(topology.mapDegradedNoteForTest(),
             StationTopologyWidget::kDegradedServiceNote);

    // 过门后成功 → 标注清除、进入真图模式
    topology.advanceClockForTest(31000);
    fake->setAutoFailure(ev::MapImageProvider::Failure::None);
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);
    topology.setStations(stations);
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 2, 2000);
    fake->deliverLastSuccess();

    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);
    QCOMPARE(topology.mapDegradedNoteForTest(), QString());
}

void TestUi::invalidStationCoordinatesSanitizedBeforeLayout()
{
    // NaN/越业务范围站 → setStations 入口净化：stationCount/取景/布局只含合法站
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    ev::StationInfo bad1 = testStation(2, 41.8, 123.44);
    bad1.latitude = qQNaN();
    ev::StationInfo bad2 = testStation(3, 41.8, 123.44);
    bad2.latitude = 91.0; // 越业务纬度
    ev::StationInfo bad3 = testStation(4, 41.8, 123.44);
    bad3.longitude = 181.0; // 越业务经度
    topology.setStations({demoStationPair().at(0), bad1, bad2, bad3});

    QCOMPARE(topology.stationCount(), 1); // 只计净化后
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000); // 取景只含合法站（单站 zoom14）
    fake->deliverLastSuccess();
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);

    // 布局与 m_stations 对齐：越界索引返回空点
    const QPointF p0 = topology.mapPointForTest(0);
    const ev::StationInfo &only = demoStationPair().at(0);
    ev::StaticMapView singleView;
    QVERIFY(ev::computeMapView({{only.latitude, only.longitude}}, QSize(640, 480),
                               &singleView));
    const QPointF expected = ev::lonLatToWidgetPoint({only.latitude, only.longitude},
                                                     singleView);
    QVERIFY2(qAbs(p0.x() - expected.x()) < 1e-6 && qAbs(p0.y() - expected.y()) < 1e-6,
             "净化后单站投影必须与独立期望一致");
    QCOMPARE(topology.mapPointForTest(1), QPointF()); // 越界 → 空点

    // 拓扑布局同源净化：无 provider 且混入非法站 → 归一化不崩（NaN 几何防线）
    StationTopologyWidget topologyOnly;
    topologyOnly.resize(640, 480);
    topologyOnly.setStations({demoStationPair().at(0), bad1, bad2});
    QCOMPARE(topologyOnly.stationCount(), 1);
    QVERIFY(!topologyOnly.grab().isNull());
}

void TestUi::providerCancelAllMultipleInflightSafe()
{
    // 评审 P1-3 回归：多个在途请求时 cancelAll——abort 可能同步触发 finished
    // 回调（onReplyFinished 修改 m_active），遍历中改容器是 UB。修复 = 快照先摘除。
    FakeStaticMapServer server;
    QVERIFY2(server.listen(), "本地假服务器监听失败");

    ev::StaticMapImageProvider provider(QStringLiteral("TEST-KEY"), nullptr, server.baseUrl());
    ev::StaticMapView viewA;
    viewA.centerLat = 41.76;
    viewA.centerLng = 123.44;
    viewA.zoom = 12;
    viewA.width = 640;
    viewA.height = 480;
    ev::StaticMapView viewB = viewA;
    viewB.centerLat = 39.9; // 不同视图（两请求独立在途）

    int deliveries = 0;
    provider.fetch(viewA, 1, [&](int, bool, const QImage &, ev::MapImageProvider::Failure) {
        ++deliveries;
    });
    provider.fetch(viewB, 2, [&](int, bool, const QImage &, ev::MapImageProvider::Failure) {
        ++deliveries;
    });
    QTest::qWait(80); // 两请求发出（进入 m_active）
    provider.cancelAll(); // 修复前：容器遍历中同步回调修改 = UB
    QTest::qWait(500);
    QCOMPARE(deliveries, 0); // 两回调均不得触发

    // 取消后容器一致：新请求可正常完成（假服务器随后回包成功图）
    server.enqueue({200, tinyPngBytes(), QByteArrayLiteral("image/png")});
    bool okGot = false;
    provider.fetch(viewA, 3, [&](int, bool ok, const QImage &, ev::MapImageProvider::Failure) {
        okGot = ok;
    });
    QTRY_VERIFY_WITH_TIMEOUT(okGot, 3000);
}

void TestUi::providerTotalDeadlineFiresOnDripTransfer()
{
    // 评审 P2-2 回归：transferTimeout 只在数据停滞时触发——持续滴答的慢传输
    // 永不触发它。总时长上限由 provider 内 deadline 定时器保证（到期 abort → Network）。
    FakeStaticMapServer server;
    QVERIFY2(server.listen(), "本地假服务器监听失败");
    // 64 KiB 响应按 40ms/256B 滴答 ≈ 10s 才发完（远大于 300ms 测试窗口）
    server.enqueue({200, QByteArray(64 * 1024, 'x'), QByteArrayLiteral("image/png"),
                    0, false, 40, 256});

    ev::StaticMapImageProvider provider(QStringLiteral("TEST-KEY"), nullptr, server.baseUrl());
    provider.setTransferTimeoutMs(300); // 总超时 300ms（activity 超时同值也不会先触发）
    ev::StaticMapView view;
    view.centerLat = 41.76;
    view.centerLng = 123.44;
    view.zoom = 12;
    view.width = 640;
    view.height = 480;

    bool okGot = true;
    ev::MapImageProvider::Failure failureGot = ev::MapImageProvider::Failure::None;
    bool delivered = false;
    provider.fetch(view, 1, [&](int, bool ok, const QImage &,
                                ev::MapImageProvider::Failure failure) {
        okGot = ok;
        failureGot = failure;
        delivered = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(delivered, 3000);
    QVERIFY(!okGot);
    QCOMPARE(failureGot, ev::MapImageProvider::Failure::Network);
}

void TestUi::debounceStoppedWhenTargetBecomesUnreachable()
{
    // 评审 P1-1 回归：debounce 300ms 挂起期间目标失效（数据清空 / 变不可取景），
    // timer 到期不得再用失效前的旧目标发起请求
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    topology.setStations(demoStationPair()); // debounce 挂起（300ms 内不发出）
    topology.setStations({});                // 立即清空 → 目标失效
    QTest::qWait(600);                       // 超过 debounce 窗口
    QCOMPARE(fake->fetchCount, 0);           // 修复前 = 1（旧目标仍被请求）
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);
    QCOMPARE(topology.mapDegradedNoteForTest(), QString()); // 空态静默

    // 再次有数据 → debounce 正常发一次（机制存活）
    topology.setStations(demoStationPair());
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);

    // 变体：防抖挂起期间数据变为不可取景（超投影范围）→ 不发请求、落投影标注
    topology.setStations(demoStationPair()); // 重启 debounce
    topology.setStations({testStation(1, 41.71, 123.44),
                          testStation(2, 85.5, 123.44)}); // 立即替换为极区站
    QTest::qWait(600);
    QCOMPARE(fake->fetchCount, 1); // 修复前 = 2（旧目标仍被请求）
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);
    QCOMPARE(topology.mapDegradedNoteForTest(),
             StationTopologyWidget::kDegradedProjectionNote);
}

void TestUi::staleImageRetiredWhileSwitchingRegion()
{
    // 评审 P1-2 回归：切换区域后、新图到达前，旧底图不得继续显示
    // （否则新区域标记画在旧区域底图上）。修复前 renderMode 在此期间仍为 Map。
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    topology.setStations(demoStationPair()); // A：校准站对
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);
    fake->deliverLastSuccess();
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);

    // 切 B（北京附近）：B 请求在途未交付的整个窗口内，A 底图必须退役 → 拓扑
    topology.setStations({testStation(11, 39.90, 116.40),
                          testStation(12, 39.96, 116.46)});
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 2, 2000);
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);

    // B 图到达 → 恢复真图（B 视图）
    fake->deliverLastSuccess();
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);
    QVERIFY(!topology.mapImageForTest().isNull());
}

void TestUi::smallWidgetStaysTopologyWithoutRequest()
{
    // 评审 P2-1 回归：控件小于请求图 clamp 下限（320×240）时不发请求——
    // 否则拉取的图大于控件，paint 里偏移为负被跳过（图不绘制 + 投影错位）。
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(200, 200); // 小于 320×240 → 静默拓扑
    topology.show();           // offscreen：使后续 resize() 同步派发 resizeEvent
    topology.setStations(demoStationPair());
    QTest::qWait(500);
    QCOMPARE(fake->fetchCount, 0);
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Topology);
    QCOMPARE(topology.mapDegradedNoteForTest(), QString()); // 静默：无故障标注

    // 拉大到 ≥320×240 → 桶化请求尺寸 ≤ 控件尺寸（图必可绘制），正常拉图
    topology.resize(640, 480);
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);
    fake->deliverLastSuccess();
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);
    QVERIFY(!topology.grab().isNull()); // 绘制含底图路径，不崩
}

void TestUi::backToCachedViewCancelsPendingDifferentViewFetch()
{
    // 评审 P1 补充回归：成功 A → 切 B（300ms debounce 挂起未发出）→ 立即回 A
    // （缓存命中）——挂起的 B 防抖必须取消。修复前 timer 到期会用旧目标 B 发
    // 请求，图回来覆盖 A 缓存（目标 A 却显示 B 图）
    auto *fake = new FakeMapProvider;
    QImage mapImage(640, 480, QImage::Format_ARGB32);
    mapImage.fill(Qt::lightGray);
    fake->setImage(mapImage);

    StationTopologyWidget topology;
    topology.setMapImageProvider(fake);
    topology.resize(640, 480);
    const QList<ev::StationInfo> regionA = demoStationPair();
    topology.setStations(regionA);
    QTRY_COMPARE_WITH_TIMEOUT(fake->fetchCount, 1, 2000);
    fake->deliverLastSuccess();
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);

    // 切 B（防抖挂起）→ 立即回 A（缓存命中激活，同步 evaluate 不等待）
    topology.setStations({testStation(11, 39.90, 116.40),
                          testStation(12, 39.96, 116.46)});
    topology.setStations(regionA);
    QCOMPARE(topology.renderModeForTest(), StationTopologyWidget::RenderMode::Map);

    QTest::qWait(600); // 超过 debounce 窗口
    QCOMPARE(fake->fetchCount, 1); // 修复前 = 2（B 的防抖请求仍被发出）
    QCOMPARE(fake->pendingCount(), 0);
    QCOMPARE(topology.mapDegradedNoteForTest(), QString());
}

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
