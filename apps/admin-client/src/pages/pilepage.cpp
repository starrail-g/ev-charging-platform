#include "pilepage.h"

#include <QComboBox>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include "data/mockadminrepository.h"
#include "theme/generated/theme_tokens.h"
#include "widgets/statustag.h"

namespace {

QString pileTypeDisplay(const QString &type)
{
    return type == QStringLiteral("fast") ? QStringLiteral("快充") : QStringLiteral("慢充");
}

QString statusFilterText(const QString &filter)
{
    if (filter == QStringLiteral("all"))
        return QStringLiteral("全部");
    if (filter == QStringLiteral("attention"))
        return QStringLiteral("需关注");
    if (filter == QStringLiteral("idle"))
        return QStringLiteral("空闲");
    if (filter == QStringLiteral("reserved"))
        return QStringLiteral("已预约");
    if (filter == QStringLiteral("charging"))
        return QStringLiteral("充电中");
    if (filter == QStringLiteral("fault"))
        return QStringLiteral("故障");
    if (filter == QStringLiteral("offline"))
        return QStringLiteral("离线");
    return QStringLiteral("全部");
}

} // namespace

PilePage::PilePage(ev::AdminRepository *repository, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("pilePage"));

    if (!repository) {
        m_ownedRepository = std::make_unique<MockAdminRepository>();
        repository = m_ownedRepository.get();
    }
    m_repository = repository;

    // ---- 工具条：状态筛选 ----
    auto *filterLabel = new QLabel(QStringLiteral("状态筛选"), this);
    m_filterCombo = new QComboBox(this);
    m_filterCombo->setObjectName(QStringLiteral("pileFilterCombo"));
    const QStringList filters = {
        QStringLiteral("all"),   QStringLiteral("idle"),     QStringLiteral("reserved"),
        QStringLiteral("charging"), QStringLiteral("fault"), QStringLiteral("offline"),
        QStringLiteral("attention"),
    };
    for (const QString &filter : filters)
        m_filterCombo->addItem(statusFilterText(filter), filter);

    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(filterLabel);
    toolbar->addWidget(m_filterCombo);
    toolbar->addStretch();

    // ---- 桩列表（A-04：编号/站点/类型/功率/单价/状态/累计次数/累计时长）----
    m_table = new QTableWidget(0, 8, this);
    m_table->setObjectName(QStringLiteral("pileTable"));
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("桩编号"), QStringLiteral("站点"), QStringLiteral("类型"),
        QStringLiteral("额定功率"), QStringLiteral("单价"), QStringLiteral("状态"),
        QStringLiteral("累计次数"), QStringLiteral("累计时长"),
    });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(46);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // ---- 底部提示行（加载/空/错误/未找到，非阻塞）----
    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("pilePageHint"));
    m_hintLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    QFont hintFont = m_hintLabel->font();
    hintFont.setPixelSize(12);
    m_hintLabel->setFont(hintFont);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addLayout(toolbar);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_hintLabel);

    connect(m_filterCombo, &QComboBox::currentIndexChanged,
            this, &PilePage::onFilterIndexChanged);
}

void PilePage::refresh(ev::mockdata::DataMode mode)
{
    const int generation = ++m_loadGeneration;
    m_pendingFetches = 2;
    m_pilesOk = false;
    m_stationsOk = false;
    showHint(QStringLiteral("正在加载充电桩数据…"));

    // 第一阶段演示：模式只驱动 Mock 特有层（不在抽象接口）
    if (auto *mock = dynamic_cast<MockAdminRepository *>(m_repository))
        mock->setOverviewMode(mode);

    m_repository->fetchPiles(
        this, [this, generation](const ev::ListResult<ev::PileInfo> &result) {
            if (generation != m_loadGeneration)
                return;
            m_piles = result.items;
            m_pilesOk = result.ok;
            m_pilesError = result.error;
            if (--m_pendingFetches == 0)
                rebuildRows();
        });
    m_repository->fetchStations(
        this, [this, generation](const ev::ListResult<ev::StationInfo> &result) {
            if (generation != m_loadGeneration)
                return;
            m_stationNames.clear();
            if (result.ok) {
                for (const auto &station : result.items)
                    m_stationNames.insert(station.id, station.name);
            }
            m_stationsOk = result.ok;
            m_stationsError = result.error;
            if (--m_pendingFetches == 0)
                rebuildRows();
        });
}

