#ifndef STATICMAPVIEWPORT_H
#define STATICMAPVIEWPORT_H

#include <QPointF>
#include <QSize>
#include <QVector>

// 静态图取景/投影纯函数（T1，docs/role-c-admin-map-renderer-plan.md §1 D7 / §3.3 / §3.5）。
// 无状态、无网络、不依赖模型/控件类型——只消费经纬度对与请求尺寸。
// 底图 = 腾讯静态图 WebService：图中心 == center 参数，图像 px 与世界 px 1:1。
namespace ev {

// 一个可上图坐标点（业务坐标，WGS/GCJ 未校准前不做 datum 补偿，见计划 D4）
struct MapLatLng {
    double lat = 0.0;   // 纬度（度）
    double lng = 0.0;   // 经度（度）
};

// 单次静态图请求的完整视图描述
struct StaticMapView {
    double centerLat = 0.0;  // 图中心纬度（度）
    double centerLng = 0.0;  // 图中心经度（度）
    int zoom = 10;           // 缩放级别，夹取范围见 kMapZoomMin/Max
    int width = 0;           // 请求图像宽（px），> 0
    int height = 0;          // 请求图像高（px），> 0
};

// Web-Mercator 地图投影纬度保护边界（±85.05112878°，本期的渲染保护范围，
// 不声称腾讯支持范围已实测；业务合法但超出该范围的站 → 不可取景，见计划 §3.5）
constexpr double kWebMercatorMaxLat = 85.05112878;

// 取景 zoom 夹取范围（计划 §3.3）
constexpr int kMapZoomMin = 10;
constexpr int kMapZoomMax = 17;

// 边距系数：(1 + 2 × 0.08)，bbox 两轴 px × 该系数 ≤ 请求尺寸对应轴（计划 §3.3）
constexpr double kMapViewMarginFactor = 1.16;

// 计算覆盖 stations 全部坐标的最小适配视图（全站必在图内，无裁剪设计）：
//  - 尺寸宽或高 ≤ 0                                 → false（先于一切，含单站分支）
//  - stations 空，或全部坐标非法（非有限 / 纬度 ∉ [-90,90] / 经度 ∉ [-180,180]）
//                                                  → false
//  - 混入非法坐标 → 剔除后按合法站取景（§3.5 净化语义，调用方展示计数自管）
//  - 任一保留站纬度超出 ±kWebMercatorMaxLat        → false（超出地图投影范围）
//  - 两轴全零跨度（单站 / 同坐标站集）              → center=该点、zoom=14（单站规则）
//  - 单轴零跨度 → 该轴不参与 zoom 约束（center 仍为 bbox 中心，不退化单站）
//  - 跨度需 zoom < 10 才适配（区域过大）            → false
// 失败时若 view 非空则写回默认无效值（调用方不得消费）。
bool computeMapView(const QVector<MapLatLng> &stations, QSize size, StaticMapView *view);

// D7 精确投影（图中心 == center 参数，左上原点、图像 y 向下为正）：
//   W = 256·2^zoom；x_img = width/2 + (lng − centerLng)·W/360
//   y_world(φ) = W/2·(1 − asinh(tan(πφ/180))/π)；y_img = height/2 + y_world(lat) − y_world(centerLat)
// 北侧点 y_img < 图中心 y，南侧点 y_img > 图中心 y。实现必须用精确式，
// 不得用导数 × Δ 近似（大跨度下两者偏差 > 1px，T1 单测锁）。
QPointF lonLatToWidgetPoint(const MapLatLng &p, const StaticMapView &view);

// 稳定指纹：center 量化到 1e-5 度、zoom/width/height 整数直用。
// 相同视图必返回相同值；任一字段跨量化变化必返回不同值（缓存匹配/请求去重 key）。
quint64 requestFingerprint(const StaticMapView &view);

} // namespace ev

#endif // STATICMAPVIEWPORT_H
