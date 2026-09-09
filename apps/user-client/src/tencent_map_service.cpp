#include "tencent_map_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QUrlQuery>
#include <cmath>
#include <memory>

namespace ev {
namespace {

bool validNumber(const QJsonValue &value) { return value.isDouble() && std::isfinite(value.toDouble()); }

bool readCoordinate(const QJsonObject &object, GeoCoordinate *coordinate) {
  if (!object.contains(QStringLiteral("lat")) || !object.contains(QStringLiteral("lng")) ||
      !validNumber(object.value(QStringLiteral("lat"))) || !validNumber(object.value(QStringLiteral("lng")))) return false;
  *coordinate = {object.value(QStringLiteral("lat")).toDouble(), object.value(QStringLiteral("lng")).toDouble()};
  return isValidCoordinate(*coordinate);
}

bool readPoi(const QJsonObject &object, MapPoi *poi, MapSource source) {
  const auto location = object.value(QStringLiteral("location")).toObject();
  if (!readCoordinate(location, &poi->coordinate)) return false;
  poi->id = object.value(QStringLiteral("id")).toString();
  poi->title = object.value(QStringLiteral("title")).toString();
  poi->address = object.value(QStringLiteral("address")).toString();
  const auto distance = object.value(QStringLiteral("_distance"));
  poi->distanceMeters = distance.isDouble() ? distance.toInt(-1) : -1;
  poi->source = source;
  return !poi->id.isEmpty() && !poi->title.isEmpty() && !poi->address.isEmpty();
}

QVector<GeoCoordinate> readPolyline(const QJsonObject &route) {
  QVector<GeoCoordinate> points;
  const auto values = route.value(QStringLiteral("polyline")).toArray();
  if (values.size() >= 4 && values.size() % 2 == 0) {
    double latitude = 0.0;
    double longitude = 0.0;
    for (int i = 0; i < values.size(); i += 2) {
      if (!validNumber(values.at(i)) || !validNumber(values.at(i + 1))) { points.clear(); break; }
      if (i == 0) {
        latitude = values.at(i).toDouble();
        longitude = values.at(i + 1).toDouble();
      } else {
        // Tencent Direction returns the first point absolutely and the
        // remaining points as 1e-6 degree forward deltas.
        latitude += values.at(i).toDouble() / 1000000.0;
        longitude += values.at(i + 1).toDouble() / 1000000.0;
      }
      const GeoCoordinate point{latitude, longitude};
      if (!isValidCoordinate(point)) { points.clear(); break; }
      points.push_back(point);
    }
  }
  return points;
}

QString envOrEmpty(const char *name) {
  return qEnvironmentVariableIsSet(name) ? qEnvironmentVariable(name).trimmed() : QString();
}

bool looksLikePlaceholder(const QString &value) {
  const QString key = value.trimmed().toLower();
  return key.isEmpty() || key.size() < 8 || key.contains(QStringLiteral("replace")) ||
         key.contains(QStringLiteral("your-key")) || key.contains(QStringLiteral("example")) ||
         key.contains(QStringLiteral("placeholder")) || key.contains(QStringLiteral("changeme"));
}

struct ApiFailureDetails {
  QString message;
  bool retryable{true};
};

ApiFailureDetails apiFailureDetails(const QJsonObject &root, int status) {
  const QString message = root.value(QStringLiteral("message")).toString().toLower();
  if (status == 121 || message.contains(QStringLiteral("调用量")) ||
      message.contains(QStringLiteral("配额")) || message.contains(QStringLiteral("上限")))
    return {QStringLiteral("腾讯地图今日调用额度已用完"), false};
  if (status == 311 || (message.contains(QStringLiteral("key")) &&
      (message.contains(QStringLiteral("格式")) || message.contains(QStringLiteral("无效")) ||
       message.contains(QStringLiteral("invalid")))))
    return {QStringLiteral("腾讯地图 Key 无效或格式错误"), false};
  if (message.contains(QStringLiteral("权限")) || message.contains(QStringLiteral("授权")) ||
      message.contains(QStringLiteral("denied")) || message.contains(QStringLiteral("unauthorized")) ||
      message.contains(QStringLiteral("ip")) || message.contains(QStringLiteral("domain")))
    return {QStringLiteral("腾讯地图权限或调用来源未授权"), false};
  return {QStringLiteral("腾讯地图服务暂不可用"), true};
}

} // namespace

TencentMapService::TencentMapService(QObject *parent, const QString &baseUrl, int timeoutMs,
                                     const QString &localConfigPath)
    : QObject(parent), manager_(this), baseUrl_(baseUrl.trimmed()),
      localConfigPath_(localConfigPath.trimmed()), timeoutMs_(qMax(1, timeoutMs)) {
  apiKey_ = qEnvironmentVariableIsSet("TENCENT_MAP_KEY")
      ? envOrEmpty("TENCENT_MAP_KEY") : readLocalValue(QStringLiteral("TENCENT_MAP_KEY"));
  QString enabledValue = qEnvironmentVariableIsSet("TENCENT_MAP_ENABLED")
      ? envOrEmpty("TENCENT_MAP_ENABLED") : readLocalValue(QStringLiteral("TENCENT_MAP_ENABLED"));
  enabled_ = enabledValue.isEmpty() ? true : enabledValue != QStringLiteral("0");
  keyValid_ = !looksLikePlaceholder(apiKey_);
}

QString TencentMapService::readLocalValue(const QString &name) const {
  QStringList candidates;
  if (!localConfigPath_.isEmpty()) candidates << localConfigPath_;
  QDir dir(QCoreApplication::applicationDirPath());
  for (int i = 0; i < 6; ++i) { candidates << dir.filePath(QStringLiteral("config/local.env")); if (!dir.cdUp()) break; }
  candidates << QDir::current().filePath(QStringLiteral("config/local.env"));
  for (const auto &path : candidates) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
    while (!file.atEnd()) {
      const QString line = QString::fromUtf8(file.readLine()).trimmed();
      if (line.startsWith(name + QChar('='))) return line.section(QChar('='), 1).trimmed();
    }
  }
  return {};
}

