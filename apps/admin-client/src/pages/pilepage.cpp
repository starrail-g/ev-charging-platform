#include "pilepage.h"

#include <QLabel>
#include <QVBoxLayout>

PilePage::PilePage(QWidget *parent)
    : QWidget(parent)
{
    auto *title = new QLabel(QStringLiteral("充电桩管理"), this);
    auto *hint = new QLabel(QStringLiteral("桩列表、状态筛选、刷新、重启模拟（待实现）"), this);
    hint->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addStretch();
}
