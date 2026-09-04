#include "metriccard.h"

#include <QLabel>
#include <QVBoxLayout>

#include "theme/generated/theme_tokens.h"

MetricCard::MetricCard(const QString &label, const QString &valueObjectName,
                       QWidget *parent)
    : QFrame(parent)
{
    // QSS 已提供 QFrame[panel="true"] 面板样式（surface + 细边界 + 圆角）
    setProperty("panel", true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // 颜色一律取自生成令牌（design-tokens.json → theme_tokens.h），
    // 不在组件里手写十六进制，避免与 QSS 令牌漂移。
    auto *labelWidget = new QLabel(label, this);
    labelWidget->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    QFont labelFont = labelWidget->font();
    labelFont.setPixelSize(12);
    labelWidget->setFont(labelFont);

    m_value = new QLabel(this);
    m_value->setObjectName(valueObjectName);
    m_value->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayText.name()));
    QFont valueFont = m_value->font();
    valueFont.setPixelSize(28);
    valueFont.setBold(true);
    m_value->setFont(valueFont);
    m_value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // 无障碍：数值标签以指标名作为可访问名称
    m_value->setAccessibleName(label);

    m_hint = new QLabel(this);
    m_hint->setStyleSheet(
        QStringLiteral("color: %1;").arg(ev::theme::kDayMutedText.name()));
    QFont hintFont = m_hint->font();
    hintFont.setPixelSize(11);
    m_hint->setFont(hintFont);
    m_hint->setVisible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(6);
    layout->addWidget(labelWidget);
    layout->addWidget(m_value);
    layout->addWidget(m_hint);
}

void MetricCard::setValue(const QString &value)
{
    m_value->setText(value);
}

void MetricCard::setHint(const QString &hint)
{
    m_hint->setText(hint);
    m_hint->setVisible(!hint.isEmpty());
}
