#include "stationtopologywidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>

#include "theme/generated/theme_tokens.h"

namespace {

// 站内桩的最高关注状态：故障 > 离线 > 充电中 > 已预约 > 空闲。
// 状态缺失（unknown）与无桩站退回中立灰，不参与"关注"排序。
QColor dayColorForStatus(ev::PileStatus status)
{
    switch (status) {
    case ev::PileStatus::Fault:
        return ev::theme::kDayFault;
    case ev::PileStatus::Offline:
        return ev::theme::kDayOffline;
    case ev::PileStatus::Charging:
        return ev::theme::kDayCharging;
    case ev::PileStatus::Reserved:
        return ev::theme::kDayReserved;
    case ev::PileStatus::Idle:
        return ev::theme::kDayIdle;
    }
    return ev::theme::kDayUnknown;
}

constexpr int kNodeRadius = 9;
constexpr int kHitSlop = 18; // 点击命中半径（节点半径 + 容差）

} // namespace

StationTopologyWidget::StationTopologyWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::ArrowCursor);
}

void StationTopologyWidget::setStations(const QList<ev::StationInfo> &stations)
{
    m_stations = stations;
    update();
}

void StationTopologyWidget::setPiles(const QList<ev::PileInfo> &piles)
{
    m_piles = piles;
    update();
}

QColor StationTopologyWidget::informationLineColor() const
{
    // 承载连接关系的信息线必须使用信息拓扑线令牌（spec §4.2 对比度约束）
    return ev::theme::kDayTopologyLine;
}

void StationTopologyWidget::rebuildLayout()
{
    m_points.clear();
    // 四周留白：上/左/右放节点与站名，底部放图例
    m_plotRect = QRectF(contentsRect()).adjusted(34, 30, -34, -40);
    if (m_stations.isEmpty())
        return;

    double minLon = m_stations.first().longitude;
    double maxLon = minLon;
    double minLat = m_stations.first().latitude;
    double maxLat = minLat;
    for (const auto &station : m_stations) {
        minLon = qMin(minLon, station.longitude);
        maxLon = qMax(maxLon, station.longitude);
        minLat = qMin(minLat, station.latitude);
        maxLat = qMax(maxLat, station.latitude);
    }

    const double rangeLon = maxLon - minLon;
    const double rangeLat = maxLat - minLat;
    // 投影内容再内缩 12%，避免最边缘节点贴边
    const double inset = 0.12;
    const QRectF area = m_plotRect;

    for (const auto &station : m_stations) {
        const double fx = (rangeLon == 0.0) ? 0.5 : (station.longitude - minLon) / rangeLon;
        const double fy = (rangeLat == 0.0) ? 0.5 : (station.latitude - minLat) / rangeLat;
        const double x = area.left() + inset * area.width()
            + fx * (1.0 - 2.0 * inset) * area.width();
        // 纬度越大越靠北（上方）
        const double y = area.top() + inset * area.height()
            + (1.0 - fy) * (1.0 - 2.0 * inset) * area.height();
        m_points.append(QPointF(x, y));
    }
}

QColor StationTopologyWidget::colorForStation(int stationId) const
{
    static const ev::PileStatus priority[] = {
        ev::PileStatus::Fault, ev::PileStatus::Offline, ev::PileStatus::Charging,
        ev::PileStatus::Reserved, ev::PileStatus::Idle,
    };
    int bestRank = -1;
    for (const auto &pile : m_piles) {
        if (pile.stationId != stationId)
            continue;
        for (int rank = 0; rank < 5; ++rank) {
            if (pile.status == priority[rank]) {
                if (bestRank < 0 || rank < bestRank)
                    bestRank = rank;
                break;
            }
        }
    }
    if (bestRank >= 0)
        return dayColorForStatus(priority[bestRank]);

    // 无桩站点：按站点运行状态区分，避免与"空闲"混淆
    for (const auto &station : m_stations) {
        if (station.id == stationId)
            return station.status == QStringLiteral("active")
                ? ev::theme::kDayUnknown
                : ev::theme::kDayOffline;
    }
    return ev::theme::kDayUnknown;
}

void StationTopologyWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    rebuildLayout();

    if (m_stations.isEmpty()) {
        painter.setPen(ev::theme::kDayMutedText);
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("暂无站点数据"));
        return;
    }

    // 背景网格：非信息装饰，只允许使用装饰结构色
    QColor gridColor = ev::theme::kDayDecorativeStructure;
    gridColor.setAlpha(140);
    painter.setPen(QPen(gridColor, 1.0));
    const qreal gridStep = 36.0;
    for (qreal x = m_plotRect.left(); x <= m_plotRect.right(); x += gridStep)
        painter.drawLine(QPointF(x, m_plotRect.top()), QPointF(x, m_plotRect.bottom()));
    for (qreal y = m_plotRect.top(); y <= m_plotRect.bottom(); y += gridStep)
        painter.drawLine(QPointF(m_plotRect.left(), y), QPointF(m_plotRect.right(), y));

    // 站间连接线：信息拓扑线色 + 虚线表达"示意网络"，非物理电网连接
    if (m_points.size() > 1) {
        QPolygonF polyline;
        for (const auto &point : m_points)
            polyline << point;
        QColor lineColor = informationLineColor();
        lineColor.setAlpha(190);
        painter.setPen(QPen(lineColor, 1.4, Qt::DashLine));
        painter.drawPolyline(polyline);
    }

    // 站点节点：状态语义色实心圆 + 深色描边 + 中心表面点
    QFont nameFont = painter.font();
    nameFont.setPixelSize(11);
    painter.setFont(nameFont);
    for (int i = 0; i < m_stations.size(); ++i) {
        const QPointF center = m_points.at(i);
        const ev::StationInfo &station = m_stations.at(i);
        const QColor fill = colorForStation(station.id);

        painter.setPen(QPen(fill.darker(135), 1.6));
        painter.setBrush(fill);
        painter.drawEllipse(center, kNodeRadius, kNodeRadius);

        painter.setPen(Qt::NoPen);
        painter.setBrush(ev::theme::kDaySurface);
        painter.drawEllipse(center, 3.2, 3.2);

        // 站名（承载信息：使用主文字色而非装饰色）
        painter.setPen(ev::theme::kDayMutedText);
        const QRectF nameRect(center.x() - 70, center.y() + kNodeRadius + 4, 140, 16);
        painter.drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop, station.name);
    }

    // 图例（spec §5.2：拓扑图必须声明示意性质）
    painter.setPen(ev::theme::kDayMutedText);
    QFont legendFont = painter.font();
    legendFont.setPixelSize(10);
    painter.setFont(legendFont);
    const QRectF legendRect(contentsRect().left() + 8,
                            contentsRect().bottom() - 18,
                            contentsRect().width() - 16, 14);
    painter.drawText(legendRect, Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("态势示意，不代表物理电网连接"));
}

void StationTopologyWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    rebuildLayout();
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF delta = m_points.at(i) - event->position();
        if (delta.x() * delta.x() + delta.y() * delta.y()
            <= kHitSlop * kHitSlop) {
            emit stationActivated(m_stations.at(i).id);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

#ifdef QT_TESTLIB_LIB
void StationTopologyWidget::activateStationForTest(int stationId)
{
    // 与真实点击同路径：仅当站点存在时发出激活信号
    for (const auto &station : m_stations) {
        if (station.id == stationId) {
            emit stationActivated(stationId);
            return;
        }
    }
}
#endif
