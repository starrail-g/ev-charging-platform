#include "statuspulsewidget.h"

#include <QPainter>

#include "theme/generated/theme_tokens.h"
#include "theme/theme.h"

namespace {

QColor colorForState(ev::PileStatus state)
{
    switch (state) {
    case ev::PileStatus::Idle:
        return ev::theme::kDayIdle;
    case ev::PileStatus::Reserved:
        return ev::theme::kDayReserved;
    case ev::PileStatus::Charging:
        return ev::theme::kDayCharging;
    case ev::PileStatus::Fault:
        return ev::theme::kDayFault;
    case ev::PileStatus::Offline:
        return ev::theme::kDayOffline;
    case ev::PileStatus::Unknown:
        return ev::theme::kDayUnknown;
    }
    return ev::theme::kDayUnknown;
}

} // namespace

StatusPulseWidget::StatusPulseWidget(ev::PileStatus state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_motionEnabled(ev::Theme::motionEnabled())
{
    setFixedSize(16, 16);
    setAccessibleName(QStringLiteral("充电桩状态指示：%1").arg(ev::pileStatusToDisplay(state)));
    m_animation.setStartValue(0.45);
    m_animation.setEndValue(1.0);
    connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_opacity = value.toReal();
        update();
    });
    connect(&m_animation, &QVariantAnimation::finished, this, [this] {
        if (!shouldAnimate())
            return;
        m_animation.setDirection(
            m_animation.direction() == QAbstractAnimation::Forward
                ? QAbstractAnimation::Backward
                : QAbstractAnimation::Forward);
        m_animation.start();
    });
    updateAnimation();
}

void StatusPulseWidget::setState(ev::PileStatus state)
{
    if (m_state == state)
        return;
    m_state = state;
    setAccessibleName(QStringLiteral("充电桩状态指示：%1").arg(ev::pileStatusToDisplay(state)));
    updateAnimation();
    update();
}

void StatusPulseWidget::setMotionEnabled(bool enabled)
{
    if (m_motionEnabled == enabled)
        return;
    m_motionEnabled = enabled;
    updateAnimation();
}

bool StatusPulseWidget::isAnimationRunning() const
{
    return m_animation.state() == QAbstractAnimation::Running;
}

void StatusPulseWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF center(width() / 2.0, height() / 2.0);
    QColor color = colorForState(m_state);
    QColor halo = color;
    halo.setAlphaF(0.16 * m_opacity);
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    painter.drawEllipse(center, 6.0, 6.0);
    color.setAlphaF(m_opacity);
    painter.setBrush(color);
    painter.drawEllipse(center, 4.0, 4.0);
}

bool StatusPulseWidget::shouldAnimate() const
{
    return m_motionEnabled
        && (m_state == ev::PileStatus::Charging || m_state == ev::PileStatus::Fault);
}

void StatusPulseWidget::updateAnimation()
{
    m_animation.stop();
    m_opacity = 1.0;
    if (!shouldAnimate()) {
        update();
        return;
    }
    m_animation.setDuration(
        m_state == ev::PileStatus::Charging
            ? ev::theme::kMotionChargingPulseMs
            : ev::theme::kMotionFaultPulseMs);
    m_animation.setDirection(QAbstractAnimation::Forward);
    m_animation.start();
}
