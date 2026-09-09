#include <QtTest>

#include <QtCharts/QCategoryAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QComboBox>
#include <QDate>
#include <QElapsedTimer>
#include <QGraphicsTextItem>
#include <QLabel>

#include <limits>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
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

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
