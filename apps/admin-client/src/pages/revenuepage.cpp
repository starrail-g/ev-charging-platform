#include "revenuepage.h"

#include <QComboBox>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "data/adminrepository.h"
#include "theme/generated/theme_tokens.h"
#include "widgets/metriccard.h"
#include "widgets/revenuechartwidget.h"
#include "widgets/statestack.h"

namespace {
const QString kLoadErrorDisplay = QStringLiteral("接口错误：营收数据加载失败");

QString ymd(const QDate &date)
{
    return date.toString(Qt::ISODate); // UTC 日期原样(不本地化)
}
} // namespace

RevenuePage::RevenuePage(ev::AdminRepository *repository, QWidget *parent)
    : QWidget(parent)
    , m_repository(repository)
{
    setObjectName(QStringLiteral("revenuePage"));

    auto *content = new QWidget(this);

    // ── 顶部两张合计卡 ────────────────────────────────────────────────────────
    m_7dCard = new MetricCard(QStringLiteral("近 7 日营收"),
                              QStringLiteral("metricRevenue7d"), content);
    m_30dCard = new MetricCard(QStringLiteral("近 30 日营收"),
                               QStringLiteral("metricRevenue30d"), content);
    auto *cardsRow = new QHBoxLayout;
    cardsRow->setSpacing(12);
    cardsRow->addWidget(m_7dCard);
    cardsRow->addWidget(m_30dCard);

    // ── 主图面板: 工具栏 + Full 折线 ──────────────────────────────────────────
    auto *chartPanel = new QFrame(content);
    chartPanel->setProperty("panel", true);
    auto *chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(14, 12, 14, 12);
    chartLayout->setSpacing(10);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    auto *panelTitle = new QLabel(QStringLiteral("营收趋势"), chartPanel);
    QFont panelFont = panelTitle->font();
    panelFont.setPixelSize(13);
    panelFont.setBold(true);
    panelTitle->setFont(panelFont);
    toolbar->addWidget(panelTitle);
    toolbar->addStretch();

    m_rangeCombo = new QComboBox(chartPanel);
    m_rangeCombo->setObjectName(QStringLiteral("revenueRangeCombo"));
    m_rangeCombo->addItem(QStringLiteral("近 7 日"), 7);
    m_rangeCombo->addItem(QStringLiteral("近 30 日"), 30);
    connect(m_rangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RevenuePage::onComboChanged);
    toolbar->addWidget(m_rangeCombo);

    m_refreshButton = new QPushButton(QStringLiteral("刷新"), chartPanel);
    m_refreshButton->setObjectName(QStringLiteral("revenueRefreshButton"));
    connect(m_refreshButton, &QPushButton::clicked, this, &RevenuePage::refresh);
    toolbar->addWidget(m_refreshButton);
    chartLayout->addLayout(toolbar);

    m_chart = new ev::RevenueChartWidget(ev::RevenueChartWidget::Mode::Full,
                                         chartPanel);
    m_chart->setObjectName(QStringLiteral("revenueChart"));
    m_chart->setMinimumHeight(240);
    chartLayout->addWidget(m_chart, 1);

    m_seriesErrorLabel = new QLabel(chartPanel);
    m_seriesErrorLabel->setObjectName(QStringLiteral("revenueSeriesError"));
    m_seriesErrorLabel->setVisible(false);
    m_seriesErrorLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayFault.name()));
    chartLayout->addWidget(m_seriesErrorLabel);

    m_zeroHintLabel = new QLabel(chartPanel);
    m_zeroHintLabel->setObjectName(QStringLiteral("revenueZeroHint"));
    m_zeroHintLabel->setText(QStringLiteral("本期间暂无已结算营收"));
    m_zeroHintLabel->setVisible(false);
    m_zeroHintLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    chartLayout->addWidget(m_zeroHintLabel);

    // ── 每日明细表 ────────────────────────────────────────────────────────────
    auto *tablePanel = new QFrame(content);
    tablePanel->setProperty("panel", true);
    auto *tableLayout = new QVBoxLayout(tablePanel);
    tableLayout->setContentsMargins(14, 12, 14, 12);
    tableLayout->setSpacing(8);

    auto *tableTitle = new QLabel(QStringLiteral("每日营收"), tablePanel);
    QFont tableTitleFont = tableTitle->font();
    tableTitleFont.setPixelSize(13);
    tableTitleFont.setBold(true);
    tableTitle->setFont(tableTitleFont);
    tableLayout->addWidget(tableTitle);

    m_table = new QTableWidget(tablePanel);
    m_table->setObjectName(QStringLiteral("revenueDailyTable"));
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("日期（UTC）"), QStringLiteral("营收（元）")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    tableLayout->addWidget(m_table, 1);

    // ── 整页四态壳 + 底部更新时间 ────────────────────────────────────────────
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    contentLayout->addLayout(cardsRow);
    contentLayout->addWidget(chartPanel, 5);
    contentLayout->addWidget(tablePanel, 3);

    m_updatedLabel = new QLabel(content);
    m_updatedLabel->setObjectName(QStringLiteral("revenueUpdatedLabel"));
    m_updatedLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    QFont updatedFont = m_updatedLabel->font();
    updatedFont.setPixelSize(11);
    m_updatedLabel->setFont(updatedFont);
    contentLayout->addWidget(m_updatedLabel, 0, Qt::AlignRight);

    m_stateStack = new StateStack(this);
    m_stateStack->setContentWidget(content);
    m_stateStack->setRetryHandler([this] { refresh(); });
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stateStack);
}

