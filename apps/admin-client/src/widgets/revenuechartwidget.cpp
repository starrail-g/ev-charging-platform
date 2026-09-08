#include "revenuechartwidget.h"

#include <QCategoryAxis>
#include <QChart>
#include <QGraphicsLayout>
#include <QLineSeries>
#include <QPainter>
#include <QPen>
#include <QToolTip>
#include <QValueAxis>

#include <limits>

#include "theme/generated/theme_tokens.h"

namespace ev {
namespace {

// 金额(整数分)→ 折线 Y(元) 只在绘图边界换算; 悬停/表格金额仍从整数分格式化
qreal toYuan(qint64 cents)
{
    return static_cast<qreal>(cents) / 100.0;
}

// 与 Web 侧一致的紧凑日期标签(M/d)
QString dayLabel(const QDate &date)
{
    return date.toString(QStringLiteral("M/d"));
}

// Full 稀疏标签步长: 7 点逐日; 30 点约 6 个(0,6,12,18,24,29, 含首尾)
int labelStep(int count)
{
    if (count <= 7)
        return 1;
    return (count + 4) / 5;
}

} // namespace

RevenueChartWidget::RevenueChartWidget(Mode mode, QWidget *parent)
    : QChartView(parent)
    , m_mode(mode)
{
    auto *chart = new QChart; // 无 QWidget 父: setChart 后由 QChartView 场景接管
    chart->legend()->hide();
    chart->setBackgroundVisible(false);          // 防止默认白底盖住背景金额
    chart->setPlotAreaBackgroundVisible(false);
    chart->setDropShadowEnabled(false);
    chart->setMargins(mode == Mode::Mini ? QMargins(0, 0, 0, 0)
                                         : QMargins(8, 4, 8, 4));
    chart->layout()->setContentsMargins(0, 0, 0, 0);
    setChart(chart); // QChartView 接管 chart 所有权
    setRenderHint(QPainter::Antialiasing);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(Qt::NoBrush);             // 视图背景透明(面板底色透出)
    viewport()->setAutoFillBackground(false);

    // 单条线 + 两根轴各只创建一次; 更新只做 replace(points)/标签重建, 不累积
    m_line = new QLineSeries(chart);
    QColor lineColor = ev::theme::kDayFocusBlue;
    if (mode == Mode::Mini)
        lineColor.setAlphaF(0.3); // Mini: 折线为低透明度背景层(用户指定 0.3, 金额在前景)
    QPen linePen(lineColor, 1.8);
    m_line->setPen(linePen);
    m_line->setPointsVisible(false);
    chart->addSeries(m_line);

    m_xAxis = new QCategoryAxis(chart);
    m_xAxis->setLabelsVisible(mode == Mode::Full);
    // QtCharts 默认 truncateLabels=true: 30 类时按 31 tick 均分每标签配额仅
    // ~19px, 'M/d' 日期被截成 '...' 且变宽后首尾标签被裁剪/重叠判定隐藏 ——
    // 稀疏标签间距 5-6 天远大于文本宽, 无需截断, 显式关闭
    m_xAxis->setTruncateLabels(false);
    m_xAxis->setGridLineVisible(false);
    m_xAxis->setLineVisible(false);
    m_xAxis->setLabelsColor(ev::theme::kDayMutedText);
    m_xAxis->setLabelsAngle(0);

    m_yAxis = new QValueAxis(chart);
    m_yAxis->setLabelsVisible(mode == Mode::Full);
    m_yAxis->setGridLineVisible(mode == Mode::Full);
    m_yAxis->setLineVisible(false);
    m_yAxis->setLabelsColor(ev::theme::kDayMutedText);
    m_yAxis->setGridLineColor(ev::theme::kDayDecorativeStructure);
    m_yAxis->setLabelFormat(QStringLiteral("%.0f"));
    if (mode == Mode::Full)
        m_yAxis->setTitleText(QStringLiteral("¥")); // 纵轴金额单位(人民币符号)

    chart->addAxis(m_xAxis, Qt::AlignBottom);
    chart->addAxis(m_yAxis, Qt::AlignLeft);
    m_line->attachAxis(m_xAxis);
    m_line->attachAxis(m_yAxis);

    if (mode == Mode::Mini) {
        // 隐藏轴对象: 不可见轴不占图表布局空间 → plotArea ≈ 视口(消除隐藏轴占位
        // 造成的折线内缩偏移); 低透明度坐标网格由 drawBackground 手绘。
        m_xAxis->setVisible(false);
        m_yAxis->setVisible(false);
    } else {
        m_xAxis->setVisible(true);
        m_yAxis->setVisible(true);
    }

    connect(m_line, &QLineSeries::hovered, this, [this](const QPointF &point, bool state) {
        if (m_mode != Mode::Full) {
            QToolTip::hideText();
            return;
        }
        if (!state) {
            QToolTip::hideText();
            return;
        }
        const int julian = qRound64(point.x());
        for (const auto &day : m_data.days) {
            if (day.date.toJulianDay() != julian)
                continue;
            const QString text = QStringLiteral("%1\n%2")
                .arg(day.date.toString(Qt::ISODate), formatYuanCents(day.revenueCents));
            const QPointF viewPos = this->chart()->mapToPosition(point, m_line);
            QToolTip::showText(viewport()->mapToGlobal(viewPos.toPoint()), text, this);
            return;
        }
        QToolTip::hideText();
    });
}

void RevenueChartWidget::setSeries(const ev::RevenueSeries &series)
{
    if (series.days.isEmpty()) {
        clearSeries();
        return;
    }
    m_data = series;

    QList<QPointF> points;
    points.reserve(m_data.days.size());
    qreal maximumY = 0.0;
    qreal minimumY = std::numeric_limits<qreal>::max();
    for (const auto &day : m_data.days) {
        const qreal yuan = toYuan(day.revenueCents);
        points.append(QPointF(static_cast<qreal>(day.date.toJulianDay()), yuan));
        maximumY = qMax(maximumY, yuan);
        minimumY = qMin(minimumY, yuan);
    }
    m_line->replace(points);

    // X 分类轴: 每个儒略日占一个单位宽的类目(标签居中在点上); 旧标签全删再加。
    // QCategoryAxis 要求 label 唯一(重复 label 被静默拒绝、不建类目)——
    // 空串做隐藏位会因重复被拒导致类目缺失、后续标签错位(实测 30 类只成 7 类),
    // 隐藏位改用递增空格串: 唯一、绘制零字形。
    clearXAxisCategories();
    const int count = m_data.days.size();
    // X 轴水平留白: Mini 0.3 天(折线视觉贴边); Full 稀疏标签需把首尾类加宽到
    // 超过标签文本宽, 否则 QtCharts 对贴 plotArea 边缘的窄类 forceHide 整条标签
    // (30 类时 1 天宽 ≈28px < 'M/d' ≈38px → 8/3 与 9/1 被静默隐藏, 实测)
    const qreal xPad = m_mode == Mode::Mini ? 0.3 : (count > 10 ? 1.5 : 0.5);
    const qreal xMin = static_cast<qreal>(m_data.days.first().date.toJulianDay()) - xPad;
    const qreal xMax = static_cast<qreal>(m_data.days.last().date.toJulianDay()) + xPad;
    m_xAxis->setStartValue(xMin); // 空轴时设定首类真实起点(默认 0 会把首标签甩出可视区)
    const int step = labelStep(count);
    for (int i = 0; i < count; ++i) {
        const QDate date = m_data.days.at(i).date;
        const bool labeled = m_mode == Mode::Full && (i % step == 0 || i == count - 1);
        m_xAxis->append(labeled ? dayLabel(date) : QString(i + 1, QLatin1Char(' ')),
                        static_cast<qreal>(date.toJulianDay()) + 0.5);
    }
    m_xAxis->setRange(xMin, xMax);

    // Y 量程: Full 自 0(真实零基线); Mini 聚焦数据带(上下各 15% 余量)让折线
    // 垂直居中于卡片(0 基线会把线压到顶部/底部边缘, 造成"偏移"观感)。
    // 全零/无效数据 → 0..1 非退化量程。
    if (m_mode == Mode::Mini && maximumY > 0.0 && maximumY > minimumY) {
        const qreal span = maximumY - minimumY;
        m_yMin = qMax(0.0, minimumY - span * 0.15);
        m_yMax = maximumY + span * 0.15;
    } else {
        m_yMin = 0.0;
        m_yMax = maximumY > 0.0 ? maximumY * 1.15 : 1.0;
    }
    m_yAxis->setRange(m_yMin, m_yMax);
    m_hasData = true;

    viewport()->update();
}

void RevenueChartWidget::clearXAxisCategories()
{
    // QCategoryAxis::remove 只收单 label 逐个删; 修复后 label 全唯一, 循环安全
    const QStringList oldLabels = m_xAxis->categoriesLabels();
    for (const QString &label : oldLabels)
        m_xAxis->remove(label);
}

void RevenueChartWidget::clearSeries()
{
    m_data = RevenueSeries();
    m_line->replace(QList<QPointF>());
    // 轴状态一并清空: 分类(旧日期标签)与 X/Y 量程, 防"折线没了但上次的日期轴/
    // 网格仍在"的半残留图(评审 B-3); 下次 setSeries 前轴呈干净初始态
    clearXAxisCategories();
    m_xAxis->setRange(0.0, 1.0);
    m_yAxis->setRange(0.0, 1.0);
    QToolTip::hideText();
    m_hasData = false;
    m_yMin = 0.0;
    m_yMax = 1.0;
    viewport()->update();
}

int RevenueChartWidget::pointCount() const
{
    return m_line->count();
}

QString RevenueChartWidget::displayedRange() const
{
    return m_data.range;
}

void RevenueChartWidget::setForegroundPainter(
    std::function<void(QPainter *, const QRectF &)> painter)
{
    m_foregroundPainter = std::move(painter);
}

void RevenueChartWidget::drawBackground(QPainter *painter, const QRectF &rect)
{
    // 背景层: 基类清背景 → 轻量坐标网格(Mini, 低透明度结构色);
    // 折线由场景绘制(前景层之下), 金额文字在 drawForeground。
    QGraphicsView::drawBackground(painter, rect);

    if (m_mode == Mode::Mini && m_hasData && m_yMax > m_yMin) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        const QRectF bounds = mapToScene(viewport()->rect()).boundingRect();
        const qreal x0 = bounds.left();
        const qreal x1 = bounds.right();
        const qreal yTop = bounds.top();
        const qreal yBottom = bounds.bottom();
        const auto yFor = [&](qreal value) {
            return yBottom
                - (value - m_yMin) / (m_yMax - m_yMin) * (yBottom - yTop);
        };
        // 三条水平网格(数据带内均分), 低透明度
        QColor gridColor = ev::theme::kDayDecorativeStructure;
        gridColor.setAlphaF(0.45);
        QPen gridPen(gridColor, 1.0);
        painter->setPen(gridPen);
        for (int i = 1; i <= 3; ++i) {
            const qreal y = yFor(m_yMin + (m_yMax - m_yMin) * i / 4.0);
            painter->drawLine(QPointF(x0, y), QPointF(x1, y));
        }
        // 底线(横轴)稍实 + 左侧竖线(纵轴)更淡
        QColor axisColor = ev::theme::kDayDecorativeStructure;
        axisColor.setAlphaF(0.65);
        QPen axisPen(axisColor, 1.0);
        painter->setPen(axisPen);
        painter->drawLine(QPointF(x0, yBottom), QPointF(x1, yBottom));
        QColor spineColor = ev::theme::kDayDecorativeStructure;
        spineColor.setAlphaF(0.35);
        painter->setPen(QPen(spineColor, 1.0));
        painter->drawLine(QPointF(x0, yTop), QPointF(x0, yBottom));
        painter->restore();
    }
}

void RevenueChartWidget::drawForeground(QPainter *painter, const QRectF &rect)
{
    // 前景层: 金额文字画在折线/网格之上(Mini; Full 无前景回调)
    QGraphicsView::drawForeground(painter, rect);
    if (m_foregroundPainter) {
        painter->save();
        // rect 是局部重绘区域，金额排版必须使用完整视口的场景坐标。
        m_foregroundPainter(painter, mapToScene(viewport()->rect()).boundingRect());
        painter->restore();
    }
}

} // namespace ev
