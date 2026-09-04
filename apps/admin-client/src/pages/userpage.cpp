#include "userpage.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include "data/mockadminrepository.h"
#include "theme/generated/theme_tokens.h"

namespace {

QString userStatusDisplay(const QString &status)
{
    if (status == QStringLiteral("active"))
        return QStringLiteral("正常");
    if (status == QStringLiteral("frozen"))
        return QStringLiteral("冻结");
    return status;
}

// 余额：整数分 → 元（与营收口径一致，千分位）
QString formatYuan(qint64 cents)
{
    return QStringLiteral("¥%1")
        .arg(QString::number(cents / 100.0, 'f', 2));
}

} // namespace

UserPage::UserPage(ev::AdminRepository *repository, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("userPage"));

    if (!repository) {
        m_ownedRepository = std::make_unique<MockAdminRepository>();
        repository = m_ownedRepository.get();
    }
    m_repository = repository;

    m_table = new QTableWidget(0, 4, this);
    m_table->setObjectName(QStringLiteral("userTable"));
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("手机号"), QStringLiteral("昵称"), QStringLiteral("余额"),
        QStringLiteral("状态"),
    });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(46);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("userPageHint"));
    m_hintLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    QFont hintFont = m_hintLabel->font();
    hintFont.setPixelSize(12);
    m_hintLabel->setFont(hintFont);
    // 管理操作未接入：只读列表 + 明确说明，不提供伪造的"冻结/解冻"假操作。
    // 该说明在正常数据态常驻（加载/空/错误等临时状态覆盖后恢复）

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_hintLabel);
}

void UserPage::refresh(ev::mockdata::DataMode mode)
{
    const int generation = ++m_loadGeneration;
    m_ok = false;
    showHint(QStringLiteral("正在加载用户数据…"));

    if (auto *mock = dynamic_cast<MockAdminRepository *>(m_repository))
        mock->setOverviewMode(mode);

    m_repository->fetchUsers(
        this, [this, generation](const ev::ListResult<ev::UserInfo> &result) {
            if (generation != m_loadGeneration)
                return;
            m_users = result.items;
            m_ok = result.ok;
            m_error = result.error;
            rebuildRows();
        });
}

void UserPage::rebuildRows()
{
    if (!m_ok) {
        m_table->setRowCount(0);
        showHint(QStringLiteral("接口错误：%1").arg(m_error));
        return;
    }

    m_table->setRowCount(m_users.size());
    for (int row = 0; row < m_users.size(); ++row) {
        const ev::UserInfo &user = m_users.at(row);
        m_table->setItem(row, 0, new QTableWidgetItem(user.phone));
        m_table->setItem(row, 1, new QTableWidgetItem(user.nickname));
        m_table->setItem(row, 2,
                         new QTableWidgetItem(formatYuan(user.balanceCents)));
        m_table->setItem(row, 3,
                         new QTableWidgetItem(userStatusDisplay(user.status)));
    }

    if (m_users.isEmpty())
        showHint(QStringLiteral("暂无用户数据"));
    else
        clearHint();
}

int UserPage::visibleRowCount() const
{
    return m_table->rowCount();
}

void UserPage::showHint(const QString &text)
{
    m_hintLabel->setText(text);
    m_hintLabel->setVisible(true);
}

void UserPage::clearHint()
{
    // 正常数据态：恢复"只读演示"常驻说明（区别于桩/站页的纯临时提示）
    m_hintLabel->setText(
        QStringLiteral("只读演示数据 · 冻结/解冻等管理操作待服务端接入后开放"));
    m_hintLabel->setVisible(true);
}
