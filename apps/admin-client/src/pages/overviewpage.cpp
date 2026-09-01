#include "overviewpage.h"

#include <QLabel>
#include <QVBoxLayout>

OverviewPage::OverviewPage(QWidget *parent)
    : QWidget(parent)
{
    auto *title = new QLabel(QStringLiteral("概览"), this);
    auto *hint = new QLabel(QStringLiteral("营收摘要、桩状态摘要、站点利用率、更新时间（待实现）"), this);
    hint->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addStretch();
}
