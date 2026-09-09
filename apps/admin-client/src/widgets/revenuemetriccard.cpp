#include "revenuemetriccard.h"

#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "revenuechartwidget.h"
#include "theme/generated/theme_tokens.h"

namespace {
// 上行固定大字显示当前范围，下行固定小字显示另一范围；点击后交换内容。
constexpr int kBandTopY = 2;     // 上行(视口内)顶部偏移
constexpr int kBandTopH = 36;    // 上行带高(容纳 28px 主字号)
constexpr int kBandBottomH = 20; // 下行带高(近 30 日行)
constexpr qreal kBigSize = 28.0;
constexpr qreal kSmallSize = 12.0;
constexpr qreal kMainAlpha = 0.92; // 主金额(大字, 前景可读; 折线为低透明背景层)
constexpr qreal kSubAlpha = 0.85;  // 次金额(小字, mutedText)
constexpr qreal kTagSize = 11.0;

} // namespace

RevenueMetricCard::RevenueMetricCard(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("revenueCard"));
    setProperty("panel", true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    // 标题行: 稳定标题 + 右上详情入口(携带当前范围)
    auto *head = new QHBoxLayout;
    head->setSpacing(4);
    auto *title = new QLabel(QStringLiteral("营收"), this);
    title->setObjectName(QStringLiteral("revenueCardTitle"));
    head->addWidget(title);
    head->addStretch();
    m_detailsButton = new QToolButton(this);
    m_detailsButton->setObjectName(QStringLiteral("revenueDetailsButton"));
    m_detailsButton->setText(QStringLiteral("详情"));
    m_detailsButton->setCursor(Qt::PointingHandCursor);
    m_detailsButton->setAutoRaise(true);
    m_detailsButton->setFocusPolicy(Qt::StrongFocus);
    connect(m_detailsButton, &QToolButton::clicked, this,
            [this] { emit detailsRequested(m_days); });
    head->addWidget(m_detailsButton);
    layout->addLayout(head);

    // 图表区: Mini 折线; 两行金额经背景 painter 叠在其下(线前景、金额背景)
    m_chart = new ev::RevenueChartWidget(ev::RevenueChartWidget::Mode::Mini, this);
    m_chart->setObjectName(QStringLiteral("revenueChart"));
    m_chart->setMinimumHeight(84);
    m_chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_chart->setForegroundPainter(
        [this](QPainter *painter, const QRectF &rect) { renderAmounts(painter, rect); });
    layout->addWidget(m_chart, 1);

    // 两个透明点击热区挂在 chart viewport 上(文字在背景绘制, 热区只负责命中/键盘/
    // tooltip/焦点环; Mini 无逐点 hover, 不与金额点击冲突)
    auto makeHotZone = [this](const char *objectName) {
        auto *button = new QToolButton(m_chart->viewport());
        button->setObjectName(QLatin1String(objectName));
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setText(QString());
        return button;
    };
    m_button7 = makeHotZone("revenue7dButton");
    m_button30 = makeHotZone("revenue30dButton");
    connect(m_button7, &QToolButton::clicked, this, [this] { setRange(7); });
    connect(m_button30, &QToolButton::clicked, this, [this] { setRange(30); });

    m_retryButton = makeHotZone("revenueRetryButton");
    m_retryButton->setText(QStringLiteral("重试"));
    m_retryButton->setVisible(false);
    connect(m_retryButton, &QToolButton::clicked, this,
            [this] { emit retryRequested(); });

    updateAccessibleText();
    layoutOverlayButtons();
}

void RevenueMetricCard::setStatistics(const ev::OverviewStats &stats)
{
    m_stats = stats;
    m_hasStats = true;
    updateChart(); // 不重置用户当前 m_days 选择
    updateAccessibleText();
    m_chart->viewport()->update();
}

void RevenueMetricCard::setRange(int days)
{
    if ((days != 7 && days != 30) || days == m_days)
        return;
    m_days = days;
    updateChart();
    updateAccessibleText();
    layoutOverlayButtons();
    m_chart->viewport()->update();
    emit rangeChanged(days);
}

void RevenueMetricCard::reset()
{
    m_stats = ev::OverviewStats();
    m_hasStats = false;
    m_days = 7;
    layoutOverlayButtons();
    m_chart->clearSeries();
    m_retryButton->setVisible(false);
    updateAccessibleText();
    m_chart->viewport()->update();
}

void RevenueMetricCard::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    // 延迟到本次布局完成后重排热区(卡片尺寸变化会带动 chart viewport 尺寸)
    QTimer::singleShot(0, this, [this] { layoutOverlayButtons(); });
}

void RevenueMetricCard::layoutOverlayButtons()
{
    const QRect viewport = m_chart->viewport()->rect();
    if (viewport.isEmpty())
        return;
    // 与 renderAmounts 的行位保持一致
    auto *primary = m_days == 7 ? m_button7 : m_button30;
    auto *secondary = m_days == 7 ? m_button30 : m_button7;
    primary->setGeometry(0, kBandTopY, viewport.width(), kBandTopH);
    secondary->setGeometry(0, viewport.height() - kBandBottomH - 2,
                           viewport.width(), kBandBottomH);
    m_retryButton->setGeometry(viewport.width() - 70, 2, 68, 24);
}