void PilePage::rebuildRows()
{
    if (!m_pilesOk || !m_stationsOk) {
        const QString detail = m_pilesOk ? m_stationsError : m_pilesError;
        m_table->setRowCount(0);
        showHint(QStringLiteral("接口错误：%1").arg(detail));
        return;
    }

    m_table->setRowCount(m_piles.size());
    for (int row = 0; row < m_piles.size(); ++row) {
        const ev::PileInfo &pile = m_piles.at(row);
        const QString stationName = m_stationNames.value(pile.stationId);

        auto *codeItem = new QTableWidgetItem(pile.pileCode);
        auto *stationItem = new QTableWidgetItem(stationName.isEmpty()
                                                     ? QStringLiteral("—")
                                                     : stationName);
        auto *typeItem = new QTableWidgetItem(pileTypeDisplay(pile.pileType));
        auto *powerItem = new QTableWidgetItem(
            QStringLiteral("%1 kW").arg(pile.powerKw, 0, 'f', 0));
        auto *priceItem = new QTableWidgetItem(
            ev::formatYuanCents(pile.unitPriceCentsPerKwh) + QStringLiteral("/kWh"));

        auto *tag = new StatusTag(pile.status, m_table);

        auto *countItem = new QTableWidgetItem(QString::number(pile.totalChargeCount));
        auto *durationItem = new QTableWidgetItem(
            ev::formatChargeDuration(pile.totalChargeSeconds));
        // 数字列右对齐，与金额/功率列同风格（视觉可扫描）
        countItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        durationItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_table->setItem(row, 0, codeItem);
        m_table->setItem(row, 1, stationItem);
        m_table->setItem(row, 2, typeItem);
        m_table->setItem(row, 3, powerItem);
        m_table->setItem(row, 4, priceItem);
        m_table->setCellWidget(row, 5, tag);
        m_table->setItem(row, 6, countItem);
        m_table->setItem(row, 7, durationItem);
    }

    if (m_piles.isEmpty()) {
        showHint(QStringLiteral("暂无充电桩数据"));
    } else {
        clearHint();
    }
    applyFilter();

    // 数据到齐后执行挂起的定位请求（MainWindow 联动可能在数据就绪前到达）
    if (!m_pendingFocus.isEmpty()) {
        const QString pending = m_pendingFocus;
        m_pendingFocus.clear();
        focusPile(pending);
    }
}

void PilePage::setStatusFilter(const QString &filter)
{
    if (m_statusFilter == filter)
        return;
    m_statusFilter = filter;
    const int index = m_filterCombo->findData(filter);
    if (index >= 0)
        m_filterCombo->setCurrentIndex(index); // 触发 onFilterIndexChanged -> applyFilter
    else
        applyFilter();
}

void PilePage::onFilterIndexChanged(int index)
{
    m_statusFilter = m_filterCombo->itemData(index).toString();
    applyFilter();
}

void PilePage::applyFilter()
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        bool visible = true;
        if (m_statusFilter != QStringLiteral("all")) {
            if (row < m_piles.size()) {
                const ev::PileStatus status = m_piles.at(row).status;
                const bool isAttention = status == ev::PileStatus::Fault
                    || status == ev::PileStatus::Offline;
                const QString protocol = ev::pileStatusToProtocol(status);
                if (m_statusFilter == QStringLiteral("attention"))
                    visible = isAttention;
                else
                    visible = (protocol == m_statusFilter);
            }
        }
        m_table->setRowHidden(row, !visible);
    }
}

int PilePage::visibleRowCount() const
{
    int count = 0;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (!m_table->isRowHidden(row))
            ++count;
    }
    return count;
}

void PilePage::focusPile(const QString &pileCode)
{
    // 数据未到齐（首次进入/刷新中）：挂起请求，rebuildRows 后自动定位
    if (m_piles.isEmpty() && !m_pilesOk) {
        m_pendingFocus = pileCode;
        return;
    }
    m_pendingFocus.clear();

    // 清除其他筛选，恢复全量视图后定位（计划语义）
    setStatusFilter(QStringLiteral("all"));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *codeItem = m_table->item(row, 0);
        if (!codeItem || codeItem->text() != pileCode)
            continue;
        m_table->selectRow(row);
        m_table->scrollToItem(codeItem, QAbstractItemView::PositionAtCenter);
        clearHint();
        return;
    }
    // 未找到：不改变当前选择，仅非阻塞提示
    showHint(QStringLiteral("未找到充电桩 %1").arg(pileCode));
}

QString PilePage::currentPileCode() const
{
    const int row = m_table->currentRow();
    if (row < 0)
        return QString();
    QTableWidgetItem *codeItem = m_table->item(row, 0);
    return codeItem ? codeItem->text() : QString();
}

void PilePage::showHint(const QString &text)
{
    m_hintLabel->setText(text);
    m_hintLabel->setVisible(true);
}

void PilePage::clearHint()
{
    m_hintLabel->clear();
    m_hintLabel->setVisible(false);
}
