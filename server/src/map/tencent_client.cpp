#include "tencent_client.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

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

namespace {

constexpr int kDefaultTimeoutMs = 5000;
constexpr int kMaxResponseBytes = 4 * 1024 * 1024;
constexpr int kMaxPolylinePoints = 4096;
constexpr int kMaxEncodedPolylineValues = 200000;

bool validCoordinate(double latitude, double longitude)
{
    return qIsFinite(latitude) && qIsFinite(longitude)
        && latitude >= -90.0 && latitude <= 90.0
        && longitude >= -180.0 && longitude <= 180.0;
}

QString coordinatePair(const QJsonObject &coordinate)
{
    return QString::number(coordinate.value(QStringLiteral("latitude")).toDouble(), 'f', 6)
        + QLatin1Char(',')
        + QString::number(coordinate.value(QStringLiteral("longitude")).toDouble(), 'f', 6);
}

int timeoutMs()
{
    bool ok = false;
    const int value = qEnvironmentVariable("TENCENT_MAP_TIMEOUT_MS").toInt(&ok);
    return ok ? qBound(500, value, 30000) : kDefaultTimeoutMs;
}

QUrl baseUrl()
{
    const QString configured = qEnvironmentVariable("TENCENT_MAP_BASE_URL").trimmed();
    if (!configured.isEmpty()) return QUrl::fromUserInput(configured);
    return QUrl(QStringLiteral("https://apis.map.qq.com"));
}

bool allowedBaseUrl(const QUrl &url, bool configured)
{
    if (!configured) return url.scheme() == QStringLiteral("https")
        && url.host() == QStringLiteral("apis.map.qq.com");
    if (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        return false;
    const QString host = url.host().toLower();
    return host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1")
        || host == QStringLiteral("::1");
}

ev::protocol::ErrorCode statusFailure(int httpStatus, int providerStatus,
                                      const QString &providerMessage)
{
    if (httpStatus == 401 || httpStatus == 403) return ev::protocol::ErrorCode::MapPermissionDenied;
    if (httpStatus == 408) return ev::protocol::ErrorCode::MapUpstreamTimeout;
    if (httpStatus == 429) return ev::protocol::ErrorCode::MapQuotaExceeded;
    if (httpStatus >= 500) return ev::protocol::ErrorCode::MapUpstreamUnavailable;
    const QString message = providerMessage.toLower();
    if (message.contains(QStringLiteral("quota")) || message.contains(QStringLiteral("limit"))
        || message.contains(QStringLiteral("配额")) || message.contains(QStringLiteral("频率"))
        || message.contains(QStringLiteral("调用量")) || message.contains(QStringLiteral("上限"))
        || providerStatus == 121)
        return ev::protocol::ErrorCode::MapQuotaExceeded;
    if (message.contains(QStringLiteral("key")) || message.contains(QStringLiteral("permission"))
        || message.contains(QStringLiteral("auth")) || message.contains(QStringLiteral("权限"))
        || providerStatus == 311 || providerStatus == 312 || providerStatus == 313)
        return ev::protocol::ErrorCode::MapPermissionDenied;
    if (providerStatus == 110 || providerStatus == 111) return ev::protocol::ErrorCode::MapNoResult;
    return ev::protocol::ErrorCode::MapUpstreamUnavailable;
}

QString endpointError(QNetworkReply::NetworkError networkError)
{
    return networkError == QNetworkReply::TimeoutError
        ? QStringLiteral("Tencent map request timed out")
        : QStringLiteral("Tencent map service unavailable");
}

} // namespace

void HttpTencentClient::setFailure(ev::protocol::ErrorCode *code, QString *error,
                                   ev::protocol::ErrorCode failureCode,
                                   const QString &message)
{
    if (code) *code = failureCode;
    if (error) *error = message;
}

bool HttpTencentClient::parseCoordinate(const QJsonValue &value, double minimum,
                                        double maximum, double *result)
{
    if (!result || !value.isDouble()) return false;
    const double number = value.toDouble();
    if (!qIsFinite(number) || number < minimum || number > maximum) return false;
    *result = number;
    return true;
}

bool HttpTencentClient::get(const QString &path, const QUrlQuery &query, QJsonObject *body,
                            ev::protocol::ErrorCode *code, QString *error)
{
    if (!body) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("Tencent response is invalid"));
        return false;
    }
    const bool configuredBase = !qEnvironmentVariable("TENCENT_MAP_BASE_URL").trimmed().isEmpty();
    const QUrl configuredUrl = baseUrl();
    QUrl url = configuredUrl.resolved(QUrl(path));
    if (!url.isValid() || !allowedBaseUrl(configuredUrl, configuredBase)
        || url.host() != configuredUrl.host() || url.port() != configuredUrl.port()) {
        setFailure(code, error, ev::protocol::ErrorCode::MapUpstreamUnavailable,
                   QStringLiteral("Tencent map endpoint is invalid"));
        return false;
    }
    QUrlQuery requestQuery = query;
    requestQuery.addQueryItem(QStringLiteral("key"), qEnvironmentVariable("TENCENT_MAP_KEY"));
    url.setQuery(requestQuery);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ev-charging-platform/1.0"));
    QNetworkReply *reply = networkManager_.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    bool tooLarge = false;
    QByteArray responseBytes;
    responseBytes.reserve(64 * 1024);
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        if (!reply->isOpen()) return;
        responseBytes.append(reply->readAll());
        if (responseBytes.size() > kMaxResponseBytes) {
            tooLarge = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
    });
    timer.start(timeoutMs());
    loop.exec();
    timer.stop();

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    if (timedOut || networkError == QNetworkReply::TimeoutError) {
        reply->deleteLater();
        setFailure(code, error, ev::protocol::ErrorCode::MapUpstreamTimeout,
                   QStringLiteral("Tencent map request timed out"));
        return false;
    }
    if (reply->isOpen()) responseBytes.append(reply->readAll());
    reply->deleteLater();
    if (tooLarge || responseBytes.size() > kMaxResponseBytes) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("Tencent response is too large"));
        return false;
    }
    if (networkError != QNetworkReply::NoError && httpStatus == 0) {
        setFailure(code, error, ev::protocol::ErrorCode::MapUpstreamUnavailable,
                   endpointError(networkError));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (httpStatus == 401 || httpStatus == 403)
            setFailure(code, error, ev::protocol::ErrorCode::MapPermissionDenied,
                       QStringLiteral("Tencent map permission denied"));
        else if (httpStatus == 429)
            setFailure(code, error, ev::protocol::ErrorCode::MapQuotaExceeded,
                       QStringLiteral("Tencent map quota exceeded"));
        else if (httpStatus >= 500)
            setFailure(code, error, ev::protocol::ErrorCode::MapUpstreamUnavailable,
                       QStringLiteral("Tencent map service unavailable"));
        else
            setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                       QStringLiteral("Tencent response is invalid"));
        return false;
    }
    const QJsonObject object = document.object();
    const int providerStatus = object.value(QStringLiteral("status")).toInt(-1);
    const QString providerMessage = object.value(QStringLiteral("message")).toString();
    if (httpStatus < 200 || httpStatus >= 300 || providerStatus != 0) {
        const ev::protocol::ErrorCode mapped = statusFailure(httpStatus, providerStatus,
                                                              providerMessage);
        setFailure(code, error, mapped,
                   mapped == ev::protocol::ErrorCode::MapNoResult
                       ? QStringLiteral("附近没有充电站")
                       : mapped == ev::protocol::ErrorCode::MapQuotaExceeded
                           ? QStringLiteral("Tencent map quota exceeded")
                           : mapped == ev::protocol::ErrorCode::MapPermissionDenied
                               ? QStringLiteral("Tencent map permission denied")
                               : mapped == ev::protocol::ErrorCode::MapUpstreamTimeout
                                   ? QStringLiteral("Tencent map request timed out")
                                   : QStringLiteral("Tencent map service unavailable"));
        return false;
    }
    *body = object;
    return true;
}

