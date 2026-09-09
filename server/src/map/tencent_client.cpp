#include "tencent_client.h"

#include <QtMath>

#include <algorithm>

namespace ev::server::map {
namespace {

constexpr double kReferenceLatitude = 41.7192;
constexpr double kReferenceLongitude = 123.4315;
constexpr double kEarthRadiusMeters = 6371000.0;

double distanceMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double lat1Rad = qDegreesToRadians(lat1);
    const double lat2Rad = qDegreesToRadians(lat2);
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLon = qDegreesToRadians(lon2 - lon1);
    const double a = qSin(dLat / 2.0) * qSin(dLat / 2.0)
        + qCos(lat1Rad) * qCos(lat2Rad)
        * qSin(dLon / 2.0) * qSin(dLon / 2.0);
    return 2.0 * kEarthRadiusMeters * qAsin(qMin(1.0, qSqrt(a)));
}

} // namespace

bool DeterministicTencentClient::upstreamFailure(ev::protocol::ErrorCode *code,
                                                 QString *error)
{
    const QString mode = qEnvironmentVariable("EV_MAP_UPSTREAM_FAILURE").trimmed().toLower();
    if (mode.isEmpty()) return false;
    if (mode == QStringLiteral("timeout")) {
        if (code) *code = ev::protocol::ErrorCode::MapUpstreamTimeout;
        if (error) *error = QStringLiteral("Tencent map request timed out");
    } else if (mode == QStringLiteral("quota")) {
        if (code) *code = ev::protocol::ErrorCode::MapQuotaExceeded;
        if (error) *error = QStringLiteral("Tencent map quota exceeded");
    } else if (mode == QStringLiteral("permission")) {
        if (code) *code = ev::protocol::ErrorCode::MapPermissionDenied;
        if (error) *error = QStringLiteral("Tencent map permission denied");
    } else {
        if (code) *code = ev::protocol::ErrorCode::MapUpstreamUnavailable;
        if (error) *error = QStringLiteral("Tencent map service unavailable");
    }
    return true;
}

bool DeterministicTencentClient::geocode(const QString &address, QJsonObject *origin,
                                         ev::protocol::ErrorCode *code, QString *error)
{
    if (upstreamFailure(code, error)) return false;
    if (!origin || address.trimmed().isEmpty()) {
        if (code) *code = ev::protocol::ErrorCode::MapResponseInvalid;
        if (error) *error = QStringLiteral("geocoder returned no coordinate");
        return false;
    }
    // The fake provider intentionally resolves all development addresses to a
    // stable Shenyang coordinate. Tests can therefore reproduce cache keys and
    // route vectors without an external service.
    *origin = QJsonObject{{QStringLiteral("latitude"), kReferenceLatitude},
                          {QStringLiteral("longitude"), kReferenceLongitude}};
    return true;
}

bool DeterministicTencentClient::searchStations(const QJsonObject &origin,
                                                qint64 radiusMeters,
                                                QVector<ev::database::MapPoi> *pois,
                                                ev::protocol::ErrorCode *code,
                                                QString *error)
{
    if (upstreamFailure(code, error)) return false;
    if (!pois || !origin.value(QStringLiteral("latitude")).isDouble()
        || !origin.value(QStringLiteral("longitude")).isDouble()) {
        if (code) *code = ev::protocol::ErrorCode::MapResponseInvalid;
        if (error) *error = QStringLiteral("POI response origin is invalid");
        return false;
    }
    const double latitude = origin.value(QStringLiteral("latitude")).toDouble();
    const double longitude = origin.value(QStringLiteral("longitude")).toDouble();
    struct Candidate { const char *id; const char *name; const char *address; double lat; double lon; };
    static constexpr Candidate candidates[] = {
        {"poi-001", "示例充电站一", "浑南软件园示例路1号", 41.7210, 123.4350},
        {"poi-002", "示例充电站二", "浑南科技城示例路2号", 41.7150, 123.4300},
        {"poi-003", "示例充电站三", "浑南新区示例路3号", 41.7300, 123.4500}
    };
    *pois = QVector<ev::database::MapPoi>();
    for (const Candidate &candidate : candidates) {
        const qint64 distance = qRound64(distanceMeters(latitude, longitude,
                                                        candidate.lat, candidate.lon));
        if (distance <= radiusMeters) {
            pois->append(ev::database::MapPoi{QString::fromLatin1(candidate.id),
                                              QString::fromUtf8(candidate.name),
                                              QString::fromUtf8(candidate.address),
                                              candidate.lat, candidate.lon, distance});
        }
    }
    std::sort(pois->begin(), pois->end(), [](const ev::database::MapPoi &left,
                                             const ev::database::MapPoi &right) {
        if (left.distanceMeters != right.distanceMeters)
            return left.distanceMeters < right.distanceMeters;
        return left.providerPoiId < right.providerPoiId;
    });
    if (pois->isEmpty()) {
        if (code) *code = ev::protocol::ErrorCode::MapNoResult;
        if (error) *error = QStringLiteral("附近没有充电站");
        return false;
    }
    return true;
}

bool DeterministicTencentClient::planRoute(const QJsonObject &origin,
                                           const QJsonObject &destination,
                                           const QString &mode, RouteResult *route,
                                           ev::protocol::ErrorCode *code,
                                           QString *error)
{
    if (upstreamFailure(code, error)) return false;
    if (!route || (mode != QStringLiteral("driving") && mode != QStringLiteral("walking"))) {
        if (code) *code = ev::protocol::ErrorCode::MapResponseInvalid;
        if (error) *error = QStringLiteral("route mode is invalid");
        return false;
    }
    const double originLat = origin.value(QStringLiteral("latitude")).toDouble();
    const double originLon = origin.value(QStringLiteral("longitude")).toDouble();
    const double destinationLat = destination.value(QStringLiteral("latitude")).toDouble();
    const double destinationLon = destination.value(QStringLiteral("longitude")).toDouble();
    if (!qIsFinite(originLat) || !qIsFinite(originLon) || !qIsFinite(destinationLat)
        || !qIsFinite(destinationLon)) {
        if (code) *code = ev::protocol::ErrorCode::MapResponseInvalid;
        if (error) *error = QStringLiteral("route coordinates are invalid");
        return false;
    }
    route->distanceMeters = qRound64(distanceMeters(originLat, originLon,
                                                    destinationLat, destinationLon));
    const double speedMetersPerSecond = mode == QStringLiteral("walking") ? 1.4 : 9.7;
    route->durationSeconds = qMax<qint64>(1, qCeil(route->distanceMeters / speedMetersPerSecond));
    route->polyline = {{originLat, originLon}, {destinationLat, destinationLon}};
    return true;
}

} // namespace ev::server::map