void RevenuePage::refresh()
{
    const int generation = ++m_generation;
    setLoading(true);
    m_stateStack->showState(StateStack::State::Loading,
                            QStringLiteral("正在加载营收数据…"));
    m_repository->fetchOverview(this,
        [this, generation](const ev::OverviewResult &result) {
            if (generation != m_generation)
                return; // 过期/注销后的迟到回包丢弃
            setLoading(false);
            m_result = result;
            renderResult();
        });
}

void RevenuePage::setRange(int days)
{
    if (days != 7 && days != 30)
        return;
    if (days == m_days)
        return;
    m_days = days;
    const int index = m_rangeCombo->findData(days);
    if (index >= 0) {
        const QSignalBlocker blocker(m_rangeCombo);
        m_rangeCombo->setCurrentIndex(index);
    }
    renderSelected();
}

void RevenuePage::invalidatePendingLoads()
{
    ++m_generation;
    setLoading(false);
    m_result = ev::OverviewResult();
    m_days = 7;
    const QSignalBlocker blocker(m_rangeCombo);
    m_rangeCombo->setCurrentIndex(0);
    m_chart->clearSeries();
    m_table->setRowCount(0);
    m_seriesErrorLabel->setVisible(false);
    m_zeroHintLabel->setVisible(false);
    m_updatedLabel->clear();
    m_stateStack->showState(StateStack::State::Loading,
                            QStringLiteral("正在加载营收数据…"));
}

void RevenuePage::onComboChanged(int)
{
    const int days = m_rangeCombo->currentData().toInt();
    if (days != m_days && (days == 7 || days == 30)) {
        m_days = days;
        renderSelected(); // 本地切换, 不新增请求
    }
}

void RevenuePage::renderResult()
{
    if (!m_result.ok) {
        m_stateStack->showState(
            StateStack::State::Error,
            QStringLiteral("%1（%2）").arg(kLoadErrorDisplay, m_result.error));
        return;
    }
    if (!m_result.hasData) {
        m_stateStack->showState(StateStack::State::Empty,
                                QStringLiteral("暂无营收统计数据"));
        return;
    }
    renderCards();
    renderSelected();
    m_stateStack->showState(StateStack::State::Content);
}

void RevenuePage::renderCards()
{
    // 两卡金额 = 摘要级合计(序列损坏时摘要仍可用, 卡面不消失)
    m_7dCard->setValue(ev::formatYuanCents(m_result.stats.revenueCents));
    m_30dCard->setValue(ev::formatYuanCents(m_result.stats.revenue30dCents));
}

void RevenuePage::renderSelected()
{
    const bool seven = m_days == 7;
    const ev::RevenueSeries &series =
        seven ? m_result.stats.revenue7dSeries : m_result.stats.revenue30dSeries;

    if (!series.available) {
        m_chart->clearSeries();
        m_table->setRowCount(0);
        m_seriesErrorLabel->setText(
            QStringLiteral("近 %1 日逐日趋势暂不可用：%2")
                .arg(m_days)
                .arg(series.error.isEmpty() ? QStringLiteral("数据缺失")
                                            : series.error));
        m_seriesErrorLabel->setVisible(true);
        m_zeroHintLabel->setVisible(false);
        m_updatedLabel->setText(QStringLiteral("数据更新（UTC）：%1")
                                    .arg(m_result.stats.updatedAt));
        return;
    }

    m_chart->setSeries(series);
    m_seriesErrorLabel->setVisible(false);

    // 明细表: 日期(UTC, 升序与曲线一致) + 金额(整数分格式化, 不从图上浮点反算)
    m_table->setRowCount(series.days.size());
    for (int row = 0; row < series.days.size(); ++row) {
        const auto &day = series.days.at(row);
        auto *dateItem = new QTableWidgetItem(ymd(day.date));
        auto *amountItem =
            new QTableWidgetItem(ev::formatYuanCents(day.revenueCents));
        amountItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 0, dateItem);
        m_table->setItem(row, 1, amountItem);
    }

    // 零营收为正常零线: 辅助文案提示, 不误报为空
    m_zeroHintLabel->setVisible(series.totalCents == 0);
    m_updatedLabel->setText(QStringLiteral("数据更新（UTC）：%1")
                                .arg(series.updatedAt));
}

void RevenuePage::setLoading(bool loading)
{
    m_loading = loading;
    m_refreshButton->setEnabled(!loading);
}
