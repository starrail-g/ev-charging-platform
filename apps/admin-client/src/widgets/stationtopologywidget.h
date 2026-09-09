#ifndef STATIONTOPOLOGYWIDGET_H
#define STATIONTOPOLOGYWIDGET_H

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QVector>
#include <QWidget>

#include "models/adminmodels.h"
#include "widgets/staticmapimageprovider.h" // MapImageProvider（含 Failure/回调类型）
#include "widgets/staticmapviewport.h"      // StaticMapView（值成员）

class StatusPulseWidget;
class QTimer;

// 站点态势（单控件双模式，docs/role-c-admin-map-renderer-plan.md §3.1/§3.4）：
//  拓扑模式 —— 现状归一化示意（无 provider / 无凭据 / 取景失败 / 拉图失败 /
//               超时 / 在途 / 净化后无站）。布局与绘制一字不改。
//  真图模式 —— 底图 = 腾讯静态图 PNG（经 MapImageProvider 注入获取），
//               节点/状态色/站名/呼吸 halo/键盘/点击/focus ring 全复用，
//               仅 m_points 换用 §1 D7 Web-Mercator 投影（lonLatToWidgetPoint）。
//  降级标注 —— 曾尝试真图但失败/不可取景 → 落回拓扑并在图例区显示标注；
//               失败后 30s 时间门内不重试（可注入时钟测试）。
// 净化（§3.5）：setStations 入口剔除坐标非法站（非有限/越业务范围），
// 拓扑布局、取景、stationCount、点击/键盘都只消费净化后列表。
// 真图模式不画背景网格与站间虚线，隐藏拓扑图例文案。
class StationTopologyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StationTopologyWidget(QWidget *parent = nullptr);
    ~StationTopologyWidget() override;

    void setStations(const QList<ev::StationInfo> &stations);
    void setPiles(const QList<ev::PileInfo> &piles);
    void setMotionEnabled(bool enabled);

    int stationCount() const { return m_stations.size(); }
    // 信息承载线颜色：生成令牌 kDayTopologyLine（测试断言用）
    QColor informationLineColor() const;

    // T3：注入底图提供者（所有权转移给本控件，内部 setParent(this)；重复调用
    // 替换并作废旧在途；nullptr = 移除）。接线点读环境变量后传入，
    // 控件自身不读 env、不自建 provider（计划 §3.2）。
    void setMapImageProvider(ev::MapImageProvider *provider);

    // 降级标注文案（单一来源：图例绘制与测试断言共用）
    static const QString kDegradedServiceNote;    // 拉图失败/超时/配额
    static const QString kDegradedProjectionNote; // 站纬度超地图投影范围
    static const QString kDegradedTooLargeNote;   // 跨度需 zoom<10（区域过大）

signals:
    // 用户激活（点击或键盘）某站点节点时发出站点 id
    void stationActivated(int stationId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
#ifdef QT_TESTLIB_LIB
public:
    // 测试辅助入口（仅测试构建编译，生产构建不暴露）
    void activateStationForTest(int stationId);
    enum class RenderMode { Topology, Map };
    RenderMode renderModeForTest() const { return m_mapAvailable ? RenderMode::Map : RenderMode::Topology; }
    QPointF mapPointForTest(int index);              // 当前布局点（按当前模式重建）
    QString mapDegradedNoteForTest() const { return m_degradedNote; }
    int fetchAttemptsForTest() const { return m_fetchAttemptCount; }
    QImage mapImageForTest() const { return m_mapImage; }
    void advanceClockForTest(qint64 ms);             // 假时钟推进（30s 时间门）
private:
#endif
    // 由当前数据与控件尺寸重建投影点（绘制/命中前调用）。
    // 真图模式 = D7 投影 + 图居中偏移；拓扑模式 = min/max 归一化（现状）。
    void rebuildLayout();
    // 站内桩最高关注状态（枚举版；无桩/非活跃站 → Unknown/Offline）
    ev::PileStatus stationTopStatus(int stationId) const;
    // 站内桩最高关注状态对应的日班语义色；无桩/非活跃站退回离线灰
    QColor colorForStation(int stationId) const;
    QString stationStatusText(int stationId) const;
    // 呼吸子组件：仅最高关注为 Charging/Fault 的站挂载并定位到节点中心
    void rebuildPulses();
    void layoutPulses();
    void moveFocus(int step);

    // ---- T3 真图触发/重试/竞态状态机（计划 §3.4/§3.5）----
    void evaluateMap(); // 触发点统一评估（数据到达/resize 桶变化后调用）
    void scheduleFetch();
    // 目标失效统一出口：作废在途 + 停止 debounce（防旧目标在 timer 到期后被重发）+
    // 清待发目标（评审 P1：视图失效必须同时停掉挂起的防抖请求）
    void discardPendingMapFetch();
    void onFetchResult(int seq, bool ok, const QImage &image,
                       ev::MapImageProvider::Failure failure);
    qint64 nowMs() const;
    QSize requestBucketSize() const; // 请求图尺寸：32px 向下桶化 + clamp
    bool stationsOutOfProjectionRange() const; // 任一净化后站 |lat|>85.05112878

    // 时间门/防抖常量
    static constexpr int kRetryGateMs = 30000; // 失败后重试时间门（§3.4 触发点 3）
    static constexpr int kDebounceMs = 300;    // 触发合并防抖
    static constexpr int kBucketPx = 32;       // resize 尺寸桶（§3.4 触发点 2）

    QList<ev::StationInfo> m_stations; // 净化后列表（setStations 入口剔除非法坐标站）
    QList<ev::PileInfo> m_piles;
    QVector<QPointF> m_points; // 与 m_stations 对齐（NaN 占位 = 不可绘制点，防御越界）
    QRectF m_plotRect;

    QList<StatusPulseWidget *> m_pulses;        // 与 m_pulseStationIndexes 平行
    QList<int> m_pulseStationIndexes;           // 对应 m_stations 下标
    bool m_motionEnabled = true;
    int m_focusIndex = -1;                      // 键盘焦点所在站下标；-1 = 无

    // ---- 真图状态（§3.4）----
    ev::MapImageProvider *m_mapProvider = nullptr; // 拥有（child），可空
    bool m_mapAvailable = false;                   // 成功底图可用（当前目标匹配）
    QImage m_mapImage;                             // 成功缓存图（仅指纹完全匹配才复用）
    ev::StaticMapView m_mapView;                   // 底图对应目标视图
    quint64 m_mapFingerprint = 0;                  // 成功缓存视图指纹
    quint64 m_pendingFingerprint = 0;              // 待发/在途目标指纹
    ev::StaticMapView m_pendingView;               // 待发/在途目标视图
    int m_fetchSeq = 0;                            // generation：目标失效立即 ++
    bool m_fetchInFlight = false;
    int m_fetchAttemptCount = 0;                   // 真正发起 fetch 次数（测试断言）
    qint64 m_lastFailureMs = -1;                   // 最近失败时刻（nowMs 域；-1=无失败）
    QString m_degradedNote;                        // 非空 → 拓扑模式 + 图例区标注
    QElapsedTimer m_clock;                         // 生产时钟（时间门）
    bool m_useFakeClock = false;                   // 测试注入假时钟
    qint64 m_fakeClockMs = 0;
    QTimer *m_debounceTimer = nullptr;             // 300ms 合并（目标稳定后再发）
    QSize m_lastBucketSize;                        // 最近一次 resize 桶（变化才评估）
};

#endif // STATIONTOPOLOGYWIDGET_H
