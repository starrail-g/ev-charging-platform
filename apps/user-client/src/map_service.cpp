#include "map_service.h"

#include <QTimer>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace ev {

QString mapSourceText(MapSource source) {
  switch (source) {
  case MapSource::Tencent: return QStringLiteral("真实腾讯地图");
  case MapSource::Server: return QStringLiteral("服务端地图");
  case MapSource::Mock: return QStringLiteral("Mock");
  case MapSource::Offline: return QStringLiteral("离线");
  }
  return QStringLiteral("未知");
}

QString mapErrorCategoryText(MapErrorCategory category) {
  switch (category) {
  case MapErrorCategory::InvalidInput: return QStringLiteral("输入无效");
  case MapErrorCategory::MissingKey: return QStringLiteral("Key 未配置");
  case MapErrorCategory::Timeout: return QStringLiteral("请求超时");
  case MapErrorCategory::Network: return QStringLiteral("网络失败");
  case MapErrorCategory::Api: return QStringLiteral("地图服务错误");
  case MapErrorCategory::Parse: return QStringLiteral("返回数据异常");
  }
  return QStringLiteral("未知错误");
}

bool isValidCoordinate(const GeoCoordinate &coordinate) {
  return std::isfinite(coordinate.latitude) && std::isfinite(coordinate.longitude) &&
         coordinate.latitude >= -90.0 && coordinate.latitude <= 90.0 &&
         coordinate.longitude >= -180.0 && coordinate.longitude <= 180.0;
}

QString matchingBusinessStationId(const MapPoi &poi, const QVector<Station> &stations) {
  const auto normalize = [](QString value) {
    value = value.simplified().toCaseFolded();
    value.remove(QRegularExpression(QStringLiteral("[\\s\\p{P}\\p{S}]+")));
    return value;
  };
  const QString poiTitle = normalize(poi.title);
  const QString poiAddress = normalize(poi.address);
  for (const auto &station : stations) {
    const bool sameName = !poiTitle.isEmpty() && poiTitle == normalize(station.name);
    const bool sameAddress = !poiAddress.isEmpty() && poiAddress == normalize(station.address);
    const GeoCoordinate stationCoordinate{station.latitude, station.longitude};
    const bool sameCoordinate = isValidCoordinate(poi.coordinate) && isValidCoordinate(stationCoordinate) &&
        std::abs(poi.coordinate.latitude - stationCoordinate.latitude) <= 0.0002 &&
        std::abs(poi.coordinate.longitude - stationCoordinate.longitude) <= 0.0002;
    if (sameName || sameAddress || sameCoordinate) return station.id;
  }
  return {};
}

void MockMapService::geocode(const QString &address, GeoCallback callback) {
  const QString value = address.trimmed();
  const quint64 generation = generation_;
  QTimer::singleShot(0, this, [this, generation, value, callback = std::move(callback)] {
    if (generation != generation_) return;
    if (value.isEmpty()) {
      callback(MapResult<GeoCoordinate>::failure({MapErrorCategory::InvalidInput, QStringLiteral("请输入地址或坐标"), false}));
      return;
    }
    if (value.contains(QStringLiteral("不存在")) || value.compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0) {
      callback(MapResult<GeoCoordinate>::failure({MapErrorCategory::Api, QStringLiteral("地址暂无定位结果"), true}));
      return;
    }
    callback(MapResult<GeoCoordinate>::success({22.5300, 113.9300}));
  });
}

void MockMapService::searchNearbyChargingStations(const GeoCoordinate &center, int radiusMeters, PoiCallback callback) {
  const quint64 generation = generation_;
  QTimer::singleShot(0, this, [this, generation, center, radiusMeters, callback = std::move(callback)] {
    if (generation != generation_) return;
    if (!isValidCoordinate(center) || radiusMeters <= 0) {
      callback(MapResult<QVector<MapPoi>>::failure({MapErrorCategory::InvalidInput, QStringLiteral("定位坐标或搜索半径无效"), false}));
      return;
    }
    QVector<MapPoi> result = {
        {QStringLiteral("mock-poi-s001"), QStringLiteral("科技园充电站"), QStringLiteral("科苑路 1 号"), {22.5401, 113.9345}, 1200, MapSource::Mock},
        {QStringLiteral("mock-poi-s002"), QStringLiteral("软件园南区"), QStringLiteral("学府大道 88 号"), {22.5268, 113.9432}, 3800, MapSource::Mock}};
    Q_UNUSED(center);
    result.erase(std::remove_if(result.begin(), result.end(), [radiusMeters](const MapPoi &poi) { return poi.distanceMeters > radiusMeters; }), result.end());
    callback(MapResult<QVector<MapPoi>>::success(result));
  });
}

void MockMapService::getPoiDetail(const QString &poiId, PoiDetailCallback callback) {
  const quint64 generation = generation_;
  QTimer::singleShot(0, this, [this, generation, poiId, callback = std::move(callback)] {
    if (generation != generation_) return;
    if (poiId == QStringLiteral("mock-poi-s001")) {
      callback(MapResult<MapPoi>::success({poiId, QStringLiteral("科技园充电站"), QStringLiteral("科苑路 1 号"), {22.5401, 113.9345}, 1200, MapSource::Mock}));
      return;
    }
    if (poiId == QStringLiteral("mock-poi-s002")) {
      callback(MapResult<MapPoi>::success({poiId, QStringLiteral("软件园南区"), QStringLiteral("学府大道 88 号"), {22.5268, 113.9432}, 3800, MapSource::Mock}));
      return;
    }
    callback(MapResult<MapPoi>::failure({MapErrorCategory::Api, QStringLiteral("地图 POI 不存在"), false}));
  });
}

