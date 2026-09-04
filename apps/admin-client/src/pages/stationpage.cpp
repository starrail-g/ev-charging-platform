#include "stationpage.h"

#include <QLabel>
#include <QVBoxLayout>

StationPage::StationPage(QWidget *parent)
    : QWidget(parent)
{
    auto *title = new QLabel(QStringLiteral("充电站管理"), this);
    auto *hint = new QLabel(QStringLiteral("站点查询和管理（待实现）"), this);
    hint->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addStretch();
}
