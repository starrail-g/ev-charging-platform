#pragma once

#include "map_service.h"

#include <QFutureWatcher>
#include <QList>
#include <QObject>

namespace ev {

// Client-side adapter for the server-owned map contract. It never receives or
// stores a Tencent credential; all upstream calls happen inside B's server.
class ServerMapService final : public QObject, public IMapService {
  Q_OBJECT
public:
  explicit ServerMapService(QString host = {}, quint16 port = 0, int timeoutMs = 3000,
                            QObject *parent = nullptr);
  ~ServerMapService() override;

  void setUserId(const QString &userId) override { userId_ = userId.trimmed(); ++generation_; }
  void setTargetStationId(const QString &stationId) override { targetStationId_ = stationId.trimmed(); ++generation_; }
  void cancelPending() override;
  void geocode(const QString &address, GeoCallback callback) override;
  void searchNearbyChargingStations(const GeoCoordinate &center, int radiusMeters, PoiCallback callback) override;
  void getPoiDetail(const QString &poiId, PoiDetailCallback callback) override;
  void queryRoute(const GeoCoordinate &origin, const GeoCoordinate &destination, RouteMode mode, RouteCallback callback) override;

private:
  struct Reply {
    bool ok{false};
    QJsonObject payload;
    int code{0};
    QString error;
  };

  static Reply request(const QString &host, quint16 port, int timeoutMs,
                       const QString &type, const QJsonObject &payload);
  static MapError mapError(int code, const QString &message);
  static bool readResponseMetadata(const QJsonObject &payload, MapSource *source,
                                   QString *dataSource, MapWarning *warning);
  static bool readCoordinate(const QJsonObject &object, GeoCoordinate *coordinate);
  static QString wireId(const QString &id);
  void remember(QFutureWatcher<Reply> *watcher);

  QString host_;
  quint16 port_{};
  int timeoutMs_{3000};
  QString userId_;
  QString targetStationId_;
  QList<QFutureWatcher<Reply> *> watchers_;
  QVector<MapPoi> lastPois_;
  quint64 generation_{0};
};

} // namespace ev
