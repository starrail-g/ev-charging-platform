#ifndef STATIONTOPOLOGYWIDGET_H
#define STATIONTOPOLOGYWIDGET_H

#include <QColor>
#include <QList>
#include <QPointF>
#include <QVector>
#include <QWidget>

#include "models/adminmodels.h"

// 站点态势示意（轻量拓扑图，统一 UI Task 5）：
// 将站点经纬度按当前数据边界等比归一化投影到内容矩形，站点间以细连线表达
// "电力脉络" 的网络感；节点颜色取站内桩的最高关注状态
// （故障 > 离线 > 充电中 > 已预约 > 空闲），使用生成的日班语义色。
// 约束（spec §4.3/§5.2）：
//   - 承载关系的连线与节点描边使用信息拓扑线色 kDayTopologyLine 或状态色；
//   - 装饰结构色只用于背景网格等非信息元素；
//   - 图例必须注明"态势示意，不代表物理电网连接"；
//   - 不绘制扩散圆等装饰动画（静态示意，呼吸仅由状态点组件承担）。
class StationTopologyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StationTopologyWidget(QWidget *parent = nullptr);

    void setStations(const QList<ev::StationInfo> &stations);
    void setPiles(const QList<ev::PileInfo> &piles);

    int stationCount() const { return m_stations.size(); }
    // 信息承载线颜色：生成令牌 kDayTopologyLine（测试断言用）
    QColor informationLineColor() const;

signals:
    // 用户激活（点击）某站点节点时发出站点 id
    void stationActivated(int stationId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
#ifdef QT_TESTLIB_LIB
public:
    // 测试辅助入口：命中节点后触发与真实点击相同的信号路径；
    // 仅在测试构建（定义了 QT_TESTLIB_LIB）下编译，生产构建不暴露。
    void activateStationForTest(int stationId);
private:
#endif
    // 由当前数据与控件尺寸重建投影点（站点少，每次绘制重算即可）
    void rebuildLayout();
    // 站内桩最高关注状态对应的日班语义色；无桩/非活跃站退回离线灰
    QColor colorForStation(int stationId) const;
    QString stationStatusText(int stationId) const;

    QList<ev::StationInfo> m_stations;
    QList<ev::PileInfo> m_piles;
    QVector<QPointF> m_points; // 与 m_stations 对齐（rebuildLayout 后有效）
    QRectF m_plotRect;
};

#endif // STATIONTOPOLOGYWIDGET_H