void MockMapService::queryRoute(const GeoCoordinate &origin, const GeoCoordinate &destination, RouteMode mode, RouteCallback callback) {
  const quint64 generation = generation_;
  QTimer::singleShot(0, this, [this, generation, origin, destination, mode, callback = std::move(callback)] {
    if (generation != generation_) return;
    if (!isValidCoordinate(origin) || !isValidCoordinate(destination)) {
      callback(MapResult<MapRoute>::failure({MapErrorCategory::InvalidInput, QStringLiteral("起点或终点坐标无效"), false}));
      return;
    }
    const bool driving = mode == RouteMode::Driving;
    const double distance = std::hypot(destination.latitude - origin.latitude, destination.longitude - origin.longitude) * 111.0 * (driving ? 1.08 : 1.25);
    MapRoute route;
    route.mode = mode;
    route.distanceMeters = qRound64(distance * 1000.0);
    route.durationSeconds = qMax(60, qRound(distance * (driving ? 180.0 : 900.0)));
    route.source = MapSource::Offline;
    route.summary = QStringLiteral("离线 Mock %1 路线").arg(driving ? QStringLiteral("驾车") : QStringLiteral("步行"));
    route.polyline = {origin, destination};
    callback(MapResult<MapRoute>::success(route));
  });
}

ResilientMapService::ResilientMapService(IMapService *primary, IMapService *fallback, QObject *parent)
    : QObject(parent), primary_(primary), fallback_(fallback) {
  Q_ASSERT(primary_);
  Q_ASSERT(fallback_);
}

void ResilientMapService::setUserId(const QString &userId) {
  primary_->setUserId(userId);
  fallback_->setUserId(userId);
}

void ResilientMapService::setTargetStationId(const QString &stationId) {
  primary_->setTargetStationId(stationId);
  fallback_->setTargetStationId(stationId);
}

void ResilientMapService::cancelPending() {
  primary_->cancelPending();
  fallback_->cancelPending();
}

QString ResilientMapService::fallbackNotice(const MapError &error, bool emptyResult) {
  if (emptyResult) return QStringLiteral("真实地图返回空结果，已切换 Mock/离线数据");
  return QStringLiteral("%1，已切换 Mock/离线模式").arg(error.userMessage.isEmpty()
      ? mapErrorCategoryText(error.category) : error.userMessage);
}

void ResilientMapService::geocode(const QString &address, GeoCallback callback) {
  primary_->geocode(address, [this, address, callback = std::move(callback)](const MapResult<GeoCoordinate> &primaryResult) mutable {
    if (primaryResult.ok) { callback(primaryResult); return; }
    fallback_->geocode(address, [primaryResult, callback = std::move(callback)](MapResult<GeoCoordinate> fallbackResult) mutable {
      if (!fallbackResult.ok) { callback(primaryResult); return; }
      fallbackResult.notice = fallbackNotice(primaryResult.error);
      callback(fallbackResult);
    });
  });
}

void ResilientMapService::searchNearbyChargingStations(const GeoCoordinate &center, int radiusMeters, PoiCallback callback) {
  primary_->searchNearbyChargingStations(center, radiusMeters,
      [this, center, radiusMeters, callback = std::move(callback)](const MapResult<QVector<MapPoi>> &primaryResult) mutable {
    if (primaryResult.ok && !primaryResult.value.isEmpty()) { callback(primaryResult); return; }
    fallback_->searchNearbyChargingStations(center, qMax(radiusMeters, 5000),
        [primaryResult, callback = std::move(callback)](MapResult<QVector<MapPoi>> fallbackResult) mutable {
      if (!fallbackResult.ok) { callback(primaryResult); return; }
      fallbackResult.notice = fallbackNotice(primaryResult.error, primaryResult.ok);
      callback(fallbackResult);
    });
  });
}

void ResilientMapService::getPoiDetail(const QString &poiId, PoiDetailCallback callback) {
  primary_->getPoiDetail(poiId, [this, poiId, callback = std::move(callback)](const MapResult<MapPoi> &primaryResult) mutable {
    if (primaryResult.ok) { callback(primaryResult); return; }
    fallback_->getPoiDetail(poiId, [primaryResult, callback = std::move(callback)](MapResult<MapPoi> fallbackResult) mutable {
      if (!fallbackResult.ok) { callback(primaryResult); return; }
      fallbackResult.notice = fallbackNotice(primaryResult.error);
      callback(fallbackResult);
    });
  });
}

void ResilientMapService::queryRoute(const GeoCoordinate &origin, const GeoCoordinate &destination,
                                     RouteMode mode, RouteCallback callback) {
  primary_->queryRoute(origin, destination, mode,
      [this, origin, destination, mode, callback = std::move(callback)](const MapResult<MapRoute> &primaryResult) mutable {
    if (primaryResult.ok) { callback(primaryResult); return; }
    fallback_->queryRoute(origin, destination, mode,
        [primaryResult, callback = std::move(callback)](MapResult<MapRoute> fallbackResult) mutable {
      if (!fallbackResult.ok) { callback(primaryResult); return; }
      fallbackResult.notice = fallbackNotice(primaryResult.error);
      callback(fallbackResult);
    });
  });
}

} // namespace ev
