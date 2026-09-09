#ifndef REVENUEMETRICCARD_H
#define REVENUEMETRICCARD_H

#include <QFrame>

#include "models/adminmodels.h"

class QPainter;
class QToolButton;

namespace ev {
class RevenueChartWidget;
}

// 概览第四张营收融合卡(只服务营收卡, 不继承/改写通用 MetricCard):
//   - QFrame[panel=true], 对象名 revenueCard(承接原卡片样式与自动化定位);
//   - 标题稳定为"营收", 右上详情入口(revenueDetailsButton)携带当前范围;
//   - 图表区为 Mini RevenueChartWidget: 折线 + 轻量网格为低透明度背景层(居中聚焦带),
//     两行金额(近 7 日/近 30 日)由前景回调画在最上层(上方大字、下方小字，点击后交换内容);
//   - 两个透明热区(revenue7dButton/revenue30dButton)覆盖各行, 承担点击/键盘/
//     tooltip/焦点环; 选中序列不可用时显示"趋势暂不可用"与重试入口;
//   - 仅点击切换范围，悬停不改变金额内容、字号或布局。
class RevenueMetricCard : public QFrame
{
    Q_OBJECT

public:
    explicit RevenueMetricCard(QWidget *parent = nullptr);

    void setStatistics(const ev::OverviewStats &stats);
    void setRange(int days);
    int selectedDays() const { return m_days; }
    void reset(); // 清统计与曲线/回 7 日/清提示

signals:
    void rangeChanged(int days);
    void detailsRequested(int days);
    void retryRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void renderAmounts(QPainter *painter, const QRectF &rect);
    void updateAccessibleText();
    void updateChart();
    void layoutOverlayButtons();

    int m_days = 7;
    bool m_hasStats = false;
    ev::OverviewStats m_stats;
    ev::RevenueChartWidget *m_chart = nullptr;
    QToolButton *m_button7 = nullptr;   // 近 7 日热区(透明, 无文字)
    QToolButton *m_button30 = nullptr;  // 近 30 日热区
    QToolButton *m_detailsButton = nullptr; // 标题行"详情"
    QToolButton *m_retryButton = nullptr;   // 趋势不可用时的重试(默认隐藏)
};

#endif // REVENUEMETRICCARD_H
