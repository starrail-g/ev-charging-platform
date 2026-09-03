#include <QtTest>

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>

#include "app/mainwindow.h"
#include "data/mockadminrepository.h"
#include "models/adminmodels.h"
#include "pages/loginpage.h"
#include "pages/overviewpage.h"
#include "pages/pilepage.h"
#include "pages/stationpage.h"
#include "pages/userpage.h"
#include "theme/generated/theme_tokens.h"
#include "theme/theme.h"
#include "widgets/aurorabackdrop.h"
#include "widgets/stationtopologywidget.h"
#include "widgets/statusglyphwidget.h"
#include "widgets/statuspulsewidget.h"
#include "widgets/statustag.h"

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
    void stationAndUserPagesRenderMockRows();
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
    QVERIFY(window.findChild<QLabel *>("productMark"));
    QVERIFY(window.findChild<QLabel *>("sessionBadge"));
    auto *nav = window.findChild<QListWidget *>("navList");
    QVERIFY(nav);
    QCOMPARE(nav->count(), 4);
    QCOMPARE(nav->spacing(), 12);
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
    QCOMPARE(page.findChild<QLabel *>("metricRevenue")->text(),
             QStringLiteral("¥2,865.40"));
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
    QTRY_COMPARE_WITH_TIMEOUT(pageStack->currentIndex(), 1, 1000); // 1 = 充电桩页
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

void TestUi::stationAndUserPagesRenderMockRows()
{
    StationPage stations;
    stations.refresh(ev::mockdata::DataMode::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(stations.visibleRowCount(), 2, 3000);
    UserPage users;
    users.refresh(ev::mockdata::DataMode::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(users.visibleRowCount(), 3, 3000);
}

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
