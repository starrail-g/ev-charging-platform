#pragma once

#include "map_types.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

namespace ev {

QString mapSourceText(MapSource source);
QString mapErrorCategoryText(MapErrorCategory category);
bool isValidCoordinate(const GeoCoordinate &coordinate);
QString matchingBusinessStationId(const MapPoi &poi, const QVector<Station> &stations);

using GeoCallback = std::function<void(const MapResult<GeoCoordinate> &)>;
using PoiCallback = std::function<void(const MapResult<QVector<MapPoi>> &)>;
using PoiDetailCallback = std::function<void(const MapResult<MapPoi> &)>;
using RouteCallback = std::function<void(const MapResult<MapRoute> &)>;

class IMapService {
public:
  virtual ~IMapService() = default;
  virtual void setUserId(const QString &) {}
  virtual void setTargetStationId(const QString &) {}
  virtual void cancelPending() {}
  virtual void geocode(const QString &address, GeoCallback callback) = 0;
  virtual void searchNearbyChargingStations(const GeoCoordinate &center, int radiusMeters, PoiCallback callback) = 0;
  virtual void getPoiDetail(const QString &poiId, PoiDetailCallback callback) = 0;
  virtual void queryRoute(const GeoCoordinate &origin, const GeoCoordinate &destination, RouteMode mode, RouteCallback callback) = 0;
};

class MockMapService final : public QObject, public IMapService {
  Q_OBJECT
public:
  explicit MockMapService(QObject *parent = nullptr) : QObject(parent) {}
  void cancelPending() override { ++generation_; }
  void geocode(const QString &address, GeoCallback callback) override;
  void searchNearbyChargingStations(const GeoCoordinate &center, int radiusMeters, PoiCallback callback) override;
  void getPoiDetail(const QString &poiId, PoiDetailCallback callback) override;
  void queryRoute(const GeoCoordinate &origin, const GeoCoordinate &destination, RouteMode mode, RouteCallback callback) override;

private:
  quint64 generation_{0};
};

class ResilientMapService final : public QObject, public IMapService {
  Q_OBJECT
public:
  explicit ResilientMapService(IMapService *primary, IMapService *fallback, QObject *parent = nullptr);

  void setUserId(const QString &userId) override;
  void setTargetStationId(const QString &stationId) override;
  void cancelPending() override;
  void geocode(const QString &address, GeoCallback callback) override;
  void searchNearbyChargingStations(const GeoCoordinate &center, int radiusMeters, PoiCallback callback) override;
  void getPoiDetail(const QString &poiId, PoiDetailCallback callback) override;
  void queryRoute(const GeoCoordinate &origin, const GeoCoordinate &destination, RouteMode mode, RouteCallback callback) override;

private:
  static QString fallbackNotice(const MapError &error, bool emptyResult = false);

  IMapService *primary_{};
  IMapService *fallback_{};
};

} // namespace ev
