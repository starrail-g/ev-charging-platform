#include "stationtopologywidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>
#include <QTimer>

#include <cmath>
#include <utility>

#include "statuspulsewidget.h"
#include "theme/generated/theme_tokens.h"
#include "theme/theme.h"
#include "widgets/staticmapimageprovider.h"

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
    case ev::PileStatus::Unknown:
        return ev::theme::kDayUnknown;
    }
    return ev::theme::kDayUnknown;
}

constexpr int kNodeRadius = 9;
constexpr int kHitSlop = 18; // 点击命中半径（节点半径 + 容差）
constexpr int kPulseExtent = 16;

// 地图投影点越界防御带：取景保证全站入图，正常路径不触发；
// 越界/非有限点以 NaN 占位（保持与 m_stations 对齐），绘制与命中自然跳过。
constexpr int kMapEdgeGuard = 24;

} // namespace

const QString StationTopologyWidget::kDegradedServiceNote =
    QStringLiteral("地图服务不可用，已回退示意拓扑");
const QString StationTopologyWidget::kDegradedProjectionNote =
    QStringLiteral("坐标超出地图投影范围，已回退示意拓扑");
const QString StationTopologyWidget::kDegradedTooLargeNote =
    QStringLiteral("区域过大，暂不支持地图视图");

StationTopologyWidget::StationTopologyWidget(QWidget *parent)
    : QWidget(parent)
    , m_motionEnabled(ev::Theme::motionEnabled())
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::StrongFocus); // 键盘可达（Review Task 9）
    m_clock.start();
}

StationTopologyWidget::~StationTopologyWidget()
{
    // §3.4 析构保护：先作废序号，再取消在途（回调闭包另有 QPointer 双保险）
    ++m_fetchSeq;
    if (m_mapProvider)
        m_mapProvider->cancelAll();
}

void StationTopologyWidget::setMapImageProvider(ev::MapImageProvider *provider)
{
    if (m_mapProvider == provider)
        return;
    // 替换提供者 = 视图失效：作废在途、停 debounce、清成功缓存与标注，
    // 防旧响应/旧防抖目标污染新提供者（评审 P1 同构）
    discardPendingMapFetch();
    m_mapAvailable = false;
    m_mapFingerprint = 0;
    m_mapImage = QImage();
    m_degradedNote.clear();
    if (m_mapProvider) {
        m_mapProvider->cancelAll();
        delete m_mapProvider; // 子对象（child），直接释放
    }
    m_mapProvider = provider;
    if (m_mapProvider) {
        if (auto *object = dynamic_cast<QObject *>(provider))
            object->setParent(this); // 所有权转移给控件
        evaluateMap();
    } else {
        update();
    }
}

void StationTopologyWidget::setStations(const QList<ev::StationInfo> &stations)
{
    // §3.5 统一净化：剔除非有限/越业务范围的坐标站——拓扑布局与取景
    // 只消费净化后列表，杜绝非法值进入 min/max 归一化产生 NaN 几何
    m_stations.clear();
    m_stations.reserve(stations.size());
    for (const ev::StationInfo &station : stations) {
        const bool usable = std::isfinite(station.latitude) && std::isfinite(station.longitude)
            && station.latitude >= -90.0 && station.latitude <= 90.0
            && station.longitude >= -180.0 && station.longitude <= 180.0;
        if (usable)
            m_stations.append(station);
    }
    m_focusIndex = qMin(m_focusIndex, m_stations.size() - 1);
    rebuildPulses();
    update();
    evaluateMap(); // §3.4 触发点 1：数据到达（含首帧）
}

void StationTopologyWidget::setPiles(const QList<ev::PileInfo> &piles)
{
    m_piles = piles;
    rebuildPulses();
    update();
}

