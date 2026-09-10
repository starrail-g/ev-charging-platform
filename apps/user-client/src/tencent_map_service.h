#pragma once

#include "map_service.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>

namespace ev {

class TencentMapService final : public QObject, public IMapService {
  Q_OBJECT
public:
  explicit TencentMapService(QObject *parent = nullptr, const QString &baseUrl = {}, int timeoutMs = 10000,
                             const QString &localConfigPath = {});

  bool configured() const { return enabled_ && keyValid_; }
  QString configurationMessage() const;
  QString apiKeyForWebEngine() const { return apiKey_; }
  QString endpointBaseUrl() const { return baseUrl_.isEmpty() ? QStringLiteral("https://apis.map.qq.com") : baseUrl_; }

  void cancelPending() override;
  void geocode(const QString &address, GeoCallback callback) override;
  void searchNearbyChargingStations(const QString &address, int radiusMeters, PoiCallback callback) override;
  void searchNearbyChargingStations(const GeoCoordinate &center, int radiusMeters, PoiCallback callback) override;
  void getPoiDetail(const QString &poiId, PoiDetailCallback callback) override;
  void queryRoute(const GeoCoordinate &origin, const GeoCoordinate &destination, RouteMode mode, RouteCallback callback) override;
  void queryRouteFromAddress(const QString &originAddress, const GeoCoordinate &destination, RouteMode mode, RouteCallback callback) override;

private:
  using JsonCallback = std::function<void(const QJsonDocument &, int, const MapError &)>;
  void requestJson(const QString &path, const QUrlQuery &query, JsonCallback callback);
  QUrl endpoint(const QString &path) const;
  QString readLocalValue(const QString &name) const;
  static MapError error(MapErrorCategory category, const QString &message, bool retryable, int httpStatus = 0, int apiStatus = 0);

  QNetworkAccessManager manager_;
  QString apiKey_;
  QString baseUrl_;
  QString localConfigPath_;
  bool enabled_{false};
  bool keyValid_{false};
  int timeoutMs_{10000};
};

} // namespace ev
