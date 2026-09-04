#include "overviewpage.h"

#include <algorithm>

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

#include "data/mockadminrepository.h"
#include "theme/generated/theme_tokens.h"
#include "widgets/metriccard.h"
#include "widgets/statestack.h"
#include "widgets/stationtopologywidget.h"

namespace {

const QString kSimulatedErrorDisplay = QStringLiteral("接口错误：模拟数据层失败");
const QString kFaultDisplay = QStringLiteral("故障");
const QString kOfflineDisplay = QStringLiteral("离线");

// 金额格式：整数分 → "¥2,865.40"（千分位，无浮点漂移；口径同 Web dashboard）
QString formatYuanCents(qint64 cents)
{
    const qint64 yuan = cents / 100;
    const QString frac = QString::number(qAbs(cents % 100)).rightJustified(2, QLatin1Char('0'));
    const QString digits = QString::number(qAbs(yuan));
    QString grouped;
    const int length = digits.size();
    for (int i = 0; i < length; ++i) {
        if (i > 0 && (length - i) % 3 == 0)
            grouped += QLatin1Char(',');
        grouped += digits.at(i);
    }
    if (yuan < 0)
        grouped.prepend(QLatin1Char('-'));
    return QStringLiteral("¥%1.%2").arg(grouped, frac);
}

QString attentionText(const ev::PileInfo &pile)
{
    return pile.status == ev::PileStatus::Fault ? kFaultDisplay : kOfflineDisplay;
}

QString attentionStationName(const ev::ListResult<ev::StationInfo> &stations, int stationId)
{
    for (const auto &station : stations.items) {
        if (station.id == stationId)
            return station.name;
    }
    return QString();
}

} // namespace

namespace ev {

double availabilityRate(const QList<PileInfo> &piles)
{
    if (piles.isEmpty())
        return 0.0;
    const int unavailable = std::count_if(piles.cbegin(), piles.cend(), [](const PileInfo &pile) {
        return pile.status == PileStatus::Fault || pile.status == PileStatus::Offline;
    });
    return double(piles.size() - unavailable) / double(piles.size());
}

} // namespace ev