QString TencentMapService::configurationMessage() const {
  if (!enabled_) return QStringLiteral("腾讯地图已关闭，当前使用 Mock/离线模式");
  if (!keyValid_) return QStringLiteral("Key 未配置，当前使用 Mock/离线模式");
  return QStringLiteral("已配置腾讯地图 WebService");
}

void TencentMapService::cancelPending() {
  const auto replies = manager_.findChildren<QNetworkReply *>();
  for (auto *reply : replies) if (reply && reply->isRunning()) reply->abort();
}

MapError TencentMapService::error(MapErrorCategory category, const QString &message, bool retryable, int httpStatus, int apiStatus) {
  return {category, message, retryable, httpStatus, apiStatus};
}

QUrl TencentMapService::endpoint(const QString &path) const {
  if (!baseUrl_.isEmpty()) return QUrl(baseUrl_ + (baseUrl_.endsWith(QChar('/')) ? QString() : QStringLiteral("/")) + path);
  return QUrl(QStringLiteral("https://apis.map.qq.com") + path);
}

void TencentMapService::requestJson(const QString &path, const QUrlQuery &query, JsonCallback callback) {
  if (!configured()) {
    QTimer::singleShot(0, this, [callback = std::move(callback)] { callback({}, 0, error(MapErrorCategory::MissingKey, QStringLiteral("Key 未配置"), false)); });
    return;
  }
  QUrl url = endpoint(path);
  QUrlQuery finalQuery = query;
  finalQuery.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
  finalQuery.addQueryItem(QStringLiteral("key"), apiKey_);
  url.setQuery(finalQuery);
  auto finished = std::make_shared<bool>(false);
  QNetworkReply *reply = manager_.get(QNetworkRequest(url));
  const QPointer<QNetworkReply> guardedReply(reply);
  QTimer::singleShot(timeoutMs_, this, [guardedReply, finished, callback] {
    auto *reply = guardedReply.data();
    if (*finished || !reply) return;
    *finished = true;
    reply->abort();
    callback({}, 0, error(MapErrorCategory::Timeout, QStringLiteral("地图请求超时，请重试"), true));
    reply->deleteLater();
  });
  connect(reply, &QNetworkReply::finished, this, [reply, finished, callback] {
    if (*finished) return;
    *finished = true;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
      callback({}, httpStatus, error(MapErrorCategory::Network, QStringLiteral("地图网络暂不可用，请稍后重试"), true, httpStatus));
      reply->deleteLater();
      return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
      callback({}, httpStatus, error(MapErrorCategory::Network, QStringLiteral("地图服务暂不可用，请稍后重试"), true, httpStatus));
      reply->deleteLater();
      return;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (document.isNull() || !document.isObject()) {
      callback({}, httpStatus, error(MapErrorCategory::Parse, QStringLiteral("地图返回数据异常"), true, httpStatus));
      reply->deleteLater();
      return;
    }
    callback(document, httpStatus, {});
    reply->deleteLater();
  });
}

