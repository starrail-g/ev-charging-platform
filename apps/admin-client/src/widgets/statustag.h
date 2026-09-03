#pragma once

#include <QWidget>

#include "models/adminmodels.h"
#include "widgets/statusglyphwidget.h"

class QHBoxLayout;
class QLabel;

// 桩状态标签：图形编码（StatusGlyphWidget）+ 文字编码（QLabel，颜色由
// theme QSS 的 QLabel[state=...] 驱动）组合，spec §8.1 三通道不依赖单一颜色。
// 对外保持原 QLabel 时代的稳定接口：text() / state() / setState() / glyphKind()。
// 完整无障碍名称放在外层标签（“充电桩状态：故障”），内层图形与文字不再重复朗读。
class StatusTag : public QWidget
{
    Q_OBJECT

public:
    explicit StatusTag(ev::PileStatus state, QWidget *parent = nullptr);

    void setState(ev::PileStatus state);

    QString text() const;
    ev::PileStatus state() const { return m_state; }
    StatusGlyphWidget::GlyphKind glyphKind() const;

private:
    ev::PileStatus m_state = ev::PileStatus::Idle;
    StatusGlyphWidget *m_glyph = nullptr;
    QLabel *m_label = nullptr;
};
