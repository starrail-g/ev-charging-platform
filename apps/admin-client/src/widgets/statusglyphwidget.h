#pragma once

#include <QWidget>

#include "models/adminmodels.h"

// 桩状态图形编码（spec §8.1：颜色/文字/图形三编码并存，状态不依赖单一通道）。
// 自绘 16×16 简单几何：对勾圆 / 虚线时钟 / 闪电 / 警告三角 / 断链圆 / 问号菱形。
// 图形本身不承载可朗读文字（朗读交给组合它的 StatusTag 外层标签），
// 只作为视觉第二编码，避免与文字标签重复播报。
class StatusGlyphWidget : public QWidget
{
    Q_OBJECT

public:
    enum class GlyphKind {
        CheckCircle,     // Idle
        ClockDashed,     // Reserved
        BoltDot,         // Charging
        WarningTriangle, // Fault
        LinkOff,         // Offline
        QuestionDiamond, // Unknown
    };

    explicit StatusGlyphWidget(ev::PileStatus state, QWidget *parent = nullptr);

    void setState(ev::PileStatus state);
    ev::PileStatus state() const { return m_state; }
    GlyphKind glyphKind() const { return glyphKindFor(m_state); }

    static GlyphKind glyphKindFor(ev::PileStatus state);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ev::PileStatus m_state = ev::PileStatus::Idle;
};
