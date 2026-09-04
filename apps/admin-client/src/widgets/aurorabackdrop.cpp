#include "aurorabackdrop.h"

#include <QPainter>
#include <QVariantAnimation>

#include "theme/generated/theme_tokens.h"
#include "theme/theme.h"

namespace {

// 呼吸透明度范围（日班浅底：青蓝光晕只作氛围，峰值远低于 Web 夜班极光，
// 且只出现在面板间隙/页面边缘，不叠在 surface/文字上）
constexpr double kMinGlowAlpha = 0.05;
constexpr double kMaxGlowAlpha = 0.16;
// 减少动态效果时的静止亮度（介于呼吸范围中值，保留氛围不突兀）
constexpr double kStaticGlowAlpha = 0.10;

// Web 极光关键帧比例（spec §5.6）：0 暗 → 45 亮 → 90 回到与 0 完全一致 → 100 保持
constexpr qreal kKeyBright = 0.45;
constexpr qreal kKeyReturn = 0.90;

} // namespace

AuroraBackdrop::AuroraBackdrop(QWidget *parent)
    : QWidget(parent)
    , m_motionEnabled(ev::Theme::motionEnabled())
{
    setObjectName(QStringLiteral("auroraBackdrop"));
    setAttribute(Qt::WA_OpaquePaintEvent, true); // 全区域自绘（背景色+光斑）

    m_animation = new QVariantAnimation(this);
    // 周期与 Web 极光同源：design-tokens.json motion.aurora = 11000ms
    m_animation->setDuration(ev::theme::kMotionAuroraMs);
    m_animation->setLoopCount(-1);
    // 闭合循环，终点与起点同值：不使用 alternate，循环边界状态一致
    m_animation->setKeyValues({
        {0.0, kMinGlowAlpha},
        {kKeyBright, kMaxGlowAlpha},
        {kKeyReturn, kMinGlowAlpha},
        {1.0, kMinGlowAlpha},
    });
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_glowAlpha = value.toReal();
        update();
    });
    m_glowAlpha = kStaticGlowAlpha;
    updateAnimation();
}

void AuroraBackdrop::setMotionEnabled(bool enabled)
{
    if (m_motionEnabled == enabled)
        return;
    m_motionEnabled = enabled;
    updateAnimation();
}

bool AuroraBackdrop::isAnimationRunning() const
{
    return m_animation->state() == QAbstractAnimation::Running;
}

void AuroraBackdrop::updateAnimation()
{
    if (m_motionEnabled) {
        m_animation->start();
    } else {
        m_animation->stop();
        m_glowAlpha = kStaticGlowAlpha;
        update();
    }
}

void AuroraBackdrop::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 日班背景色（与全局 QSS 同令牌；页面根已透明，由本层统一提供底色）
    painter.fillRect(rect(), ev::theme::kDayBackground);

    const qreal w = width();
    const qreal h = height();
    const qreal base = qMin(w, h);

    // 光斑 1：聚焦蓝（青蓝），左上偏中 —— 主氛围光
    {
        const QPointF center(w * 0.28, h * 0.30);
        const qreal radius = base * 0.75;
        QColor glow = ev::theme::kDayCharging;
        glow.setAlphaF(m_glowAlpha);
        QRadialGradient gradient(center, radius);
        gradient.setColorAt(0.0, glow);
        glow.setAlphaF(0.0);
        gradient.setColorAt(1.0, glow);
        painter.fillRect(rect(), gradient);
    }

    // 光斑 2：能源绿（青绿），右下 —— 次级氛围光，亮度减半保持克制
    {
        const QPointF center(w * 0.80, h * 0.86);
        const qreal radius = base * 0.55;
        QColor glow = ev::theme::kDayIdle;
        glow.setAlphaF(m_glowAlpha * 0.5);
        QRadialGradient gradient(center, radius);
        gradient.setColorAt(0.0, glow);
        glow.setAlphaF(0.0);
        gradient.setColorAt(1.0, glow);
        painter.fillRect(rect(), gradient);
    }
}
