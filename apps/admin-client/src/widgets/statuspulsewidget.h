#pragma once

#include <QVariantAnimation>
#include <QWidget>

#include "models/adminmodels.h"

class StatusPulseWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StatusPulseWidget(ev::PileStatus state, QWidget *parent = nullptr);

    void setState(ev::PileStatus state);
    void setMotionEnabled(bool enabled);
    bool isAnimationRunning() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool shouldAnimate() const;
    void updateAnimation();

    ev::PileStatus m_state;
    bool m_motionEnabled = true;
    qreal m_opacity = 1.0;
    QVariantAnimation m_animation;
};