void TencentMapService::geocode(const QString &address, GeoCallback callback) {
  const QString value = address.trimmed();
  if (value.isEmpty()) { callback(MapResult<GeoCoordinate>::failure(error(MapErrorCategory::InvalidInput, QStringLiteral("请输入地址"), false))); return; }
  QUrlQuery query; query.addQueryItem(QStringLiteral("address"), value);
  requestJson(QStringLiteral("/ws/geocoder/v1/"), query, [callback = std::move(callback)](const QJsonDocument &document, int httpStatus, const MapError &requestError) {
    if (!requestError.userMessage.isEmpty()) { auto e = requestError; e.httpStatus = httpStatus; callback(MapResult<GeoCoordinate>::failure(e)); return; }
    const QJsonObject root = document.object(); const int status = root.value(QStringLiteral("status")).toInt(-1);
    if (status != 0) { const auto failure = apiFailureDetails(root, status); callback(MapResult<GeoCoordinate>::failure(error(MapErrorCategory::Api, failure.message, failure.retryable, httpStatus, status))); return; }
    GeoCoordinate coordinate; if (!readCoordinate(root.value(QStringLiteral("result")).toObject().value(QStringLiteral("location")).toObject(), &coordinate)) { callback(MapResult<GeoCoordinate>::failure(error(MapErrorCategory::Parse, QStringLiteral("地址坐标无效"), true, httpStatus, status))); return; }
    callback(MapResult<GeoCoordinate>::success(coordinate));
  });
}

void TencentMapService::searchNearbyChargingStations(const GeoCoordinate &center, int radiusMeters, PoiCallback callback) {
  if (!isValidCoordinate(center) || radiusMeters < 10 || radiusMeters > 1000) { callback(MapResult<QVector<MapPoi>>::failure(error(MapErrorCategory::InvalidInput, QStringLiteral("真实 POI 搜索半径必须在 10–1000 米之间"), false))); return; }
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("keyword"), QStringLiteral("充电站"));
  query.addQueryItem(QStringLiteral("boundary"), QStringLiteral("nearby(%1,%2,%3)").arg(center.latitude, 0, 'f', 6).arg(center.longitude, 0, 'f', 6).arg(radiusMeters));
  query.addQueryItem(QStringLiteral("orderby"), QStringLiteral("_distance")); query.addQueryItem(QStringLiteral("page_size"), QStringLiteral("20"));
  requestJson(QStringLiteral("/ws/place/v1/search"), query, [callback = std::move(callback)](const QJsonDocument &document, int httpStatus, const MapError &requestError) {
    if (!requestError.userMessage.isEmpty()) { auto e = requestError; e.httpStatus = httpStatus; callback(MapResult<QVector<MapPoi>>::failure(e)); return; }
    const QJsonObject root = document.object(); const int status = root.value(QStringLiteral("status")).toInt(-1);
    if (status != 0) { const auto failure = apiFailureDetails(root, status); callback(MapResult<QVector<MapPoi>>::failure(error(MapErrorCategory::Api, failure.message, failure.retryable, httpStatus, status))); return; }
    const auto data = root.value(QStringLiteral("data"));
    if (!data.isArray()) { callback(MapResult<QVector<MapPoi>>::failure(error(MapErrorCategory::Parse, QStringLiteral("附近 POI 字段异常"), true, httpStatus, status))); return; }
    QVector<MapPoi> result;
    for (const auto &value : data.toArray()) {
      MapPoi poi;
      if (readPoi(value.toObject(), &poi, MapSource::Tencent)) result.push_back(poi);
    }
    if (!data.toArray().isEmpty() && result.isEmpty()) { callback(MapResult<QVector<MapPoi>>::failure(error(MapErrorCategory::Parse, QStringLiteral("附近 POI 字段不完整"), true, httpStatus, status))); return; }
    callback(MapResult<QVector<MapPoi>>::success(result));
  });
}

