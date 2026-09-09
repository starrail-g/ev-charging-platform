#include "map_service.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace ev::server::map {
namespace {

QString utcNow()
{
    QString value = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (value.endsWith(QStringLiteral("+00:00"))) {
        value.chop(6);
        value.append(QLatin1Char('Z'));
    }
    return value;
}

bool positiveInteger(const QJsonValue &value, qint64 *result)
{
    if (!result || !value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 1.0 || std::floor(number) != number
        || number > 9007199254740991.0) return false;
    *result = static_cast<qint64>(number);
    return true;
}

bool finiteCoordinate(const QJsonValue &value, double minimum, double maximum,
                      double *result)
{
    if (!result || !value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < minimum || number > maximum) return false;
    *result = number;
    return true;
}

QJsonObject nullWarning()
{
    return {};
}

} // namespace

MapService::MapService(ev::database::Database *database)
    : database_(database)
{
}

void MapService::setFailure(MapFailure *failure, ev::protocol::ErrorCode code,
                            const QString &message)
{
    if (!failure) return;
    failure->code = code;
    failure->message = message;
}

bool MapService::isMapMockEnabled()
{
    return qEnvironmentVariable("EV_MAP_SERVER_MOCK").trimmed() == QStringLiteral("1");
}

bool MapService::parseOrigin(const QJsonObject &payload, QJsonObject *origin,
                             QString *address, QString *error)
{
    if (!origin || !address) return false;
    *origin = QJsonObject();
    address->clear();
    const QJsonValue originValue = payload.value(QStringLiteral("origin"));
    if (!originValue.isObject()) {
        if (error) *error = QStringLiteral("origin must be an object");
        return false;
    }
    const QJsonObject input = originValue.toObject();
    const QString kind = input.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("address")) {
        if (input.size() != 2 || !input.value(QStringLiteral("value")).isString()) {
            if (error) *error = QStringLiteral("address origin is invalid");
            return false;
        }
        const QString value = input.value(QStringLiteral("value")).toString()
            .normalized(QString::NormalizationForm_C).trimmed();
        if (value.isEmpty() || value.size() > 200) {
            if (error) *error = QStringLiteral("address must contain 1..200 characters");
            return false;
        }
        *address = value;
        return true;
    }
    if (kind == QStringLiteral("coordinate")) {
        if (input.size() != 3) {
            if (error) *error = QStringLiteral("coordinate origin is invalid");
            return false;
        }
        double latitude = 0.0;
        double longitude = 0.0;
        if (!finiteCoordinate(input.value(QStringLiteral("latitude")), -90.0, 90.0, &latitude)
            || !finiteCoordinate(input.value(QStringLiteral("longitude")), -180.0, 180.0, &longitude)) {
            if (error) *error = QStringLiteral("origin coordinates are invalid");
            return false;
        }
        const double normalizedLatitude = std::round(latitude * 100000.0) / 100000.0;
        const double normalizedLongitude = std::round(longitude * 100000.0) / 100000.0;
        *origin = QJsonObject{{QStringLiteral("latitude"), normalizedLatitude},
                              {QStringLiteral("longitude"), normalizedLongitude}};
        return true;
    }
    if (error) *error = QStringLiteral("origin kind must be address or coordinate");
    return false;
}

QString MapService::queryBinding(const QJsonObject &query)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(query).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}

QString MapService::cacheKey(const QString &operation, const QString &binding)
{
    return operation + QLatin1Char(':') + binding;
}