OverviewPage::OverviewPage(ev::AdminRepository *repository, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("overviewPage"));

    if (!repository) {
        // 与 MainWindow 同策略：无注入时自建 Mock（第一阶段默认数据源）
        m_ownedRepository = std::make_unique<MockAdminRepository>();
        repository = m_ownedRepository.get();
    }
    m_repository = repository;

    // ---- 顶部：演示模式控制（第一阶段保留；9/6 Socket 接入后移除下拉） ----
    auto *demoControls = new QFrame(this);
    demoControls->setObjectName(QStringLiteral("demoControls"));
    demoControls->setProperty("panel", true);
    auto *demoLabel = new QLabel(QStringLiteral("演示控制"), demoControls);
    demoLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    QFont demoFont = demoLabel->font();
    demoFont.setPixelSize(11);
    demoLabel->setFont(demoFont);

    m_modeCombo = new QComboBox(demoControls);
    m_modeCombo->setObjectName(QStringLiteral("dataModeCombo"));
    m_modeCombo->addItem(QStringLiteral("数据模式：正常"), int(ev::mockdata::DataMode::Normal));
    m_modeCombo->addItem(QStringLiteral("数据模式：空"), int(ev::mockdata::DataMode::Empty));
    m_modeCombo->addItem(QStringLiteral("数据模式：错误"), int(ev::mockdata::DataMode::Error));

    auto *demoLayout = new QHBoxLayout(demoControls);
    demoLayout->setContentsMargins(10, 6, 10, 6);
    demoLayout->setSpacing(8);
    demoLayout->addWidget(demoLabel);
    demoLayout->addWidget(m_modeCombo);

    auto *topLayout = new QHBoxLayout;
    topLayout->addStretch();
    topLayout->addWidget(demoControls);

    // ---- 内容区：指标卡 + 需关注列表 + 站点态势 ----
    auto *content = new QWidget(this);

    // 4 张指标卡：值对象名固定，供测试/自动化定位（口径可追溯 mockdataset）
    m_pileTotalCard = new MetricCard(QStringLiteral("总桩数"), QStringLiteral("metricPileTotal"), content);
    m_availabilityCard = new MetricCard(QStringLiteral("网络可用率"), QStringLiteral("metricAvailability"), content);
    m_utilizationCard = new MetricCard(QStringLiteral("平均利用率"), QStringLiteral("metricUtilization"), content);
    m_revenueCard = new MetricCard(QStringLiteral("近 7 日营收"), QStringLiteral("metricRevenue"), content);
    m_revenueCard->setObjectName(QStringLiteral("revenueCard"));

    auto *metricsLayout = new QHBoxLayout;
    metricsLayout->setSpacing(12);
    metricsLayout->addWidget(m_pileTotalCard);
    metricsLayout->addWidget(m_availabilityCard);
    metricsLayout->addWidget(m_utilizationCard);
    metricsLayout->addWidget(m_revenueCard);

    // 需关注面板：计数 + 异常列表（故障/离线桩，与拓扑图/状态点同语义）
    auto *attentionPanel = new QFrame(content);
    attentionPanel->setProperty("panel", true);
    auto *attentionTitle = new QLabel(QStringLiteral("需关注"), attentionPanel);
    QFont attentionFont = attentionTitle->font();
    attentionFont.setPixelSize(13);
    attentionFont.setBold(true);
    attentionTitle->setFont(attentionFont);

    m_faultCountValue = new QLabel(attentionPanel);
    m_faultCountValue->setObjectName(QStringLiteral("metricFaultCount"));
    m_faultCountValue->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    QFont countFont = m_faultCountValue->font();
    countFont.setPixelSize(13);
    m_faultCountValue->setFont(countFont);

    m_attentionList = new QListWidget(attentionPanel);
    m_attentionList->setObjectName(QStringLiteral("attentionList"));
    m_attentionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_attentionList->setUniformItemSizes(true);

    auto *attentionHeader = new QHBoxLayout;
    attentionHeader->addWidget(attentionTitle);
    attentionHeader->addStretch();
    attentionHeader->addWidget(m_faultCountValue);

    auto *attentionLayout = new QVBoxLayout(attentionPanel);
    attentionLayout->setContentsMargins(14, 12, 14, 12);
    attentionLayout->setSpacing(8);
    attentionLayout->addLayout(attentionHeader);
    attentionLayout->addWidget(m_attentionList);

    // 站点态势面板：轻量拓扑图
    auto *topologyPanel = new QFrame(content);
    topologyPanel->setProperty("panel", true);
    auto *topologyTitle = new QLabel(QStringLiteral("站点态势"), topologyPanel);
    QFont topologyFont = topologyTitle->font();
    topologyFont.setPixelSize(13);
    topologyFont.setBold(true);
    topologyTitle->setFont(topologyFont);

    m_topology = new StationTopologyWidget(topologyPanel);
    m_topology->setObjectName(QStringLiteral("stationTopology"));

    auto *topologyLayout = new QVBoxLayout(topologyPanel);
    topologyLayout->setContentsMargins(14, 12, 14, 12);
    topologyLayout->setSpacing(8);
    topologyLayout->addWidget(topologyTitle);
    topologyLayout->addWidget(m_topology);

    auto *bodyLayout = new QHBoxLayout;
    bodyLayout->setSpacing(12);
    bodyLayout->addWidget(attentionPanel, 2);
    bodyLayout->addWidget(topologyPanel, 5);

    m_updatedLabel = new QLabel(content);
    m_updatedLabel->setObjectName(QStringLiteral("updatedLabel"));
    m_updatedLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    QFont updatedFont = m_updatedLabel->font();
    updatedFont.setPixelSize(11);
    m_updatedLabel->setFont(updatedFont);

    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    contentLayout->addLayout(metricsLayout);
    contentLayout->addLayout(bodyLayout, 1);
    contentLayout->addWidget(m_updatedLabel, 0, Qt::AlignRight);

    // ---- 四态壳：Loading / Empty / Error / Content ----
    m_stateStack = new StateStack(this);
    m_stateStack->setContentWidget(content);
    m_stateStack->setRetryHandler([this] { refresh(); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addLayout(topLayout);
    layout->addWidget(m_stateStack, 1);

    connect(m_modeCombo, &QComboBox::currentIndexChanged,
            this, &OverviewPage::onModeChanged);
    // 异常项激活：单击（鼠标）与 Enter/双击（键盘，itemActivated）同路径
    const auto activateAttention = [this](QListWidgetItem *item) {
        const QString code = item->data(Qt::UserRole).toString();
        if (!code.isEmpty())
            emit pileAttentionRequested(code);
    };
    connect(m_attentionList, &QListWidget::itemClicked, this, activateAttention);
    connect(m_attentionList, &QListWidget::itemActivated, this, activateAttention);
}

void OverviewPage::refresh()
{
    const int generation = ++m_loadGeneration;
    m_pendingFetches = 3;
    m_stateStack->showState(StateStack::State::Loading,
                            QStringLiteral("正在加载概览数据…"));

    // 三路并行（每个请求独立异步返回）；generation 防过期请求覆盖新结果。
    m_repository->fetchOverview(this,
        [this, generation](const ev::OverviewResult &result) {
            if (generation != m_loadGeneration)
                return;
            m_lastOverview = result;
            if (--m_pendingFetches == 0)
                onAllReady();
        });
    m_repository->fetchPiles(this,
        [this, generation](const ev::ListResult<ev::PileInfo> &result) {
            if (generation != m_loadGeneration)
                return;
            m_lastPiles = result;
            if (--m_pendingFetches == 0)
                onAllReady();
        });
    m_repository->fetchStations(this,
        [this, generation](const ev::ListResult<ev::StationInfo> &result) {
            if (generation != m_loadGeneration)
                return;
            m_lastStations = result;
            if (--m_pendingFetches == 0)
                onAllReady();
        });
}

void OverviewPage::onAllReady()
{
    if (!m_lastOverview.ok) {
        m_stateStack->showState(StateStack::State::Error,
                                QStringLiteral("%1（%2）")
                                    .arg(kSimulatedErrorDisplay, m_lastOverview.error));
        return;
    }
    if (!m_lastOverview.hasData) {
        // 空态由数据层显式声明（hasData=false），不从数值反推
        m_stateStack->showState(StateStack::State::Empty,
                                QStringLiteral("暂无概览数据"));
        return;
    }
    if (!m_lastPiles.ok || !m_lastStations.ok) {
        const QString detail = m_lastPiles.ok ? m_lastStations.error : m_lastPiles.error;
        m_stateStack->showState(StateStack::State::Error,
                                QStringLiteral("%1（%2）").arg(kSimulatedErrorDisplay, detail));
        return;
    }

    renderContent();
    m_stateStack->showState(StateStack::State::Content);
}

void OverviewPage::renderContent()
{
    const ev::OverviewStats &stats = m_lastOverview.stats;
    const QList<ev::PileInfo> &piles = m_lastPiles.items;

    // 指标卡（口径与 mockdataset 可追溯一致）
    m_pileTotalCard->setValue(QString::number(piles.size()));
    const double rate = ev::availabilityRate(piles);
    m_availabilityCard->setValue(QString::number(rate * 100.0, 'f', 1) + QStringLiteral("%"));
    m_utilizationCard->setValue(
        QString::number(stats.avgStationUtilization * 100.0, 'f', 0) + QStringLiteral("%"));
    m_revenueCard->setValue(formatYuanCents(stats.revenueCents));

    // 需关注列表：故障/离线桩（同一五态语义；"需关注"= 故障 + 离线）
    int attentionCount = 0;
    m_attentionList->clear();
    for (const auto &pile : piles) {
        if (pile.status != ev::PileStatus::Fault
            && pile.status != ev::PileStatus::Offline)
            continue;
        ++attentionCount;
        const QString stationName = attentionStationName(m_lastStations, pile.stationId);
        auto *item = new QListWidgetItem(
            stationName.isEmpty()
                ? QStringLiteral("%1 · %2").arg(pile.pileCode, attentionText(pile))
                : QStringLiteral("%1 · %2 · %3")
                      .arg(pile.pileCode, attentionText(pile), stationName),
            m_attentionList);
        item->setData(Qt::UserRole, pile.pileCode);
        // 状态色文本：故障珊瑚/离线灰（日班深色变体）
        item->setForeground(pile.status == ev::PileStatus::Fault
                                ? ev::theme::kDayFault
                                : ev::theme::kDayOffline);
    }
    m_faultCountValue->setText(QString::number(attentionCount));

    // 站点态势
    m_topology->setStations(m_lastStations.items);
    m_topology->setPiles(piles);

    m_updatedLabel->setText(
        QStringLiteral("数据更新（UTC）：%1").arg(stats.updatedAt));
}

ev::mockdata::DataMode OverviewPage::dataMode() const
{
    const int idx = m_modeCombo->currentIndex();
    if (idx < 0)
        return ev::mockdata::DataMode::Normal;
    return static_cast<ev::mockdata::DataMode>(m_modeCombo->itemData(idx).toInt());
}

void OverviewPage::onModeChanged(int)
{
    // 第一阶段演示：模式下拉驱动 Mock 数据层（setOverviewMode 为 Mock 特有接口，
    // 不在抽象层；9/6 Socket 接入后由数据层自动驱动，下拉移除）
    if (auto *mock = dynamic_cast<MockAdminRepository *>(m_repository))
        mock->setOverviewMode(dataMode());
    refresh();
}