void StationTopologyWidget::setMotionEnabled(bool enabled)
{
    if (m_motionEnabled == enabled)
        return;
    m_motionEnabled = enabled;
    for (StatusPulseWidget *pulse : std::as_const(m_pulses))
        pulse->setMotionEnabled(enabled);
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

    if (m_mapAvailable) {
        // ---- 真图模式：D7 投影（图中心 == center 参数）+ 图居中偏移 ----
        const QRectF cr = contentsRect();
        const qreal offsetX = (cr.width() - m_mapImage.width()) / 2.0;
        const qreal offsetY = (cr.height() - m_mapImage.height()) / 2.0;
        const qreal minX = cr.left() - kMapEdgeGuard;
        const qreal maxX = cr.right() + kMapEdgeGuard;
        const qreal minY = cr.top() - kMapEdgeGuard;
        const qreal maxY = cr.bottom() + kMapEdgeGuard;
        for (const ev::StationInfo &station : std::as_const(m_stations)) {
            QPointF point = ev::lonLatToWidgetPoint(
                {station.latitude, station.longitude}, m_mapView);
            point.rx() += cr.left() + offsetX;
            point.ry() += cr.top() + offsetY;
            // 防御：越界/非有限点不绘制（NaN 占位保持与 m_stations 对齐，不打印坐标）
            if (!std::isfinite(point.x()) || !std::isfinite(point.y())
                || point.x() < minX || point.x() > maxX
                || point.y() < minY || point.y() > maxY) {
                m_points.append(QPointF(qQNaN(), qQNaN()));
                continue;
            }
            m_points.append(point);
        }
        return;
    }

    // ---- 拓扑模式（现状）：min/max 归一化投影 ----
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

ev::PileStatus StationTopologyWidget::stationTopStatus(int stationId) const
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
        return priority[bestRank];
    // 无桩站点：按站点运行状态区分，避免与"空闲"混淆
    for (const auto &station : m_stations) {
        if (station.id == stationId)
            return station.status == QStringLiteral("active")
                ? ev::PileStatus::Unknown
                : ev::PileStatus::Offline;
    }
    return ev::PileStatus::Unknown;
}

QColor StationTopologyWidget::colorForStation(int stationId) const
{
    return dayColorForStatus(stationTopStatus(stationId));
}

void StationTopologyWidget::rebuildPulses()
{
    // 同步销毁：pulse 是纯展示子组件、无活动信号栈，调用点都在外部
    // （数据刷新），立即重建保证 findChildren 断言与视觉同步一致。
    for (StatusPulseWidget *pulse : std::as_const(m_pulses))
        delete pulse;
    m_pulses.clear();
    m_pulseStationIndexes.clear();

    for (int index = 0; index < m_stations.size(); ++index) {
        const ev::PileStatus top = stationTopStatus(m_stations.at(index).id);
        // 只有"有能量/需处置"状态呼吸：充电中与故障；其余状态保持静态
        if (top != ev::PileStatus::Charging && top != ev::PileStatus::Fault)
            continue;
        auto *pulse = new StatusPulseWidget(top, this);
        pulse->setMotionEnabled(m_motionEnabled);
        // 呼吸只作为低幅 halo：点击穿透，节点选择仍由拓扑自身处理
        pulse->setAttribute(Qt::WA_TransparentForMouseEvents);
        pulse->setFixedSize(kPulseExtent, kPulseExtent);
        pulse->show();
        m_pulses.append(pulse);
        m_pulseStationIndexes.append(index);
    }
    layoutPulses();
}