QString MapService::encodePageToken(const QString &binding, qint64 position)
{
    if (binding.isEmpty() || position < 0) return {};
    const QJsonObject token{{QStringLiteral("b"), binding},
                            {QStringLiteral("p"), position}};
    return QString::fromLatin1(QJsonDocument(token).toJson(QJsonDocument::Compact)
                                   .toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
}

QString MapService::encodeAuditPageToken(const QString &binding,
                                         const QString &createdAt, qint64 id)
{
    const QJsonObject value{{QStringLiteral("binding"), binding},
                            {QStringLiteral("created_at"), createdAt},
                            {QStringLiteral("id"), id}};
    return QString::fromLatin1(QJsonDocument(value).toJson(QJsonDocument::Compact).toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool MapService::decodeAuditPageToken(const QString &token, const QString &binding,
                                      QString *createdAt, qint64 *id)
{
    if (!createdAt || !id) return false;
    QByteArray raw = QByteArray::fromBase64(token.toLatin1(), QByteArray::Base64UrlEncoding);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject value = document.object();
    const QJsonValue tokenBinding = value.value(QStringLiteral("binding"));
    const QJsonValue tokenCreatedAt = value.value(QStringLiteral("created_at"));
    const QJsonValue tokenId = value.value(QStringLiteral("id"));
    if (!tokenBinding.isString() || tokenBinding.toString() != binding
        || !tokenCreatedAt.isString() || tokenCreatedAt.toString().isEmpty()
        || !tokenId.isDouble() || tokenId.toInteger() <= 0) {
        return false;
    }
    *createdAt = tokenCreatedAt.toString();
    *id = tokenId.toInteger();
    return true;
}

bool MapService::decodePageToken(const QString &token, const QString &binding,
                                 qint64 *position)
{
    if (!position || token.isEmpty() || token.size() > 256 || binding.isEmpty()) return false;
    const QByteArray decoded = QByteArray::fromBase64(token.toLatin1(), QByteArray::Base64UrlEncoding);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(decoded, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject object = document.object();
    qint64 parsed = 0;
    if (object.size() != 2 || object.value(QStringLiteral("b")).toString() != binding
        || !positiveInteger(object.value(QStringLiteral("p")), &parsed)) return false;
    *position = parsed;
    if (*position < 0) return false;
    return true;
}

QVector<ev::database::MapPoi> MapService::parseCachedPois(const QJsonObject &payload)
{
    QVector<ev::database::MapPoi> result;
    const QJsonArray array = payload.value(QStringLiteral("pois")).toArray();
    result.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject poi = value.toObject();
        if (!poi.value(QStringLiteral("provider_poi_id")).isString()
            || !poi.value(QStringLiteral("name")).isString()
            || !poi.value(QStringLiteral("address")).isString()
            || !poi.value(QStringLiteral("latitude")).isDouble()
            || !poi.value(QStringLiteral("longitude")).isDouble()
            || !poi.value(QStringLiteral("distance_meters")).isDouble()) continue;
        result.append(ev::database::MapPoi{
            poi.value(QStringLiteral("provider_poi_id")).toString(),
            poi.value(QStringLiteral("name")).toString(),
            poi.value(QStringLiteral("address")).toString(),
            poi.value(QStringLiteral("latitude")).toDouble(),
            poi.value(QStringLiteral("longitude")).toDouble(),
            poi.value(QStringLiteral("distance_meters")).toInteger()});
    }
    return result;
}

QJsonObject MapService::mapOnlyPayload(const QString &provider,
                                       const QString &providerSource,
                                       const QJsonObject &origin,
                                       const QVector<ev::database::MapPoi> &pois,
                                       bool hasMore, const QString &nextToken)
{
    QJsonArray values;
    for (const ev::database::MapPoi &poi : pois) {
        values.append(QJsonObject{{QStringLiteral("provider_poi_id"), poi.providerPoiId},
                                  {QStringLiteral("name"), poi.name},
                                  {QStringLiteral("address"), poi.address},
                                  {QStringLiteral("latitude"), poi.latitude},
                                  {QStringLiteral("longitude"), poi.longitude},
                                  {QStringLiteral("distance_meters"), poi.distanceMeters}});
    }
    return QJsonObject{{QStringLiteral("provider"), provider},
                       {QStringLiteral("provider_source"), providerSource},
                       {QStringLiteral("resolved_origin"), origin},
                       {QStringLiteral("pois"), values},
                       {QStringLiteral("has_more"), hasMore},
                       {QStringLiteral("next_page_token"), hasMore ? QJsonValue(nextToken) : QJsonValue(QJsonValue::Null)}};
}

bool MapService::stationSearch(const QString &requestId, qint64 userId,
                               const QJsonObject &payload, QJsonObject *response,
                               MapFailure *failure)
{
    if (!database_ || !response || !failure || requestId.isEmpty() || userId <= 0) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest,
                   QStringLiteral("map search arguments are invalid"));
        return false;
    }
    QJsonObject origin;
    QString address;
    QString validationError;
    if (!parseOrigin(payload, &origin, &address, &validationError)) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest, validationError);
        return false;
    }
    qint64 radius = 1000;
    qint64 pageSize = 20;
    const QJsonValue radiusValue = payload.value(QStringLiteral("radius_meters"));
    const QJsonValue pageSizeValue = payload.value(QStringLiteral("page_size"));
    if ((!radiusValue.isUndefined()
         && (!positiveInteger(radiusValue, &radius) || radius < 10 || radius > 1000))
        || (!pageSizeValue.isUndefined()
            && (!positiveInteger(pageSizeValue, &pageSize) || pageSize > 20))) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest,
                   QStringLiteral("radius_meters must be 10..1000 and page_size 1..20"));
        return false;
    }
    const QJsonValue pageTokenValue = payload.value(QStringLiteral("page_token"));
    if (!pageTokenValue.isUndefined() && !pageTokenValue.isNull() && !pageTokenValue.isString()) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest,
                   QStringLiteral("page_token must be null or a string"));
        return false;
    }
    const QString pageToken = pageTokenValue.isString() ? pageTokenValue.toString() : QString();
    QJsonObject baseQuery{{QStringLiteral("user_id"), userId},
                          {QStringLiteral("origin"), address.isEmpty()
                              ? QJsonValue(QJsonObject{{QStringLiteral("kind"), QStringLiteral("coordinate")},
                                                       {QStringLiteral("latitude"), origin.value(QStringLiteral("latitude"))},
                                                       {QStringLiteral("longitude"), origin.value(QStringLiteral("longitude"))}})
                              : QJsonValue(QJsonObject{{QStringLiteral("kind"), QStringLiteral("address")},
                                                       {QStringLiteral("value"), address}})},
                          {QStringLiteral("radius_meters"), radius},
                          {QStringLiteral("page_size"), pageSize},
                          {QStringLiteral("operation"), QStringLiteral("map.station.search")}};
    const QString binding = queryBinding(baseQuery);
    qint64 offset = 0;
    if (!pageToken.isEmpty() && !decodePageToken(pageToken, binding, &offset)) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest,
                   QStringLiteral("page_token is invalid or bound to another query"));
        return false;
    }
    const QString key = cacheKey(QStringLiteral("map.station.search"),
                                  binding + QLatin1Char(':') + QString::number(offset));
    QJsonObject normalizedQuery = baseQuery;
    normalizedQuery.insert(QStringLiteral("provider"), QStringLiteral("tencent"));
    normalizedQuery.insert(QStringLiteral("page_token"), pageToken.isEmpty()
                           ? QJsonValue(QJsonValue::Null) : QJsonValue(pageToken));
    normalizedQuery.insert(QStringLiteral("cache_key"), key);
    const QString fingerprint = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(normalizedQuery).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
    QJsonObject replay;
    bool replayFound = false;
    QString dbError;
    ev::database::ErrorKind dbKind = ev::database::ErrorKind::None;
    if (!database_->findRequestReplay(requestId, QStringLiteral("map.station.search"), fingerprint,
                                      &replay, &replayFound, &dbError, &dbKind)) {
        if (dbKind == ev::database::ErrorKind::Conflict) {
            setFailure(failure, ev::protocol::ErrorCode::Conflict, dbError);
        } else {
            setFailure(failure, ev::protocol::ErrorCode::DatabaseError,
                       QStringLiteral("map request replay lookup failed"));
        }
        return false;
    }
    if (replayFound) {
        *response = replay;
        return true;
    }

    if (!isMapMockEnabled()) {
        if (qEnvironmentVariable("TENCENT_MAP_ENABLED", "0") != QStringLiteral("1")) {
            qint64 ignored = 0;
            database_->recordMapRequest(requestId, QStringLiteral("map.station.search"), userId,
                                        normalizedQuery, key, QStringLiteral("error"), {},
                                        static_cast<int>(ev::protocol::ErrorCode::MapDisabled), 0,
                                        &ignored, nullptr, nullptr);
            setFailure(failure, ev::protocol::ErrorCode::MapDisabled,
                       QStringLiteral("地图服务未启用"));
            return false;
        }
        if (qEnvironmentVariable("TENCENT_MAP_KEY").trimmed().isEmpty()) {
            qint64 ignored = 0;
            database_->recordMapRequest(requestId, QStringLiteral("map.station.search"), userId,
                                        normalizedQuery, key, QStringLiteral("error"), {},
                                        static_cast<int>(ev::protocol::ErrorCode::MapNotConfigured), 0,
                                        &ignored, nullptr, nullptr);
            setFailure(failure, ev::protocol::ErrorCode::MapNotConfigured,
                       QStringLiteral("地图服务未配置"));
            return false;
        }
    }

    QJsonObject cached;
    bool fresh = false;
    bool stale = false;
    if (!database_->readMapCache(key, &cached, &fresh, &stale, &dbError, &dbKind)) {
        setFailure(failure, ev::protocol::ErrorCode::DatabaseError,
                   QStringLiteral("map cache lookup failed"));
        return false;
    }
    if (!address.isEmpty() && (fresh || stale)) {
        const QJsonObject cachedOrigin = cached.value(QStringLiteral("resolved_origin")).toObject();
        if (cachedOrigin.value(QStringLiteral("latitude")).isDouble()
            && cachedOrigin.value(QStringLiteral("longitude")).isDouble()) {
            origin = cachedOrigin;
        }
    }
    if (!address.isEmpty() && !origin.value(QStringLiteral("latitude")).isDouble()) {
        ev::protocol::ErrorCode code = ev::protocol::ErrorCode::MapResponseInvalid;
        QString clientError;
        TencentClient &client = isMapMockEnabled()
            ? static_cast<TencentClient &>(deterministicClient_)
            : static_cast<TencentClient &>(httpClient_);
        if (!client.geocode(address, &origin, &code, &clientError)) {
            qint64 ignored = 0;
            database_->recordMapRequest(requestId, QStringLiteral("map.station.search"), userId,
                                        normalizedQuery, key, QStringLiteral("error"), {},
                                        static_cast<int>(code), 0, &ignored, nullptr, nullptr);
            setFailure(failure, code, clientError);
            return false;
        }
    }
    QVector<ev::database::MapPoi> allPois;
    QString dataSource = QStringLiteral("server_mock");
    ev::protocol::ErrorCode upstreamCode = ev::protocol::ErrorCode::MapUpstreamUnavailable;
    QString upstreamError;
    bool fromCache = false;
    bool upstreamAttempted = false;
    if (fresh) {
        allPois = parseCachedPois(cached);
        origin = cached.value(QStringLiteral("resolved_origin")).toObject();
        dataSource = cached.value(QStringLiteral("provider_source")).toString()
                         == QStringLiteral("server_mock")
                     ? QStringLiteral("server_mock") : QStringLiteral("tencent_cache");
        fromCache = true;
    } else {
        if (stale) {
            // A stale value is a fallback only. Try the provider first so a
            // healthy upstream refreshes the cache rather than extending old
            // map metadata indefinitely.
            upstreamAttempted = true;
            TencentClient &client = isMapMockEnabled()
                ? static_cast<TencentClient &>(deterministicClient_)
                : static_cast<TencentClient &>(httpClient_);
            if (!client.searchStations(origin, radius, &allPois,
                                       &upstreamCode, &upstreamError)) {
                allPois = parseCachedPois(cached);
                origin = cached.value(QStringLiteral("resolved_origin")).toObject();
                dataSource = cached.value(QStringLiteral("provider_source")).toString()
                                 == QStringLiteral("server_mock")
                             ? QStringLiteral("server_mock") : QStringLiteral("tencent_stale");
                fromCache = true;
            } else {
                dataSource = isMapMockEnabled() ? QStringLiteral("server_mock")
                                                : QStringLiteral("tencent_live");
            }
        } else {
            upstreamAttempted = true;
            TencentClient &client = isMapMockEnabled()
                ? static_cast<TencentClient &>(deterministicClient_)
                : static_cast<TencentClient &>(httpClient_);
            if (!client.searchStations(origin, radius, &allPois,
                                       &upstreamCode, &upstreamError)) {
            qint64 ignored = 0;
            database_->recordMapRequest(requestId, QStringLiteral("map.station.search"), userId,
                                        normalizedQuery, key, QStringLiteral("error"), {},
                                        static_cast<int>(upstreamCode), 0, &ignored, nullptr, nullptr);
            setFailure(failure, upstreamCode, upstreamError);
            return false;
            }
            dataSource = isMapMockEnabled() ? QStringLiteral("server_mock")
                                            : QStringLiteral("tencent_live");
        }
    }
    if (allPois.isEmpty()) {
        qint64 ignored = 0;
        database_->recordMapRequest(requestId, QStringLiteral("map.station.search"), userId,
                                    normalizedQuery, key, QStringLiteral("error"), dataSource,
                                    static_cast<int>(ev::protocol::ErrorCode::MapNoResult), 0,
                                    &ignored, nullptr, nullptr);
        setFailure(failure, ev::protocol::ErrorCode::MapNoResult, QStringLiteral("附近没有充电站"));
        return false;
    }
    std::sort(allPois.begin(), allPois.end(), [](const ev::database::MapPoi &left,
                                                 const ev::database::MapPoi &right) {
        if (left.distanceMeters != right.distanceMeters)
            return left.distanceMeters < right.distanceMeters;
        return left.providerPoiId < right.providerPoiId;
    });
    if (offset >= allPois.size()) {
        qint64 ignored = 0;
        database_->recordMapRequest(requestId, QStringLiteral("map.station.search"), userId,
                                    normalizedQuery, key, QStringLiteral("error"), dataSource,
                                    static_cast<int>(ev::protocol::ErrorCode::MapNoResult), 0,
                                    &ignored, nullptr, nullptr);
        setFailure(failure, ev::protocol::ErrorCode::MapNoResult, QStringLiteral("分页位置已失效"));
        return false;
    }
    const int begin = static_cast<int>(offset);
    const int end = qMin(begin + static_cast<int>(pageSize), allPois.size());
    QVector<ev::database::MapPoi> page;
    page.reserve(end - begin);
    for (int i = begin; i < end; ++i) page.append(allPois.at(i));
    const bool hasMore = end < allPois.size();
    const QString nextToken = hasMore ? encodePageToken(binding, end) : QString();
    QJsonObject warning = nullWarning();
    if (dataSource == QStringLiteral("tencent_stale")) {
        warning = QJsonObject{{QStringLiteral("code"), static_cast<int>(ev::protocol::ErrorCode::MapUpstreamUnavailable)},
                              {QStringLiteral("name"), ev::protocol::errorCodeName(ev::protocol::ErrorCode::MapUpstreamUnavailable)},
                              {QStringLiteral("message"), QStringLiteral("地图服务暂不可用，当前展示缓存结果")},
                              {QStringLiteral("retryable"), true}, {QStringLiteral("degraded"), true}};
    } else if (dataSource == QStringLiteral("server_mock")) {
        warning = QJsonObject{{QStringLiteral("code"), static_cast<int>(ev::protocol::ErrorCode::MapServerMock)},
                              {QStringLiteral("name"), ev::protocol::errorCodeName(ev::protocol::ErrorCode::MapServerMock)},
                              {QStringLiteral("message"), QStringLiteral("当前为服务端演示数据")},
                              {QStringLiteral("retryable"), false}, {QStringLiteral("degraded"), true}};
    }
    const QJsonObject cachePayload = mapOnlyPayload(QStringLiteral("tencent"), dataSource,
                                                    origin, page,
                                                    hasMore, nextToken);
    if (!database_->importMapStations(requestId, userId, normalizedQuery, origin, page,
                                      dataSource, warning, hasMore, nextToken, response,
                                      &dbError, &dbKind)) {
        if (dbKind == ev::database::ErrorKind::Conflict)
            setFailure(failure, ev::protocol::ErrorCode::Conflict, dbError);
        else setFailure(failure, ev::protocol::ErrorCode::DatabaseError,
                        QStringLiteral("import map stations failed"));
        return false;
    }
    if (!fromCache) {
        const QDateTime fetched = QDateTime::currentDateTimeUtc();
        database_->writeMapCache(key, QStringLiteral("map.station.search"), cachePayload,
                                 nextToken, fetched, fetched.addSecs(86400),
                                 fetched.addSecs(7 * 86400), nullptr, nullptr);
    }
    if (upstreamAttempted && response->value(QStringLiteral("map_request_log_id")).toInteger() > 0) {
        database_->recordMapUpstreamCall(response->value(QStringLiteral("map_request_log_id")).toInteger(),
                                         QStringLiteral("poi_search"), fromCache ? QStringLiteral("error") : QStringLiteral("success"),
                                         fromCache ? 0 : 200, 0, fromCache ? static_cast<int>(upstreamCode) : 0,
                                         nullptr, nullptr);
    }
    return true;
}

