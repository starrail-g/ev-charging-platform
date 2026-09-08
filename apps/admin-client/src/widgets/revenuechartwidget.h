#ifndef REVENUECHARTWIDGET_H
#define REVENUECHARTWIDGET_H

#include <QChartView>

#include <QRectF>

#include <functional>

#include "models/adminmodels.h"

class QCategoryAxis;
class QLineSeries;
class QPainter;
class QValueAxis;

namespace ev {

// 营收折线共用组件(QChartView + QChart + QLineSeries; 不发起网络请求, 数据来自
// 调用方传入的 ev::RevenueSeries)。
//
//   Mini — 概览融合卡(折线 = 低透明度背景层): 真实比例折线以 60% 透明前景蓝绘制,
//          轻量坐标网格(底轴/纵轴/3 条水平线)手绘在内容下层, 轴对象整体隐藏使
//          plotArea ≈ 视口(消除隐藏轴占位造成的折线内缩); Y 量程聚焦数据带(上下
//          15% 余量)让折线垂直居中。两行金额文字由宿主卡经 setForegroundPainter
//          注入、在 drawForeground 中画于最上层(数据可读优先); 点击/键盘/tooltip
//          由宿主卡的透明热区承担, Mini 不做逐点 hover。
//   Full — 销售业绩页: Y 轴自 0(真实零基线), 金额仅在绘图边界由分转元; 横轴为
//          UTC 儒略日数值(不受本机时区影响), 稀疏日期标签(7 日逐日 / 30 日约 6 个
//          含首尾); 悬停按 X 位置找回 RevenueDay, 用原整数分格式化精确金额
//          (不从浮点反算)。
//
// 非法/空序列由调用方先判 RevenueSeries.available 再 setSeries; setSeries 可重复
// 调用(旧标签全删再加、series 只此一条), clearSeries 一次清空数据/点/提示/回调。
// 注意: paint 内不得调用 viewport()->update()(会形成常驻重绘循环), 动画帧由宿主
// (QVariantAnimation::valueChanged)单点驱动 viewport 更新。
class RevenueChartWidget : public QChartView
{
    Q_OBJECT

public:
    enum class Mode { Mini, Full };

    explicit RevenueChartWidget(Mode mode, QWidget *parent = nullptr);

    void setSeries(const ev::RevenueSeries &series);
    void clearSeries();
    int pointCount() const;
    QString displayedRange() const; // 当前数据 range(7d/30d); 无数据为空串

    // Mini 前景金额文字回调(drawForeground 中绘制, 场景内容之上); rect 为本视图坐标
    void setForegroundPainter(
        std::function<void(QPainter *, const QRectF &)> painter);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    // 清空 X 分类轴全部 category(label 唯一后逐个 remove 安全); setSeries 重建
    // 与 clearSeries 共用同一出口, 避免轴状态残留
    void clearXAxisCategories();

    Mode m_mode;
    ev::RevenueSeries m_data;
    QLineSeries *m_line = nullptr;
    QCategoryAxis *m_xAxis = nullptr; // 儒略日数值位置 + 稀疏日期标签
    QValueAxis *m_yAxis = nullptr;
    std::function<void(QPainter *, const QRectF &)> m_foregroundPainter;
    qreal m_yMin = 0.0;   // 当前 Y 显示范围(手绘网格/绘制共用)
    qreal m_yMax = 1.0;
    bool m_hasData = false;
};

} // namespace ev

#endif // REVENUECHARTWIDGET_H