bool HttpTencentClient::geocode(const QString &address, QJsonObject *origin,
                                ev::protocol::ErrorCode *code, QString *error)
{
    if (!origin || address.trimmed().isEmpty()) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("geocoder address is invalid"));
        return false;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("address"), address);
    QJsonObject body;
    if (!get(QStringLiteral("/ws/geocoder/v1/"), query, &body, code, error)) return false;
    const QJsonObject location = body.value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("location")).toObject();
    double latitude = 0.0;
    double longitude = 0.0;
    if (!parseCoordinate(location.value(QStringLiteral("lat")), -90.0, 90.0, &latitude)
        || !parseCoordinate(location.value(QStringLiteral("lng")), -180.0, 180.0, &longitude)) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("geocoder returned an invalid coordinate"));
        return false;
    }
    *origin = QJsonObject{{QStringLiteral("latitude"), latitude},
                          {QStringLiteral("longitude"), longitude}};
    return true;
}

bool HttpTencentClient::searchStations(const QJsonObject &origin, qint64 radiusMeters,
                                       QVector<ev::database::MapPoi> *pois,
                                       ev::protocol::ErrorCode *code, QString *error)
{
    if (!pois) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("POI response is invalid"));
        return false;
    }
    double latitude = 0.0;
    double longitude = 0.0;
    if (!parseCoordinate(origin.value(QStringLiteral("latitude")), -90.0, 90.0, &latitude)
        || !parseCoordinate(origin.value(QStringLiteral("longitude")), -180.0, 180.0, &longitude)
        || radiusMeters < 1 || radiusMeters > 100000) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("POI search origin is invalid"));
        return false;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("boundary"),
                       QStringLiteral("nearby(%1,%2,%3)")
                           .arg(QString::number(latitude, 'f', 6))
                           .arg(QString::number(longitude, 'f', 6))
                           .arg(radiusMeters));
    query.addQueryItem(QStringLiteral("keyword"), QStringLiteral("充电站"));
    query.addQueryItem(QStringLiteral("page_size"), QStringLiteral("20"));
    query.addQueryItem(QStringLiteral("page_index"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("orderby"), QStringLiteral("_distance"));
    QJsonObject body;
    if (!get(QStringLiteral("/ws/place/v1/search"), query, &body, code, error)) return false;
    const QJsonValue dataValue = body.value(QStringLiteral("data"));
    if (!dataValue.isArray()) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("POI response is invalid"));
        return false;
    }
    QVector<ev::database::MapPoi> result;
    for (const QJsonValue &value : dataValue.toArray()) {
        const QJsonObject item = value.toObject();
        const QJsonObject location = item.value(QStringLiteral("location")).toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        const QString name = item.value(QStringLiteral("title")).toString();
        const QString address = item.value(QStringLiteral("address")).toString();
        double itemLatitude = 0.0;
        double itemLongitude = 0.0;
        if (id.isEmpty() || name.isEmpty()
            || !parseCoordinate(location.value(QStringLiteral("lat")), -90.0, 90.0, &itemLatitude)
            || !parseCoordinate(location.value(QStringLiteral("lng")), -180.0, 180.0, &itemLongitude))
            continue;
        qint64 distance = qRound64(distanceMeters(latitude, longitude,
                                                  itemLatitude, itemLongitude));
        if (item.value(QStringLiteral("distance")).isDouble()) {
            const double valueDistance = item.value(QStringLiteral("distance")).toDouble();
            if (qIsFinite(valueDistance) && valueDistance >= 0.0
                && valueDistance <= std::numeric_limits<qint64>::max())
                distance = qRound64(valueDistance);
        }
        if (distance <= radiusMeters)
            result.append(ev::database::MapPoi{id, name, address, itemLatitude,
                                               itemLongitude, distance});
        if (result.size() >= 100) break;
    }
    if (result.isEmpty()) {
        setFailure(code, error, ev::protocol::ErrorCode::MapNoResult,
                   QStringLiteral("附近没有充电站"));
        return false;
    }
    std::sort(result.begin(), result.end(), [](const ev::database::MapPoi &left,
                                               const ev::database::MapPoi &right) {
        if (left.distanceMeters != right.distanceMeters)
            return left.distanceMeters < right.distanceMeters;
        return left.providerPoiId < right.providerPoiId;
    });
    *pois = result;
    return true;
}