void TencentMapService::getPoiDetail(const QString &poiId, PoiDetailCallback callback) {
  if (poiId.trimmed().isEmpty()) { callback(MapResult<MapPoi>::failure(error(MapErrorCategory::InvalidInput, QStringLiteral("POI 编号无效"), false))); return; }
  QUrlQuery query; query.addQueryItem(QStringLiteral("id"), poiId.trimmed());
  requestJson(QStringLiteral("/ws/place/v1/detail"), query, [callback = std::move(callback)](const QJsonDocument &document, int httpStatus, const MapError &requestError) {
    if (!requestError.userMessage.isEmpty()) { auto e = requestError; e.httpStatus = httpStatus; callback(MapResult<MapPoi>::failure(e)); return; }
    const QJsonObject root = document.object(); const int status = root.value(QStringLiteral("status")).toInt(-1);
    if (status != 0) { const auto failure = apiFailureDetails(root, status); callback(MapResult<MapPoi>::failure(error(MapErrorCategory::Api, failure.message, failure.retryable, httpStatus, status))); return; }
    const auto data = root.value(QStringLiteral("data"));
    QJsonObject object;
    if (data.isArray()) {
      const QJsonArray array = data.toArray();
      if (!array.isEmpty()) object = array.at(0).toObject();
    } else {
      object = data.toObject();
    }
    MapPoi poi; if (!readPoi(object, &poi, MapSource::Tencent)) { callback(MapResult<MapPoi>::failure(error(MapErrorCategory::Parse, QStringLiteral("POI 详情字段不完整"), true, httpStatus, status))); return; }
    callback(MapResult<MapPoi>::success(poi));
  });
}

void TencentMapService::queryRoute(const GeoCoordinate &origin, const GeoCoordinate &destination, RouteMode mode, RouteCallback callback) {
  if (!isValidCoordinate(origin) || !isValidCoordinate(destination)) { callback(MapResult<MapRoute>::failure(error(MapErrorCategory::InvalidInput, QStringLiteral("起点或终点坐标无效"), false))); return; }
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("from"), QStringLiteral("%1,%2").arg(origin.latitude, 0, 'f', 6).arg(origin.longitude, 0, 'f', 6));
  query.addQueryItem(QStringLiteral("to"), QStringLiteral("%1,%2").arg(destination.latitude, 0, 'f', 6).arg(destination.longitude, 0, 'f', 6));
  const QString path = mode == RouteMode::Driving ? QStringLiteral("/ws/direction/v1/driving/") : QStringLiteral("/ws/direction/v1/walking/");
  requestJson(path, query, [origin, destination, mode, callback = std::move(callback)](const QJsonDocument &document, int httpStatus, const MapError &requestError) {
    if (!requestError.userMessage.isEmpty()) { auto e = requestError; e.httpStatus = httpStatus; callback(MapResult<MapRoute>::failure(e)); return; }
    const QJsonObject root = document.object(); const int status = root.value(QStringLiteral("status")).toInt(-1);
    if (status != 0) { const auto failure = apiFailureDetails(root, status); callback(MapResult<MapRoute>::failure(error(MapErrorCategory::Api, failure.message, failure.retryable, httpStatus, status))); return; }
    const QJsonArray routes = root.value(QStringLiteral("result")).toObject().value(QStringLiteral("routes")).toArray();
    if (routes.isEmpty()) { callback(MapResult<MapRoute>::failure(error(MapErrorCategory::Api, QStringLiteral("暂无可用路线"), true, httpStatus, status))); return; }
    const QJsonObject routeObject = routes.first().toObject();
    if (!validNumber(routeObject.value(QStringLiteral("distance"))) || !validNumber(routeObject.value(QStringLiteral("duration")))) { callback(MapResult<MapRoute>::failure(error(MapErrorCategory::Parse, QStringLiteral("路线距离或时间缺失"), true, httpStatus, status))); return; }
    const double distance = routeObject.value(QStringLiteral("distance")).toDouble();
    const double durationMinutes = routeObject.value(QStringLiteral("duration")).toDouble();
    if (distance < 0.0 || durationMinutes < 0.0 || durationMinutes > 35791394.0) { callback(MapResult<MapRoute>::failure(error(MapErrorCategory::Parse, QStringLiteral("路线距离或时间无效"), true, httpStatus, status))); return; }
    MapRoute route; route.mode = mode; route.distanceMeters = qRound64(distance); route.durationSeconds = qRound(durationMinutes * 60.0); route.polyline = readPolyline(routeObject); route.source = MapSource::Tencent; route.summary = QStringLiteral("腾讯地图 %1 路线").arg(mode == RouteMode::Driving ? QStringLiteral("驾车") : QStringLiteral("步行"));
    Q_UNUSED(origin); Q_UNUSED(destination); callback(MapResult<MapRoute>::success(route));
  });
}

} // namespace ev
