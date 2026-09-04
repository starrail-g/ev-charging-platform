#include "userpage.h"

#include <QLabel>
#include <QVBoxLayout>

UserPage::UserPage(QWidget *parent)
    : QWidget(parent)
{
    auto *title = new QLabel(QStringLiteral("用户管理"), this);
    auto *hint = new QLabel(QStringLiteral("用户查询、冻结/解冻（待实现）"), this);
    hint->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addStretch();
}