bool HttpTencentClient::decodePolyline(const QJsonArray &encoded,
                                       QVector<QPair<double, double>> *polyline)
{
    if (!polyline || encoded.size() < 2 || encoded.size() % 2 != 0
        || encoded.size() > kMaxEncodedPolylineValues) return false;
    QVector<QPair<double, double>> decoded;
    decoded.reserve(encoded.size() / 2);
    double latitude = 0.0;
    double longitude = 0.0;
    if (!parseCoordinate(encoded.at(0), -90.0, 90.0, &latitude)
        || !parseCoordinate(encoded.at(1), -180.0, 180.0, &longitude)) return false;
    decoded.append({latitude, longitude});
    for (int index = 2; index < encoded.size(); index += 2) {
        const QJsonValue latDeltaValue = encoded.at(index);
        const QJsonValue lonDeltaValue = encoded.at(index + 1);
        if (!latDeltaValue.isDouble() || !lonDeltaValue.isDouble()
            || !qIsFinite(latDeltaValue.toDouble()) || !qIsFinite(lonDeltaValue.toDouble()))
            return false;
        latitude += latDeltaValue.toDouble() / 1000000.0;
        longitude += lonDeltaValue.toDouble() / 1000000.0;
        if (!validCoordinate(latitude, longitude)) return false;
        decoded.append({latitude, longitude});
    }
    if (decoded.size() <= kMaxPolylinePoints) {
        *polyline = decoded;
        return true;
    }
    QVector<QPair<double, double>> sampled;
    sampled.reserve(kMaxPolylinePoints);
    for (int index = 0; index < kMaxPolylinePoints; ++index) {
        const int source = qRound64(static_cast<double>(index) * (decoded.size() - 1)
                                    / (kMaxPolylinePoints - 1));
        sampled.append(decoded.at(source));
    }
    *polyline = sampled;
    return true;
}

