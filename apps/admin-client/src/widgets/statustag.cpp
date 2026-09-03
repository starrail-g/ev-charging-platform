#include "statustag.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>

StatusTag::StatusTag(ev::PileStatus state, QWidget *parent)
    : QWidget(parent)
    , m_glyph(new StatusGlyphWidget(state, this))
    , m_label(new QLabel(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    layout->addWidget(m_glyph);
    layout->addWidget(m_label);
    // 图形与文字都不单独朗读：完整语义由外层标签提供（见 setState）
    m_glyph->setAccessibleName(QString());
    m_label->setAccessibleName(QString());
    setState(state);
}

void StatusTag::setState(ev::PileStatus state)
{
    m_state = state;
    const QString protocolState = ev::pileStatusToProtocol(state);
    const QString displayState = ev::pileStatusToDisplay(state);

    m_glyph->setState(state);
    m_label->setText(displayState);
    // QSS 颜色选择器（theme.template.qss: QLabel[state="fault"]…）依赖 QLabel 的动态属性
    m_label->setProperty("state", protocolState);
    // 外层同时暴露 state 动态属性，供容器级样式与测试读取
    setProperty("state", protocolState);
    setAccessibleName(QStringLiteral("充电桩状态：%1").arg(displayState));

    style()->unpolish(m_label);
    style()->polish(m_label);
}

QString StatusTag::text() const
{
    return m_label ? m_label->text() : QString();
}

StatusGlyphWidget::GlyphKind StatusTag::glyphKind() const
{
    return m_glyph ? m_glyph->glyphKind() : StatusGlyphWidget::GlyphKind::QuestionDiamond;
}
