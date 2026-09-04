#include "stationpage.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include "data/mockadminrepository.h"
#include "theme/generated/theme_tokens.h"

namespace {

QString stationStatusDisplay(const QString &status)
{
    // 协议语义：active=运营中，inactive=已停运（其余状态原样展示，不臆造）
    if (status == QStringLiteral("active"))
        return QStringLiteral("运营中");
    if (status == QStringLiteral("inactive"))
        return QStringLiteral("已停运");
    return status;
}

} // namespace

StationPage::StationPage(ev::AdminRepository *repository, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("stationPage"));

    if (!repository) {
        m_ownedRepository = std::make_unique<MockAdminRepository>();
        repository = m_ownedRepository.get();
    }
    m_repository = repository;

    m_table = new QTableWidget(0, 4, this);
    m_table->setObjectName(QStringLiteral("stationTable"));
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("名称"), QStringLiteral("地址"), QStringLiteral("桩数"),
        QStringLiteral("运行状态"),
    });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(46);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("stationPageHint"));
    m_hintLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    QFont hintFont = m_hintLabel->font();
    hintFont.setPixelSize(12);
    m_hintLabel->setFont(hintFont);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_hintLabel);
}

void StationPage::refresh(ev::mockdata::DataMode mode)
{
    const int generation = ++m_loadGeneration;
    m_ok = false;
    showHint(QStringLiteral("正在加载站点数据…"));

    if (auto *mock = dynamic_cast<MockAdminRepository *>(m_repository))
        mock->setOverviewMode(mode);

    m_repository->fetchStations(
        this, [this, generation](const ev::ListResult<ev::StationInfo> &result) {
            if (generation != m_loadGeneration)
                return;
            m_stations = result.items;
            m_ok = result.ok;
            m_error = result.error;
            rebuildRows();
        });
}

void StationPage::rebuildRows()
{
    if (!m_ok) {
        m_table->setRowCount(0);
        showHint(QStringLiteral("接口错误：%1").arg(m_error));
        return;
    }

    m_table->setRowCount(m_stations.size());
    for (int row = 0; row < m_stations.size(); ++row) {
        const ev::StationInfo &station = m_stations.at(row);
        m_table->setItem(row, 0, new QTableWidgetItem(station.name));
        m_table->setItem(row, 1, new QTableWidgetItem(station.address));
        m_table->setItem(row, 2,
                         new QTableWidgetItem(QString::number(station.pileCount)));
        m_table->setItem(row, 3,
                         new QTableWidgetItem(stationStatusDisplay(station.status)));
    }

    if (m_stations.isEmpty())
        showHint(QStringLiteral("暂无站点数据"));
    else
        clearHint();
}

int StationPage::visibleRowCount() const
{
    return m_table->rowCount();
}

void StationPage::showHint(const QString &text)
{
    m_hintLabel->setText(text);
    m_hintLabel->setVisible(true);
}

void StationPage::clearHint()
{
    m_hintLabel->clear();
    m_hintLabel->setVisible(false);
}