bool MapService::routePlan(const QString &requestId, qint64 userId,
                           const QJsonObject &payload, QJsonObject *response,
                           MapFailure *failure)
{
    if (!database_ || !response || !failure || requestId.isEmpty() || userId <= 0) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest,
                   QStringLiteral("map route arguments are invalid"));
        return false;
    }
    QJsonObject origin;
    QString address;
    QString validationError;
    if (!parseOrigin(payload, &origin, &address, &validationError)) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest, validationError);
        return false;
    }
    qint64 stationId = 0;
    const QString mode = payload.value(QStringLiteral("mode")).toString();
    if (!positiveInteger(payload.value(QStringLiteral("station_id")), &stationId)
        || (mode != QStringLiteral("driving") && mode != QStringLiteral("walking"))) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest,
                   QStringLiteral("station_id and mode are invalid"));
        return false;
    }
    QJsonObject destination;
    QString dbError;
    ev::database::ErrorKind dbKind = ev::database::ErrorKind::None;
    if (!database_->getStationMapDestination(stationId, &destination, &dbError, &dbKind)) {
        setFailure(failure, dbKind == ev::database::ErrorKind::NotFound
                   ? ev::protocol::ErrorCode::NotFound : ev::protocol::ErrorCode::DatabaseError,
                   dbKind == ev::database::ErrorKind::NotFound ? QStringLiteral("station not found")
                   : QStringLiteral("read route destination failed"));
        return false;
    }
    const QJsonObject requestedOrigin = address.isEmpty()
        ? origin
        : QJsonObject{{QStringLiteral("kind"), QStringLiteral("address")},
                       {QStringLiteral("value"), address}};
    const QJsonObject preflightQuery{{QStringLiteral("user_id"), userId},
                                     {QStringLiteral("origin"), requestedOrigin},
                                     {QStringLiteral("station_id"), stationId},
                                     {QStringLiteral("mode"), mode},
                                     {QStringLiteral("operation"), QStringLiteral("map.route.plan")}};
    const auto recordRouteFailure = [&](ev::protocol::ErrorCode code) {
        qint64 ignoredLogId = 0;
        database_->recordMapRequest(requestId, QStringLiteral("map.route.plan"), userId,
                                    preflightQuery, QString(), QStringLiteral("error"), {},
                                    static_cast<int>(code), 0, &ignoredLogId, nullptr, nullptr);
    };
    if (!address.isEmpty()) {
        ev::protocol::ErrorCode code = ev::protocol::ErrorCode::MapResponseInvalid;
        QString clientError;
        if (!isMapMockEnabled() && qEnvironmentVariable("TENCENT_MAP_KEY").trimmed().isEmpty()) {
            recordRouteFailure(qEnvironmentVariable("TENCENT_MAP_ENABLED", "0") == QStringLiteral("1")
                               ? ev::protocol::ErrorCode::MapNotConfigured
                               : ev::protocol::ErrorCode::MapDisabled);
            setFailure(failure, qEnvironmentVariable("TENCENT_MAP_ENABLED", "0") == QStringLiteral("1")
                       ? ev::protocol::ErrorCode::MapNotConfigured : ev::protocol::ErrorCode::MapDisabled,
                       QStringLiteral("地图服务未配置或未启用"));
            return false;
        }
        TencentClient &client = isMapMockEnabled()
            ? static_cast<TencentClient &>(deterministicClient_)
            : static_cast<TencentClient &>(httpClient_);
        if (!client.geocode(address, &origin, &code, &clientError)) {
            recordRouteFailure(code);
            setFailure(failure, code, clientError);
            return false;
        }
    } else if (!isMapMockEnabled() && qEnvironmentVariable("TENCENT_MAP_ENABLED", "0") != QStringLiteral("1")) {
        recordRouteFailure(ev::protocol::ErrorCode::MapDisabled);
        setFailure(failure, ev::protocol::ErrorCode::MapDisabled, QStringLiteral("地图服务未启用"));
        return false;
    } else if (!isMapMockEnabled() && qEnvironmentVariable("TENCENT_MAP_KEY").trimmed().isEmpty()) {
        recordRouteFailure(ev::protocol::ErrorCode::MapNotConfigured);
        setFailure(failure, ev::protocol::ErrorCode::MapNotConfigured, QStringLiteral("地图服务未配置"));
        return false;
    }
    const QJsonObject query{{QStringLiteral("user_id"), userId},
                            {QStringLiteral("origin"), origin},
                            {QStringLiteral("station_id"), stationId},
                            {QStringLiteral("mode"), mode},
                            {QStringLiteral("operation"), QStringLiteral("map.route.plan")}};
    const QString binding = queryBinding(query);
    const QString key = cacheKey(QStringLiteral("map.route.plan"), binding);
    QJsonObject cached;
    bool fresh = false;
    bool stale = false;
    if (!database_->readMapCache(key, &cached, &fresh, &stale, &dbError, &dbKind)) {
        setFailure(failure, ev::protocol::ErrorCode::DatabaseError, QStringLiteral("route cache lookup failed"));
        return false;
    }
    RouteResult route;
    QString dataSource = QStringLiteral("server_mock");
    bool fromCache = false;
    bool upstreamAttempted = false;
    ev::protocol::ErrorCode providerCode = ev::protocol::ErrorCode::MapUpstreamUnavailable;
    QString providerError;
    if (fresh || stale) {
        const QJsonObject routeJson = cached;
        const QJsonArray points = routeJson.value(QStringLiteral("polyline")).toArray();
        if (fresh) {
            route.distanceMeters = routeJson.value(QStringLiteral("distance_meters")).toInteger();
            route.durationSeconds = routeJson.value(QStringLiteral("duration_seconds")).toInteger();
            for (const QJsonValue &point : points) {
                const QJsonArray pair = point.toArray();
                if (pair.size() == 2) route.polyline.append({pair.at(0).toDouble(), pair.at(1).toDouble()});
            }
            dataSource = routeJson.value(QStringLiteral("provider_source")).toString()
                             == QStringLiteral("server_mock")
                         ? QStringLiteral("server_mock") : QStringLiteral("tencent_cache");
            fromCache = true;
        } else {
            upstreamAttempted = true;
            TencentClient &client = isMapMockEnabled()
                ? static_cast<TencentClient &>(deterministicClient_)
                : static_cast<TencentClient &>(httpClient_);
            if (!client.planRoute(origin, destination, mode, &route,
                                  &providerCode, &providerError)) {
                route.distanceMeters = routeJson.value(QStringLiteral("distance_meters")).toInteger();
                route.durationSeconds = routeJson.value(QStringLiteral("duration_seconds")).toInteger();
                for (const QJsonValue &point : points) {
                    const QJsonArray pair = point.toArray();
                    if (pair.size() == 2) route.polyline.append({pair.at(0).toDouble(), pair.at(1).toDouble()});
                }
                dataSource = routeJson.value(QStringLiteral("provider_source")).toString()
                                 == QStringLiteral("server_mock")
                             ? QStringLiteral("server_mock") : QStringLiteral("tencent_stale");
                fromCache = true;
            } else {
                dataSource = QStringLiteral("tencent_live");
            }
        }
    } else {
        upstreamAttempted = true;
        TencentClient &client = isMapMockEnabled()
            ? static_cast<TencentClient &>(deterministicClient_)
            : static_cast<TencentClient &>(httpClient_);
        if (!client.planRoute(origin, destination, mode, &route,
                              &providerCode, &providerError)) {
            qint64 ignored = 0;
            database_->recordMapRequest(requestId, QStringLiteral("map.route.plan"), userId,
                                        query, key, QStringLiteral("error"), {},
                                        static_cast<int>(providerCode), 0, &ignored, nullptr, nullptr);
            setFailure(failure, providerCode, providerError);
            return false;
        }
        dataSource = isMapMockEnabled() ? QStringLiteral("server_mock")
                                        : QStringLiteral("tencent_live");
    }
    // Older cache entries may contain Tencent's valid one-point response for
    // a zero-length route. Normalize it at the service boundary as well so a
    // cache hit follows the same protocol contract as a live response.
    if (route.polyline.size() == 1)
        route.polyline.append({destination.value(QStringLiteral("latitude")).toDouble(),
                               destination.value(QStringLiteral("longitude")).toDouble()});
    if (route.polyline.size() < 2 || route.polyline.size() > 4096) {
        qint64 ignoredLogId = 0;
        database_->recordMapRequest(requestId, QStringLiteral("map.route.plan"), userId,
                                    query, key, QStringLiteral("error"), dataSource,
                                    static_cast<int>(ev::protocol::ErrorCode::MapResponseInvalid),
                                    0, &ignoredLogId, nullptr, nullptr);
        setFailure(failure, ev::protocol::ErrorCode::MapResponseInvalid,
                   QStringLiteral("route polyline is invalid"));
        return false;
    }
    QJsonArray polyline;
    for (const auto &point : route.polyline)
        polyline.append(QJsonArray{point.first, point.second});
    QJsonObject warning = nullWarning();
    if (dataSource == QStringLiteral("tencent_stale")) {
        warning = QJsonObject{{QStringLiteral("code"), static_cast<int>(ev::protocol::ErrorCode::MapUpstreamUnavailable)},
                              {QStringLiteral("name"), ev::protocol::errorCodeName(ev::protocol::ErrorCode::MapUpstreamUnavailable)},
                              {QStringLiteral("message"), QStringLiteral("地图服务暂不可用，当前展示缓存路线")},
                              {QStringLiteral("retryable"), true}, {QStringLiteral("degraded"), true}};
    } else if (dataSource == QStringLiteral("server_mock")) {
        warning = QJsonObject{{QStringLiteral("code"), static_cast<int>(ev::protocol::ErrorCode::MapServerMock)},
                              {QStringLiteral("name"), ev::protocol::errorCodeName(ev::protocol::ErrorCode::MapServerMock)},
                              {QStringLiteral("message"), QStringLiteral("当前为服务端演示路线")},
                              {QStringLiteral("retryable"), false}, {QStringLiteral("degraded"), true}};
    }
    QJsonObject mapOnly{{QStringLiteral("provider"), QStringLiteral("tencent")},
                        {QStringLiteral("provider_source"), dataSource},
                        {QStringLiteral("distance_meters"), route.distanceMeters},
                        {QStringLiteral("duration_seconds"), route.durationSeconds},
                        {QStringLiteral("polyline"), polyline}};
    qint64 logId = 0;
    database_->recordMapRequest(requestId, QStringLiteral("map.route.plan"), userId, query, key,
                                dataSource == QStringLiteral("server_mock") ? QStringLiteral("mock")
                                : dataSource == QStringLiteral("tencent_stale") ? QStringLiteral("degraded")
                                : QStringLiteral("success"), dataSource,
                                0, warning.value(QStringLiteral("code")).toInt(), &logId,
                                &dbError, &dbKind);
    if (logId <= 0) {
        setFailure(failure, ev::protocol::ErrorCode::DatabaseError, QStringLiteral("write route audit failed"));
        return false;
    }
    *response = QJsonObject{{QStringLiteral("provider"), QStringLiteral("tencent")},
                            {QStringLiteral("data_source"), dataSource},
                            {QStringLiteral("mode"), mode},
                            {QStringLiteral("resolved_origin"), origin},
                            {QStringLiteral("destination"), destination},
                            {QStringLiteral("distance_meters"), route.distanceMeters},
                            {QStringLiteral("duration_seconds"), route.durationSeconds},
                            {QStringLiteral("polyline"), polyline},
                            {QStringLiteral("fetched_at"), utcNow()},
                            {QStringLiteral("expires_at"), QDateTime::currentDateTimeUtc().addSecs(300).toString(Qt::ISODate)},
                            {QStringLiteral("map_request_log_id"), logId},
                            {QStringLiteral("warning"), warning.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(warning)}};
    if (!fromCache) {
        const QDateTime fetched = QDateTime::currentDateTimeUtc();
        database_->writeMapCache(key, QStringLiteral("map.route.plan"), mapOnly, {}, fetched,
                                 fetched.addSecs(300), fetched.addSecs(7 * 86400), nullptr, nullptr);
    }
    if (upstreamAttempted) database_->recordMapUpstreamCall(logId, mode, fromCache ? QStringLiteral("error") : QStringLiteral("success"),
                                     fromCache ? 0 : 200, 0, fromCache ? static_cast<int>(providerCode) : 0,
                                     nullptr, nullptr);
    return true;
}

