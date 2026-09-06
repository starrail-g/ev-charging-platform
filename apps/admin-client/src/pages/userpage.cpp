#include "userpage.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
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

    // 用户列表（A-07：手机号/昵称/余额/状态/注册时间(UTC)）
    m_table = new QTableWidget(0, 5, this);
    m_table->setObjectName(QStringLiteral("userTable"));
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("手机号"), QStringLiteral("昵称"), QStringLiteral("余额"),
        QStringLiteral("状态"), QStringLiteral("注册时间 (UTC)"),
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

    // C-S1-007：冻结/解冻入口（第一阶段 Mock 模拟；无选中行时不可用，
    // 文案随选中用户状态切换：正常 → "冻结选中用户"，冻结 → "解冻选中用户"）
    m_statusButton = new QPushButton(QStringLiteral("冻结选中用户"), this);
    m_statusButton->setObjectName(QStringLiteral("userStatusButton"));
    m_statusButton->setEnabled(false);

    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(m_statusButton);
    toolbar->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addLayout(toolbar);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_hintLabel);

    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &current, const QModelIndex &) {
                onUserSelectionChanged(current.row());
            });
    connect(m_statusButton, &QPushButton::clicked,
            this, &UserPage::onStatusButtonClicked);
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
        m_statusButton->setEnabled(false);
        m_actionPendingHint.clear();
        showHint(QStringLiteral("接口错误：%1").arg(m_error));
        return;
    }

    m_table->setRowCount(m_users.size());
    for (int row = 0; row < m_users.size(); ++row) {
        const ev::UserInfo &user = m_users.at(row);
        m_table->setItem(row, 0, new QTableWidgetItem(user.phone));
        m_table->setItem(row, 1, new QTableWidgetItem(user.nickname));
        m_table->setItem(row, 2,
                         new QTableWidgetItem(ev::formatYuanCents(user.balanceCents)));
        m_table->setItem(row, 3,
                         new QTableWidgetItem(userStatusDisplay(user.status)));
        // 注册时间与概览页更新时间同风格：UTC ISO-8601 原串，可追溯不歧义
        m_table->setItem(row, 4, new QTableWidgetItem(user.createdAt));
    }

    if (m_users.isEmpty()) {
        showHint(QStringLiteral("暂无用户数据"));
    } else if (!m_actionPendingHint.isEmpty()) {
        // 动作成功提示展示一次（如"用户 138…已冻结（模拟）"），随后清除
        showHint(m_actionPendingHint);
        m_actionPendingHint.clear();
    } else {
        clearHint();
    }

    // 表格重建后 currentRow 可能保留（selectRow 同值不触发 currentRowChanged）：
    // 不盲目禁用，按当前选中直接恢复按钮态与文案（数据已是刷新后的新状态）
    onUserSelectionChanged(m_table->currentRow());
}

int UserPage::visibleRowCount() const
{
    return m_table->rowCount();
}

void UserPage::onUserSelectionChanged(int currentRow)
{
    const bool hasSelection = currentRow >= 0 && currentRow < m_users.size();
    m_statusButton->setEnabled(hasSelection);
    if (!hasSelection)
        return;
    // 文案随状态切换：正常 → 冻结；冻结 → 解冻（动作目标 = 当前状态的翻转）
    const bool frozen = m_users.at(currentRow).status == QStringLiteral("frozen");
    m_statusButton->setText(frozen ? QStringLiteral("解冻选中用户")
                                   : QStringLiteral("冻结选中用户"));
}

void UserPage::onStatusButtonClicked()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_users.size())
        return;

    const ev::UserInfo &user = m_users.at(row);
    const QString target = user.status == QStringLiteral("frozen")
        ? QStringLiteral("active")
        : QStringLiteral("frozen");

    // 动作在途：禁用按钮防连点；结果经回调恢复（成功 → 提示 + 重新拉取，
    // 失败/冲突 → 数据层 message 直接展示，列表保持现状可重试）
    m_statusButton->setEnabled(false);
    m_repository->setUserStatus(user.id, target, this,
                                [this](const ev::ActionResult &result) {
                                    if (result.ok) {
                                        m_actionPendingHint = result.message;
                                        refresh();
                                    } else {
                                        m_statusButton->setEnabled(true);
                                        showHint(result.message);
                                    }
                                });
}

void UserPage::showHint(const QString &text)
{
    m_hintLabel->setText(text);
    m_hintLabel->setVisible(true);
}

void UserPage::clearHint()
{
    // 正常数据态：常驻说明（区别于桩/站页的纯临时提示）——
    // 明确模拟边界：冻结/解冻为本地模拟操作，不冒充真实服务端
    m_hintLabel->setText(QStringLiteral(
        "演示数据 · 冻结/解冻为模拟操作（9/7 接口闸门后接入真实服务端）"));
    m_hintLabel->setVisible(true);
}