bool HttpTencentClient::planRoute(const QJsonObject &origin, const QJsonObject &destination,
                                  const QString &mode, RouteResult *route,
                                  ev::protocol::ErrorCode *code, QString *error)
{
    if (!route || (mode != QStringLiteral("driving") && mode != QStringLiteral("walking"))) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("route mode is invalid"));
        return false;
    }
    double originLatitude = 0.0;
    double originLongitude = 0.0;
    double destinationLatitude = 0.0;
    double destinationLongitude = 0.0;
    if (!parseCoordinate(origin.value(QStringLiteral("latitude")), -90.0, 90.0, &originLatitude)
        || !parseCoordinate(origin.value(QStringLiteral("longitude")), -180.0, 180.0, &originLongitude)
        || !parseCoordinate(destination.value(QStringLiteral("latitude")), -90.0, 90.0, &destinationLatitude)
        || !parseCoordinate(destination.value(QStringLiteral("longitude")), -180.0, 180.0, &destinationLongitude)) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("route coordinates are invalid"));
        return false;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from"), coordinatePair(origin));
    query.addQueryItem(QStringLiteral("to"), coordinatePair(destination));
    if (mode == QStringLiteral("driving")) {
        query.addQueryItem(QStringLiteral("policy"), QStringLiteral("LEAST_TIME"));
        query.addQueryItem(QStringLiteral("no_step"), QStringLiteral("1"));
    }
    QJsonObject body;
    if (!get(mode == QStringLiteral("walking") ? QStringLiteral("/ws/direction/v1/walking/")
                                                : QStringLiteral("/ws/direction/v1/driving/"),
             query, &body, code, error)) return false;
    const QJsonArray routes = body.value(QStringLiteral("result")).toObject()
                                  .value(QStringLiteral("routes")).toArray();
    if (routes.isEmpty() || !routes.first().isObject()) {
        setFailure(code, error, ev::protocol::ErrorCode::MapNoResult,
                   QStringLiteral("路线规划没有结果"));
        return false;
    }
    const QJsonObject selected = routes.first().toObject();
    const QJsonValue distanceValue = selected.value(QStringLiteral("distance"));
    const QJsonValue durationValue = selected.value(QStringLiteral("duration"));
    if (!distanceValue.isDouble() || !durationValue.isDouble()
        || !qIsFinite(distanceValue.toDouble()) || !qIsFinite(durationValue.toDouble())
        || distanceValue.toDouble() < 0.0 || durationValue.toDouble() < 0.0) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("route result is invalid"));
        return false;
    }
    QVector<QPair<double, double>> polyline;
    if (!decodePolyline(selected.value(QStringLiteral("polyline")).toArray(), &polyline)) {
        setFailure(code, error, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("route polyline is invalid"));
        return false;
    }
    // Tencent may return a single point when the origin and destination are
    // the same location.  The public protocol always carries a start and an
    // end point, so preserve the provider point and append the validated
    // destination instead of rejecting an otherwise valid zero-length route.
    if (polyline.size() == 1)
        polyline.append({destinationLatitude, destinationLongitude});
    route->distanceMeters = qRound64(distanceValue.toDouble());
    route->durationSeconds = qMax<qint64>(1, qRound64(durationValue.toDouble() * 60.0));
    route->polyline = polyline;
    return true;
}

} // namespace ev::server::map