void RevenueMetricCard::updateAccessibleText()
{
    const QString amount7 = ev::formatYuanCents(m_stats.revenueCents);
    const QString amount30 = ev::formatYuanCents(m_stats.revenue30dCents);
    const auto describe = [this](int days, const QString &amount) {
        const bool selected = m_days == days;
        return QStringLiteral("近 %1 日营收 %2%3")
            .arg(days)
            .arg(amount, selected ? QStringLiteral(", 当前选中")
                                  : QStringLiteral(", 点击切换"));
    };
    const QString name7 = describe(7, amount7);
    const QString name30 = describe(30, amount30);
    m_button7->setAccessibleName(name7);
    m_button30->setAccessibleName(name30);
    m_button7->setToolTip(name7);
    m_button30->setToolTip(name30);
    m_detailsButton->setAccessibleName(
        QStringLiteral("查看销售业绩(当前近 %1 日)").arg(m_days));
}

void RevenueMetricCard::updateChart()
{
    const bool seven = m_days == 7;
    const ev::RevenueSeries &series =
        seven ? m_stats.revenue7dSeries : m_stats.revenue30dSeries;
    const bool unavailable = !series.available;
    if (!m_hasStats || unavailable) {
        m_chart->clearSeries();
        m_retryButton->setVisible(m_hasStats && unavailable); // 摘要可用时给重试入口
    } else {
        m_chart->setSeries(series);
        m_retryButton->setVisible(false);
    }
}

void RevenueMetricCard::renderAmounts(QPainter *painter, const QRectF &rect)
{
    if (!m_hasStats)
        return;
    const qreal width = rect.width();
    const qreal rowTopX = rect.left() + 6.0;
    const qreal rowMaxWidth = width - 12.0;

    const auto drawRow = [&](bool isSeven, qreal amountSize, qreal alpha,
                             const QColor &color, const QRectF &band) {
        const QString tag = isSeven ? QStringLiteral("近 7 日")
                                    : QStringLiteral("近 30 日");
        const QString amount =
            isSeven ? ev::formatYuanCents(m_stats.revenueCents)
                    : ev::formatYuanCents(m_stats.revenue30dCents);

        QFont tagFont = font();
        tagFont.setPixelSize(qRound(kTagSize));
        const QFontMetricsF tagMetrics(tagFont);
        const qreal tagWidth = tagMetrics.horizontalAdvance(tag);

        // 金额字号在行宽内自适应(完整精确金额不截断; 不影响 widget 布局尺寸)
        QFont amountFont = font();
        qreal fitted = amountSize;
        amountFont.setPixelSize(qRound(fitted));
        QFontMetricsF amountMetrics(amountFont);
        while (fitted > kSmallSize
               && tagWidth + 6.0 + amountMetrics.horizontalAdvance(amount)
                      > rowMaxWidth) {
            fitted -= 1.0;
            amountFont.setPixelSize(qRound(fitted));
            amountMetrics = QFontMetricsF(amountFont);
        }

        const qreal centerY = band.center().y();
        QColor tagColor = ev::theme::kDayMutedText;
        tagColor.setAlphaF(kSubAlpha);
        painter->setFont(tagFont);
        painter->setPen(tagColor);
        painter->drawText(
            QPointF(rowTopX, centerY + (tagMetrics.ascent() - tagMetrics.descent()) / 2.0),
            tag);

        QColor amountColor = color;
        amountColor.setAlphaF(alpha);
        painter->setFont(amountFont);
        painter->setPen(amountColor);
        painter->drawText(
            QPointF(rowTopX + tagWidth + 6.0,
                    centerY + (amountMetrics.ascent() - amountMetrics.descent()) / 2.0),
            amount);
    };

    const QRectF primaryBand(rect.left(), rect.top() + kBandTopY,
                             rect.width(), kBandTopH);
    const QRectF secondaryBand(rect.left(), rect.bottom() - kBandBottomH - 2.0,
                               rect.width(), kBandBottomH);
    drawRow(m_days == 7, kBigSize, kMainAlpha, ev::theme::kDayText, primaryBand);
    drawRow(m_days != 7, kSmallSize, kSubAlpha, ev::theme::kDayMutedText,
            secondaryBand);

    // 选中序列不可用时的卡内提示(金额行照常展示摘要, 不把坏序列画成 0)
    if (m_retryButton->isVisible()) {
        QFont hintFont = font();
        hintFont.setPixelSize(12);
        const QFontMetricsF hintMetrics(hintFont);
        QColor hintColor = ev::theme::kDayMutedText;
        hintColor.setAlphaF(kSubAlpha);
        painter->setFont(hintFont);
        painter->setPen(hintColor);
        const QString hint = QStringLiteral("趋势暂不可用");
        painter->drawText(
            QPointF(rowTopX, rect.top() + rect.height() * 0.5
                + (hintMetrics.ascent() - hintMetrics.descent()) / 2.0),
            hint);
    }
}
