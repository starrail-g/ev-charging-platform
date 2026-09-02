#include "overviewpage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "widgets/statestack.h"

namespace {
const QString kSimulatedErrorDisplay = QStringLiteral("接口错误：模拟数据层失败");
}

OverviewPage::OverviewPage(QWidget *parent)
    : QWidget(parent)
{
    auto *title = new QLabel(QStringLiteral("概览"), this);

    // 演示模式切换（第一阶段；9/6 后由数据层自动驱动，下拉保留供手工验证）
    m_modeCombo = new QComboBox(this);
    m_modeCombo->setObjectName(QStringLiteral("dataModeCombo"));
    m_modeCombo->addItem(QStringLiteral("数据模式：正常"), int(ev::mockdata::DataMode::Normal));
    m_modeCombo->addItem(QStringLiteral("数据模式：空"), int(ev::mockdata::DataMode::Empty));
    m_modeCombo->addItem(QStringLiteral("数据模式：错误"), int(ev::mockdata::DataMode::Error));

    auto *headerLayout = new QHBoxLayout;
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(m_modeCombo);

    m_stateStack = new StateStack(this);
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setTextFormat(Qt::PlainText);

    auto *content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->addWidget(m_summaryLabel);
    contentLayout->addStretch();
    m_stateStack->setContentWidget(content);

    // 错误态重试入口：重新加载当前模式
    m_stateStack->setRetryHandler([this] { refresh(); });

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(headerLayout);
    layout->addWidget(m_stateStack);

    connect(m_modeCombo, &QComboBox::currentIndexChanged,
            this, &OverviewPage::onModeChanged);
}

void OverviewPage::refresh()
{
    const auto mode = dataMode();

    m_stateStack->showState(StateStack::State::Loading,
                            QStringLiteral("正在加载概览数据…"));

    const ev::mockdata::OverviewResult result = ev::mockdata::overview(mode);

    if (!result.ok) {
        // 错误态：展示错误文案 + 重试按钮（StateStack 已绑定 refresh）
        m_stateStack->showState(StateStack::State::Error,
                                QStringLiteral("%1（%2）").arg(kSimulatedErrorDisplay, result.error));
        return;
    }

    if (!result.hasData) {
        // 空态由数据层显式声明（hasData=false），不从数值反推——
        // 新站点/当日无营收时"全零"可能是有效数据（P2 review 修复）
        m_stateStack->showState(StateStack::State::Empty,
                                QStringLiteral("暂无概览数据"));
        return;
    }

    const ev::OverviewStats &s = result.stats;
    m_summaryLabel->setText(
        QStringLiteral("近 7 日营收：%1 元\n")
            .arg(s.revenueCents / 100.0, 0, 'f', 2)
        + QStringLiteral("桩状态：空闲 %1 · 已预约 %2 · 充电中 %3 · 故障 %4 · 离线 %5\n")
              .arg(s.pileIdle).arg(s.pileReserved).arg(s.pileCharging)
              .arg(s.pileFault).arg(s.pileOffline)
        + QStringLiteral("站点平均利用率：%1%\n").arg(s.avgStationUtilization * 100.0, 0, 'f', 0)
        + QStringLiteral("更新时间（UTC）：%1").arg(s.updatedAt));

    m_stateStack->showState(StateStack::State::Content);
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
    refresh();
}
