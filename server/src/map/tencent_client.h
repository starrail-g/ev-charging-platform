#pragma once

#include "ev_database/database.h"
#include "ev_protocol/message.h"

#include <QJsonArray>
#include <QVector>

namespace ev::server::map {

struct RouteResult {
    qint64 distanceMeters = 0;
    qint64 durationSeconds = 0;
    QVector<QPair<double, double>> polyline;
};

class TencentClient {
public:
    virtual ~TencentClient() = default;
    virtual bool geocode(const QString &address, QJsonObject *origin,
                         ev::protocol::ErrorCode *code, QString *error) = 0;
    virtual bool searchStations(const QJsonObject &origin, qint64 radiusMeters,
                                QVector<ev::database::MapPoi> *pois,
                                ev::protocol::ErrorCode *code, QString *error) = 0;
    virtual bool planRoute(const QJsonObject &origin, const QJsonObject &destination,
                           const QString &mode, RouteResult *route,
                           ev::protocol::ErrorCode *code, QString *error) = 0;
};

// A deterministic provider adapter used by development and tests. It has the
// same validation and output boundary as the production Tencent adapter, but
// never needs a credential or network access.
class DeterministicTencentClient final : public TencentClient {
public:
    bool geocode(const QString &address, QJsonObject *origin,
                 ev::protocol::ErrorCode *code, QString *error) override;
    bool searchStations(const QJsonObject &origin, qint64 radiusMeters,
                        QVector<ev::database::MapPoi> *pois,
                        ev::protocol::ErrorCode *code, QString *error) override;
    bool planRoute(const QJsonObject &origin, const QJsonObject &destination,
                   const QString &mode, RouteResult *route,
                   ev::protocol::ErrorCode *code, QString *error) override;

private:
    static bool upstreamFailure(ev::protocol::ErrorCode *code, QString *error);
};

} // namespace ev::server::map
