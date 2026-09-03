#include "statusglyphwidget.h"

#include <QPainter>
#include <QPainterPath>

#include "theme/generated/theme_tokens.h"

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

StatusGlyphWidget::StatusGlyphWidget(ev::PileStatus state, QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(16, 16);
    setState(state);
}

void StatusGlyphWidget::setState(ev::PileStatus state)
{
    m_state = state;
    update();
}

StatusGlyphWidget::GlyphKind StatusGlyphWidget::glyphKindFor(ev::PileStatus state)
{
    switch (state) {
    case ev::PileStatus::Idle: return GlyphKind::CheckCircle;
    case ev::PileStatus::Reserved: return GlyphKind::ClockDashed;
    case ev::PileStatus::Charging: return GlyphKind::BoltDot;
    case ev::PileStatus::Fault: return GlyphKind::WarningTriangle;
    case ev::PileStatus::Offline: return GlyphKind::LinkOff;
    case ev::PileStatus::Unknown: return GlyphKind::QuestionDiamond;
    }
    return GlyphKind::QuestionDiamond;
}

void StatusGlyphWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor color = colorForState(m_state);
    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const qreal cx = 8.0;
    const qreal cy = 8.0;

    switch (glyphKind()) {
    case GlyphKind::CheckCircle:
        painter.drawEllipse(QPointF(cx, cy), 5.6, 5.6);
        painter.drawPolyline(QPolygonF{
            QPointF(5.4, 8.4), QPointF(7.2, 10.2), QPointF(10.7, 5.9)});
        break;
    case GlyphKind::ClockDashed: {
        QPen dashed = pen;
        dashed.setStyle(Qt::DashLine);
        dashed.setDashPattern({2.6, 1.8});
        painter.setPen(dashed);
        painter.drawEllipse(QPointF(cx, cy), 5.6, 5.6);
        painter.setPen(pen);
        painter.drawLine(QPointF(cx, cy), QPointF(cx, 5.2));
        painter.drawLine(QPointF(cx, cy), QPointF(10.3, cy + 2.1));
        break;
    }
    case GlyphKind::BoltDot: {
        painter.drawEllipse(QPointF(cx, cy), 5.2, 5.2);
        // 圆内实心闪电（区别于细描边图形，充电是唯一“有能量”状态）
        QPainterPath bolt;
        bolt.moveTo(9.0, 3.4);
        bolt.lineTo(5.6, 9.2);
        bolt.lineTo(7.7, 9.2);
        bolt.lineTo(7.0, 12.6);
        bolt.lineTo(10.4, 6.6);
        bolt.lineTo(8.3, 6.6);
        bolt.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(bolt);
        break;
    }
    case GlyphKind::WarningTriangle: {
        QPainterPath triangle;
        triangle.moveTo(cx, 2.6);
        triangle.lineTo(14.0, 13.0);
        triangle.lineTo(2.0, 13.0);
        triangle.closeSubpath();
        painter.drawPath(triangle);
        painter.drawLine(QPointF(cx, 6.2), QPointF(cx, 9.6));
        QPen dot = pen;
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(cx, 11.5), 0.85, 0.85);
        painter.setPen(dot);
        break;
    }
    case GlyphKind::LinkOff:
        painter.drawEllipse(QPointF(cx, cy), 5.4, 5.4);
        painter.drawLine(QPointF(4.7, 4.7), QPointF(11.3, 11.3));
        break;
    case GlyphKind::QuestionDiamond: {
        QPainterPath diamond;
        diamond.moveTo(cx, 2.2);
        diamond.lineTo(13.8, cy);
        diamond.lineTo(cx, 13.8);
        diamond.lineTo(2.2, cy);
        diamond.closeSubpath();
        painter.drawPath(diamond);
        painter.drawLine(QPointF(cx, 6.6), QPointF(cx, 7.9));
        painter.drawLine(QPointF(cx, 7.9), QPointF(cx, 7.2));
        QPainterPath question;
        question.moveTo(6.4, 6.2);
        question.cubicTo(6.4, 4.6, 9.6, 4.6, 9.6, 6.3);
        question.cubicTo(9.6, 7.5, 8.0, 7.9, 8.0, 9.0);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(question);
        QPen dot = pen;
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(cx, 11.4), 0.85, 0.85);
        painter.setPen(dot);
        break;
    }
    }
}