void StationTopologyWidget::layoutPulses()
{
    for (int k = 0; k < m_pulses.size(); ++k) {
        const int stationIndex = m_pulseStationIndexes.at(k);
        if (stationIndex < 0 || stationIndex >= m_points.size())
            continue;
        const QPointF center = m_points.at(stationIndex);
        if (!std::isfinite(center.x()) || !std::isfinite(center.y()))
            continue;
        m_pulses.at(k)->move(qRound(center.x()) - kPulseExtent / 2,
                             qRound(center.y()) - kPulseExtent / 2);
    }
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

    const bool mapMode = m_mapAvailable;
    const QRectF cr = contentsRect();

    if (mapMode) {
        // ---- 真图模式：底图 + 节点（不画网格/连线/拓扑图例，§3.1）----
        const qreal offsetX = (cr.width() - m_mapImage.width()) / 2.0;
        const qreal offsetY = (cr.height() - m_mapImage.height()) / 2.0;
        if (offsetX >= 0.0 && offsetY >= 0.0)
            painter.drawImage(QPointF(cr.left() + offsetX, cr.top() + offsetY),
                              m_mapImage);
    } else {
        // ---- 拓扑模式：背景网格（非信息装饰，只允许使用装饰结构色）----
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
            for (const auto &point : m_points) {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
                    continue;
                polyline << point;
            }
            if (polyline.size() > 1) {
                QColor lineColor = informationLineColor();
                lineColor.setAlpha(190);
                painter.setPen(QPen(lineColor, 1.4, Qt::DashLine));
                painter.drawPolyline(polyline);
            }
        }
    }

    // 站点节点：状态语义色实心圆 + 深色描边 + 中心表面点；
    // 键盘焦点节点绘制 focus ring（状态不只靠颜色，spec §8.1）
    QFont nameFont = painter.font();
    nameFont.setPixelSize(11);
    painter.setFont(nameFont);
    const bool showFocusRing = hasFocus() && m_focusIndex >= 0;
    for (int i = 0; i < m_stations.size(); ++i) {
        const QPointF center = m_points.at(i);
        // 防御：越界占位点不绘制（§3.5；正常路径取景保证全包）
        if (!std::isfinite(center.x()) || !std::isfinite(center.y()))
            continue;
        const ev::StationInfo &station = m_stations.at(i);
        const QColor fill = colorForStation(station.id);

        if (showFocusRing && i == m_focusIndex) {
            QPen ringPen(ev::theme::kDayFocusBlue, 1.8);
            painter.setPen(ringPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(center, kNodeRadius + 4.5, kNodeRadius + 4.5);
        }

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

    // 呼吸 halo 子组件跟随最新投影点（在父节点之上绘制）
    layoutPulses();

    // 图例：真图模式隐藏；拓扑模式显示示意声明，降级时替换为降级标注（§3.1）
    if (!mapMode) {
        painter.setPen(ev::theme::kDayMutedText);
        QFont legendFont = painter.font();
        legendFont.setPixelSize(10);
        painter.setFont(legendFont);
        const QRectF legendRect(cr.left() + 8,
                                cr.bottom() - 18,
                                cr.width() - 16, 14);
        const QString text = m_degradedNote.isEmpty()
            ? QStringLiteral("态势示意，不代表物理电网连接")
            : m_degradedNote;
        painter.drawText(legendRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
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
            m_focusIndex = i; // 点击同步键盘焦点，focus ring 一致
            update();
            emit stationActivated(m_stations.at(i).id);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void StationTopologyWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_stations.isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }
    const int last = m_stations.size() - 1;
    if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Left) {
        moveFocus(event->key() == Qt::Key_Right ? 1 : -1);
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Space)
        && m_focusIndex >= 0 && m_focusIndex <= last) {
        emit stationActivated(m_stations.at(m_focusIndex).id);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void StationTopologyWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // §3.4 触发点 2：resize 后尺寸桶（32px）变化才评估
    const QSize bucket = requestBucketSize();
    if (bucket != m_lastBucketSize) {
        m_lastBucketSize = bucket;
        evaluateMap();
    }
}

void StationTopologyWidget::moveFocus(int step)
{
    if (m_stations.isEmpty())
        return;
    const int last = m_stations.size() - 1;
    if (m_focusIndex < 0) {
        m_focusIndex = step > 0 ? 0 : last; // 首次方向键进入焦点序列
    } else {
        m_focusIndex = qBound(0, m_focusIndex + step, last);
    }
    update();
}

// ---- T3 真图触发/重试/竞态状态机（计划 §3.4/§3.5）----

qint64 StationTopologyWidget::nowMs() const
{
    return m_useFakeClock ? m_fakeClockMs : m_clock.elapsed();
}

QSize StationTopologyWidget::requestBucketSize() const
{
    const int cw = contentsRect().width();
    const int ch = contentsRect().height();
    // 未布局/过小（低于 clamp 下限 320×240）→ 空尺寸：不发请求、无故障标注。
    // 门槛必须 ≥ clamp 下限——否则拉取的图会大于控件（偏移为负被跳过 → 图不绘制，
    // 投影点错位），评审 P2。
    if (cw < 320 || ch < 240)
        return QSize();
    int width = (cw / kBucketPx) * kBucketPx;
    int height = (ch / kBucketPx) * kBucketPx;
    width = qBound(320, width, 1280);
    height = qBound(240, height, 960);
    return QSize(width, height);
}

bool StationTopologyWidget::stationsOutOfProjectionRange() const
{
    for (const ev::StationInfo &station : std::as_const(m_stations)) {
        if (std::fabs(station.latitude) > ev::kWebMercatorMaxLat)
            return true;
    }
    return false;
}

void StationTopologyWidget::evaluateMap()
{
    // 触发点统一评估（§3.4）。决策路径：
    //   无提供者/无凭据 → 静默拓扑（无标注，现状行为）
    //   净化后无站     → 拓扑空态（无标注）
    //   尺寸未布局     → 等待 resize（无标注）
    //   超投影范围     → 拓扑 + 投影标注，不发请求
    //   区域过大       → 拓扑 + 区域标注，不发请求
    //   缓存命中       → 直接应用缓存（不请求）
    //   同视图在途     → 复用（不取消/不重发）
    //   失败时间门内   → 保持降级展示（不重试）
    //   否则           → 作废旧在途 → debounce 300ms → 新请求

    // 先判断是否需要作废在途（目标不可达/提供者变化），保证旧响应不落地
    const bool providerReady = m_mapProvider && m_mapProvider->canFetch();
    const bool hasStations = !m_stations.isEmpty();
    const QSize bucket = requestBucketSize();
    const bool targetReachable = providerReady && hasStations && !bucket.isEmpty();

    if (!targetReachable) {
        // 视图失效：无条件作废在途 + 停挂起的 debounce（评审 P1：防抖期间的
        // 旧目标不得在目标失效后继续发出）+ 清成功缓存与标注
        discardPendingMapFetch();
        m_mapAvailable = false;
        m_degradedNote.clear();
        update();
        return;
    }

    // 可达目标：净化后站集（§3.5 输入边界）
    QVector<ev::MapLatLng> coordinates;
    coordinates.reserve(m_stations.size());
    for (const ev::StationInfo &station : std::as_const(m_stations))
        coordinates.append({station.latitude, station.longitude});

    // 投影范围是独立检查（业务合法的极区站保留在拓扑列表，仅禁止上图）
    const bool outOfProjection = stationsOutOfProjectionRange();
    ev::StaticMapView view;
    const bool computed = ev::computeMapView(coordinates, bucket, &view);
    if (outOfProjection || !computed) {
        // 目标不可达 → 作废在途并落拓扑 + 对应标注（§3.5 文案区分）
        discardPendingMapFetch();
        m_mapAvailable = false;
        m_degradedNote = outOfProjection ? kDegradedProjectionNote
                                         : kDegradedTooLargeNote;
        update();
        return;
    }

    const quint64 fingerprint = ev::requestFingerprint(view);

    // 目标离开当前缓存视图 → 旧底图立即退役（评审 P1：缓存图仅指纹完全匹配
    // 才可激活；否则切换区域后、新图到达前的窗口内，旧底图会承载新区域标记）
    if (m_mapAvailable && fingerprint != m_mapFingerprint)
        m_mapAvailable = false;

    // 目标已离开在途视图 → 立即作废（§3.4：目标改变立即 ++，不能等新 fetch；
    // 否则迟到的旧响应会按新目标错误落地——缓存命中场景也须先作废）
    if (m_fetchInFlight && fingerprint != m_pendingFingerprint)
        discardPendingMapFetch();

    // 同视图成功缓存 → 重新激活直接应用（缓存图仍在且指纹完全匹配；
    // 目标曾离开过（退役）不代表缓存失效——只有失败/替换提供者才清缓存，
    // 评审 P1-2 语义：切换后回切原区域应免重拉）
    if (m_mapFingerprint != 0 && fingerprint == m_mapFingerprint) {
        // 评审 P1 补充：回到缓存视图时，挂起的异目标 debounce 必须一并取消——
        // 否则 timer 到期会用失效前的旧目标（B）发起请求，图回来覆盖 A 缓存
        discardPendingMapFetch();
        m_mapAvailable = true;
        update();
        return;
    }
    // 在途且同视图（异视图已在上方作废）→ 保留请求与序号（§3.4 复用）
    if (m_fetchInFlight)
        return;
    // debounce 等待中且目标未变 → 不重启计时
    if (m_debounceTimer && m_debounceTimer->isActive()
        && fingerprint == m_pendingFingerprint)
        return;
    // 失败后 30s 时间门内 → 不重试（§3.4 触发点 3；标注保持展示）
    if (m_lastFailureMs >= 0 && nowMs() - m_lastFailureMs < kRetryGateMs)
        return;

    // 新目标 / 过门重试
    ++m_fetchSeq;
    if (m_fetchInFlight)
        m_mapProvider->cancelAll();
    m_fetchInFlight = false;
    m_pendingView = view;
    m_pendingFingerprint = fingerprint;
    m_degradedNote.clear(); // 新尝试：清旧标注（失败会重新标注）
    scheduleFetch();
}

void StationTopologyWidget::discardPendingMapFetch()
{
    // §3.4 目标失效统一出口（评审 P1）：
    //  ① generation ++——迟到响应作废；
    //  ② 取消在途请求；
    //  ③ 停止 debounce timer 并清待发目标——否则 timer 到期会用失效前的
    //     旧目标发起新请求（视图失效 ≠ 请求已发出时同样要停）
    ++m_fetchSeq;
    if (m_fetchInFlight) {
        if (m_mapProvider)
            m_mapProvider->cancelAll();
        m_fetchInFlight = false;
    }
    if (m_debounceTimer)
        m_debounceTimer->stop();
    m_pendingFingerprint = 0;
    m_pendingView = ev::StaticMapView();
}

void StationTopologyWidget::scheduleFetch()
{
    if (!m_debounceTimer) {
        m_debounceTimer = new QTimer(this);
        m_debounceTimer->setSingleShot(true);
        connect(m_debounceTimer, &QTimer::timeout, this, [this] {
            if (!m_mapProvider || !m_mapProvider->canFetch())
                return;
            if (m_pendingFingerprint == 0)
                return;
            m_fetchInFlight = true;
            ++m_fetchAttemptCount;
            const int seq = m_fetchSeq;
            const ev::StaticMapView view = m_pendingView;
            // 回调三保险：QPointer（控件析构）+ seq（视图作废/替换提供者）+
            // view 匹配（回调视图必须仍是当前目标；seq 校验蕴含，注释说明）
            QPointer<StationTopologyWidget> self(this);
            m_mapProvider->fetch(view, seq,
                                 [self, seq](int cbSeq, bool ok, const QImage &image,
                                             ev::MapImageProvider::Failure failure) {
                                     if (!self)
                                         return;
                                     if (cbSeq != seq || seq != self->m_fetchSeq)
                                         return; // superseded：旧响应丢弃
                                     self->m_fetchInFlight = false;
                                     self->onFetchResult(seq, ok, image, failure);
                                 });
        });
    }
    m_debounceTimer->start(kDebounceMs); // 单发重启 = 高频触发合并
}

void StationTopologyWidget::onFetchResult(int seq, bool ok, const QImage &image,
                                          ev::MapImageProvider::Failure failure)
{
    // seq 校验由调用方完成；到达此处的都是当前目标视图的响应
    if (ok && failure == ev::MapImageProvider::Failure::None) {
        // 成功：原子保存 (view, 图)，按该视图投影当前站点（§3.4）
        m_mapView = m_pendingView;
        m_mapFingerprint = m_pendingFingerprint;
        m_mapImage = image;
        m_mapAvailable = true;
        m_lastFailureMs = -1; // 成功复位失败时间门
        m_degradedNote.clear();
        update();
        return;
    }
    // 失败：清成功缓存标记与图（旧图不得承载新标记，且失败后回切原区域也必须
    // 重拉——测试锁定语义），落拓扑 + 标注；重试只受 30s 时间门约束
    m_mapAvailable = false;
    m_mapFingerprint = 0;
    m_mapImage = QImage();
    m_lastFailureMs = nowMs();
    m_degradedNote = kDegradedServiceNote;
    update();
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

QPointF StationTopologyWidget::mapPointForTest(int index)
{
    rebuildLayout(); // 按当前模式重建（与 paint 同一函数）
    if (index < 0 || index >= m_points.size())
        return QPointF();
    return m_points.at(index);
}

void StationTopologyWidget::advanceClockForTest(qint64 ms)
{
    m_useFakeClock = true;
    m_fakeClockMs += ms;
}
#endif
