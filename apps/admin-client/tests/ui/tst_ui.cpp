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
#include "theme/generated/theme_tokens.h"
#include "theme/theme.h"
#include "widgets/stationtopologywidget.h"
#include "widgets/statuspulsewidget.h"
#include "widgets/statustag.h"

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void statusTagsExposeProtocolState();
    void chargingAndFaultAnimateWhenMotionEnabled();
    void reducedMotionStopsAnimation();
    void themeLoadsGeneratedQss();
    void shellExposesProductAndSessionContext();
    void loginFieldsHaveAccessibleNames();
    void overviewShowsTraceableMockMetrics();
    void availabilityRateFormulaIsIndependent();
    void topologyUsesInformativeLineAndEmitsStationSelection();
    void attentionItemSwitchesToPilePage();
};

void TestUi::statusTagsExposeProtocolState()
{
    const QList<ev::PileStatus> states = {
        ev::PileStatus::Idle,
        ev::PileStatus::Reserved,
        ev::PileStatus::Charging,
        ev::PileStatus::Fault,
        ev::PileStatus::Offline,
    };
    for (const auto state : states) {
        StatusTag tag(state);
        QCOMPARE(tag.property("state").toString(), ev::pileStatusToProtocol(state));
        QCOMPARE(tag.text(), ev::pileStatusToDisplay(state));
    }
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

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
