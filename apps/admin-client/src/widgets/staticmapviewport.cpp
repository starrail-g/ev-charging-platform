#include "widgets/staticmapviewport.h"

#include <QtCore/qhashfunctions.h>

#include <cmath>
#include <limits>

namespace ev {
namespace {

// π（避免 M_PI 的平台差异）
constexpr double kPi = 3.141592653589793238462643383279502884;

// W = 256·2^zoom —— 世界像素总宽（zoom ∈ [10,17]，ldexp 精确，无 pow 舍入）
double worldPixelWidth(int zoom)
{
    return std::ldexp(256.0, zoom);
}

bool isFiniteLatLng(const MapLatLng &p)
{
    return std::isfinite(p.lat) && std::isfinite(p.lng)
        && p.lat >= -90.0 && p.lat <= 90.0
        && p.lng >= -180.0 && p.lng <= 180.0;
}

// D7：y_world(φ) = W/2·(1 − asinh(tan(πφ/180))/π)
double worldPixelY(double latDeg, int zoom)
{
    const double phi = latDeg * kPi / 180.0;
    const double half = worldPixelWidth(zoom) * 0.5;
    return half * (1.0 - std::asinh(std::tan(phi)) / kPi);
}

// 经度跨度像素：Δlng·W/360
double longitudeSpanPx(double spanDeg, int zoom)
{
    return spanDeg * worldPixelWidth(zoom) / 360.0;
}

// 纬度跨度像素（精确式；y_world 随纬度单调，直接取两端差值绝对值）
double latitudeSpanPx(double minLat, double maxLat, int zoom)
{
    return std::fabs(worldPixelY(maxLat, zoom) - worldPixelY(minLat, zoom));
}

} // namespace

bool computeMapView(const QVector<MapLatLng> &stations, QSize size, StaticMapView *view)
{
    const StaticMapView invalid; // 默认无效值
    if (view)
        *view = invalid;

    // 尺寸边界检查先于一切（§3.5：不投影、不请求，等待后续有效尺寸）
    if (size.width() <= 0 || size.height() <= 0)
        return false;

    // 业务净化：剔除非有限 / 纬度越 [-90,90] / 经度越 [-180,180] 的站（§3.5）
    QVector<MapLatLng> valid;
    valid.reserve(stations.size());
    for (const MapLatLng &p : stations) {
        if (isFiniteLatLng(p))
            valid.append(p);
    }
    if (valid.isEmpty())
        return false;

    // 投影范围独立检查（§3.5：业务合法的极区站不静默剔除——整体不可取景）
    for (const MapLatLng &p : valid) {
        if (std::fabs(p.lat) > kWebMercatorMaxLat)
            return false;
    }

    double minLat = valid.first().lat;
    double maxLat = valid.first().lat;
    double minLng = valid.first().lng;
    double maxLng = valid.first().lng;
    for (const MapLatLng &p : valid) {
        minLat = std::min(minLat, p.lat);
        maxLat = std::max(maxLat, p.lat);
        minLng = std::min(minLng, p.lng);
        maxLng = std::max(maxLng, p.lng);
    }
    const double spanLat = maxLat - minLat;
    const double spanLng = maxLng - minLng;

    if (view) {
        view->centerLat = (minLat + maxLat) * 0.5;
        view->centerLng = (minLng + maxLng) * 0.5;
        view->width = size.width();
        view->height = size.height();
    }

    // 两轴全零跨度（单站 / 同坐标站集）→ 单站规则 zoom=14（§3.3）
    if (spanLat == 0.0 && spanLng == 0.0) {
        if (view)
            view->zoom = 14;
        return true;
    }

    // 最大整数 zoom ∈ [10,17] 使 bbox 各非零跨度轴 px × 边距系数 ≤ 请求尺寸对应轴；
    // 单轴零跨度 → 该轴不参与约束（不退化单站）。找不到（需 zoom<10）→ false。
    // 从大到小找首个满足者即最大适配 zoom（px 随 zoom 单调增）。
    int zoom = -1;
    for (int z = kMapZoomMax; z >= kMapZoomMin; --z) {
        bool fits = true;
        if (spanLng > 0.0 && longitudeSpanPx(spanLng, z) * kMapViewMarginFactor > size.width())
            fits = false;
        if (spanLat > 0.0 && latitudeSpanPx(minLat, maxLat, z) * kMapViewMarginFactor > size.height())
            fits = false;
        if (fits) {
            zoom = z;
            break;
        }
    }
    if (zoom < 0)
        return false;

    if (view)
        view->zoom = zoom;
    return true;
}

QPointF lonLatToWidgetPoint(const MapLatLng &p, const StaticMapView &view)
{
    const double w = worldPixelWidth(view.zoom);
    const double x = view.width * 0.5 + (p.lng - view.centerLng) * w / 360.0;
    const double y = view.height * 0.5
        + worldPixelY(p.lat, view.zoom) - worldPixelY(view.centerLat, view.zoom);
    return QPointF(x, y);
}

quint64 requestFingerprint(const StaticMapView &view)
{
    // center 量化 1e-5 度（与 URL 精度同量级，防止 double 尾数抖动导致缓存不命中）
    const qint64 lat5 = qRound64(view.centerLat * 100000.0);
    const qint64 lng5 = qRound64(view.centerLng * 100000.0);
    // boost::hash_combine 式混入（Qt 6.2 无 qHashCombine，勿引入）
    auto mix = [](quint64 &seed, quint64 v) {
        seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    };
    quint64 seed = qHash(lat5);
    mix(seed, qHash(lng5));
    mix(seed, static_cast<quint64>(view.zoom));
    mix(seed, static_cast<quint64>(view.width));
    mix(seed, static_cast<quint64>(view.height));
    return seed;
}

} // namespace ev