bool MapService::listAudit(const QString &operation, const QString &resultStatus,
                           const QString &pageToken, qint64 limit,
                           QJsonObject *response, MapFailure *failure)
{
    if (!database_ || !response || !failure || limit < 1 || limit > 100
        || (!operation.isEmpty() && operation != QStringLiteral("map.station.search")
            && operation != QStringLiteral("map.route.plan"))
        || (!resultStatus.isEmpty() && resultStatus != QStringLiteral("success")
            && resultStatus != QStringLiteral("error") && resultStatus != QStringLiteral("degraded")
            && resultStatus != QStringLiteral("mock"))) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest,
                   QStringLiteral("map audit filters are invalid"));
        return false;
    }
    const QJsonObject bindingObject{{QStringLiteral("operation"), operation},
                                    {QStringLiteral("result_status"), resultStatus}};
    const QString binding = queryBinding(bindingObject);
    QString beforeCreatedAt;
    qint64 beforeId = 0;
    if (!pageToken.isEmpty() && !decodeAuditPageToken(pageToken, binding,
                                                       &beforeCreatedAt, &beforeId)) {
        setFailure(failure, ev::protocol::ErrorCode::InvalidRequest,
                   QStringLiteral("audit page_token is invalid or bound to another filter"));
        return false;
    }
    QJsonArray records;
    bool hasMore = false;
    QString dbError;
    ev::database::ErrorKind dbKind = ev::database::ErrorKind::None;
    if (!database_->listMapAudit(operation, resultStatus, beforeCreatedAt, beforeId,
                                 limit, &records, &hasMore,
                                 &dbError, &dbKind)) {
        setFailure(failure, ev::protocol::ErrorCode::DatabaseError, QStringLiteral("list map audit failed"));
        return false;
    }
    const QJsonObject last = records.isEmpty() ? QJsonObject() : records.last().toObject();
    *response = QJsonObject{{QStringLiteral("records"), records},
                            {QStringLiteral("has_more"), hasMore},
                            {QStringLiteral("next_page_token"), hasMore
                                ? QJsonValue(encodeAuditPageToken(
                                      binding,
                                      last.value(QStringLiteral("created_at")).toString(),
                                      last.value(QStringLiteral("id")).toInteger()))
                                : QJsonValue(QJsonValue::Null)}};
    return true;
}

} // namespace ev::server::map
