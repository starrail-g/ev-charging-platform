#include "server_map_service.h"

#include "ev_protocol/frame_codec.h"
#include "ev_protocol/message.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QTcpSocket>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <utility>

namespace ev {
using namespace ev::protocol;

namespace {

QString envValue(const char *name, const QString &fallback) {
  const QString value = qEnvironmentVariable(name).trimmed();
  return value.isEmpty() ? fallback : value;
}

quint16 envPort(quint16 fallback) {
  bool ok = false;
  const uint value = qEnvironmentVariable("EV_SERVER_PORT", QString::number(fallback)).toUInt(&ok);
  return ok && value > 0 && value <= 65535 ? static_cast<quint16>(value) : fallback;
}

MapErrorCategory categoryForCode(int code) {
  if (code == 1100) return MapErrorCategory::InvalidInput;
  if (code == 1401) return MapErrorCategory::MissingKey;
  if (code == 1402) return MapErrorCategory::Timeout;
  if (code == 1403) return MapErrorCategory::Network;
  if (code == 1400 || code == 1404 || code == 1405 || code == 1406 ||
      code == 1408 || code == 1410) return MapErrorCategory::Api;
  if (code == 1407 || code == 1409) return MapErrorCategory::Parse;
  if (code == 1000 || code == 1001 || code == 1002 || code == 1003) return MapErrorCategory::Parse;
  return MapErrorCategory::Network;
}

bool finiteNumber(const QJsonValue &value) {
  return value.isDouble() && std::isfinite(value.toDouble());
}

} // namespace

ServerMapService::ServerMapService(QString host, quint16 port, int timeoutMs, QObject *parent)
    : QObject(parent), host_(host.trimmed()), port_(port), timeoutMs_(qMax(1, timeoutMs)) {
  if (host_.isEmpty()) host_ = envValue("EV_SERVER_HOST", QStringLiteral("127.0.0.1"));
  if (port_ == 0) port_ = envPort(45454);
}

ServerMapService::~ServerMapService() {
  for (auto *watcher : std::as_const(watchers_)) {
    if (watcher) watcher->waitForFinished();
  }
}

QString ServerMapService::wireId(const QString &id) {
  bool ok = false;
  const qlonglong value = id.trimmed().toLongLong(&ok);
  return ok && value > 0 ? QString::number(value) : QString();
}

ServerMapService::Reply ServerMapService::request(const QString &host, quint16 port, int timeoutMs,
                                                  const QString &type, const QJsonObject &payload) {
  QTcpSocket socket;
  socket.connectToHost(host, port);
  if (!socket.waitForConnected(timeoutMs)) return {false, {}, 1500, QStringLiteral("服务端地图连接失败")};
  const QString id = QStringLiteral("map-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  const QByteArray frame = encodeFrame(Message{kProtocolVersion, id, type, payload});
  qint64 written = 0;
  while (written < frame.size()) {
    const qint64 chunk = socket.write(frame.constData() + written, frame.size() - written);
    if (chunk <= 0 || !socket.waitForBytesWritten(timeoutMs))
      return {false, {}, 1500, QStringLiteral("服务端地图请求发送失败")};
    written += chunk;
  }
  FrameDecoder decoder;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    if (!socket.waitForReadyRead(qMax(1, timeoutMs - int(timer.elapsed())))) break;
    QString frameError;
    ErrorCode frameCode = ErrorCode::Ok;
    const auto messages = decoder.feed(socket.readAll(), &frameError, &frameCode);
    if (!frameError.isEmpty()) return {false, {}, static_cast<int>(frameCode), QStringLiteral("服务端地图协议错误")};
    for (const auto &message : messages) {
      if (message.id != id) continue;
      if (message.type == QStringLiteral("error")) {
        const int code = message.payload.value(QStringLiteral("code")).toInt(1500);
        return {false, {}, code, message.payload.value(QStringLiteral("message")).toString(QStringLiteral("地图服务请求失败"))};
      }
      if (message.type != type + QStringLiteral(".result"))
        return {false, {}, 1002, QStringLiteral("服务端地图响应类型错误")};
      return {true, message.payload, 0, {}};
    }
  }
  return {false, {}, 1402, QStringLiteral("服务端地图请求超时")};
}

MapError ServerMapService::mapError(int code, const QString &message) {
  MapErrorCategory category = categoryForCode(code);
  const bool retryable = code == 1402 || code == 1403 || code == 1408 || code == 1500;
  return {category, message.isEmpty() ? QStringLiteral("服务端地图暂不可用") : message, retryable, 0, code};
}

bool ServerMapService::readResponseMetadata(const QJsonObject &payload, MapSource *source,
                                            QString *dataSource, MapWarning *warning) {
  if (!source || !dataSource || !warning || payload.value(QStringLiteral("provider")).toString() != QStringLiteral("tencent"))
    return false;
  const QString value = payload.value(QStringLiteral("data_source")).toString();
  // `tencent_live` describes the server's upstream result, not a client-side
  // Tencent page. All server-owned sources stay in the Server presentation
  // path; the exact provider value is retained separately in dataSource.
  if (value == QStringLiteral("tencent_live")) *source = MapSource::Server;
  else if (value == QStringLiteral("tencent_cache") || value == QStringLiteral("tencent_stale")) *source = MapSource::Server;
  else if (value == QStringLiteral("server_mock")) *source = MapSource::Mock;
  else return false;
  *dataSource = value;

  *warning = {};
  const QJsonValue warningValue = payload.value(QStringLiteral("warning"));
  if (!warningValue.isNull() && !warningValue.isUndefined()) {
    if (!warningValue.isObject()) return false;
    const QJsonObject object = warningValue.toObject();
    if (!object.value(QStringLiteral("code")).isDouble() ||
        !object.value(QStringLiteral("name")).isString() ||
        !object.value(QStringLiteral("message")).isString() ||
        !object.value(QStringLiteral("retryable")).isBool() ||
        !object.value(QStringLiteral("degraded")).isBool()) return false;
    warning->code = object.value(QStringLiteral("code")).toInt();
    warning->name = object.value(QStringLiteral("name")).toString();
    warning->message = object.value(QStringLiteral("message")).toString();
    warning->retryable = object.value(QStringLiteral("retryable")).toBool();
    warning->degraded = object.value(QStringLiteral("degraded")).toBool();
    if (warning->code <= 0 || warning->name.isEmpty() || warning->message.isEmpty()) return false;
  }
  if (value == QStringLiteral("server_mock"))
    return warning->code == 1410 && warning->degraded;
  if (value == QStringLiteral("tencent_stale"))
    return warning->isPresent() && warning->degraded;
  return !warning->isPresent();
}

bool ServerMapService::readCoordinate(const QJsonObject &object, GeoCoordinate *coordinate) {
  if (!coordinate || !finiteNumber(object.value(QStringLiteral("latitude"))) ||
      !finiteNumber(object.value(QStringLiteral("longitude")))) return false;
  *coordinate = {object.value(QStringLiteral("latitude")).toDouble(), object.value(QStringLiteral("longitude")).toDouble()};
  return isValidCoordinate(*coordinate);
}

void ServerMapService::remember(QFutureWatcher<Reply> *watcher) {
  if (watcher) watchers_.append(watcher);
}

void ServerMapService::cancelPending() {
  ++generation_;
  for (auto *watcher : std::as_const(watchers_)) if (watcher) watcher->cancel();
  lastPois_.clear();
}

void ServerMapService::geocode(const QString &address, GeoCallback callback) {
  const quint64 generation = generation_;
  const QString user = wireId(userId_);
  const QString value = address.trimmed();
  if (user.isEmpty() || value.isEmpty()) {
    callback(MapResult<GeoCoordinate>::failure(mapError(user.isEmpty() ? 1100 : 1002,
        user.isEmpty() ? QStringLiteral("请先登录") : QStringLiteral("请输入地址"))));
    return;
  }
  const QString host = host_; const quint16 port = port_; const int timeout = timeoutMs_;
  auto *watcher = new QFutureWatcher<Reply>(this); remember(watcher);
  connect(watcher, &QFutureWatcher<Reply>::finished, this, [this, watcher, generation, callback = std::move(callback)] {
    const Reply reply = watcher->result(); watcher->deleteLater(); watchers_.removeAll(watcher);
    if (generation != generation_) return;
    if (!reply.ok) { callback(MapResult<GeoCoordinate>::failure(mapError(reply.code, reply.error))); return; }
    GeoCoordinate coordinate; MapSource source; QString dataSource; MapWarning warning;
    if (!readResponseMetadata(reply.payload, &source, &dataSource, &warning)) {
      callback(MapResult<GeoCoordinate>::failure(mapError(1407, QStringLiteral("服务端地图元数据无效")))); return;
    }
    Q_UNUSED(source);
    if (!readCoordinate(reply.payload.value(QStringLiteral("resolved_origin")).toObject(), &coordinate)) {
      callback(MapResult<GeoCoordinate>::failure(mapError(1407, QStringLiteral("服务端返回的定位坐标无效")))); return;
    }
    MapResult<GeoCoordinate> result = MapResult<GeoCoordinate>::success(coordinate);
    result.notice = warning.message; result.dataSource = dataSource; result.warning = warning;
    callback(result);
  });
  watcher->setFuture(QtConcurrent::run([host, port, timeout, user, value] {
    return request(host, port, timeout, QStringLiteral("map.station.search"),
                   {{QStringLiteral("user_id"), user.toLongLong()}, {QStringLiteral("origin"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("address")}, {QStringLiteral("value"), value}}},
                    {QStringLiteral("radius_meters"), 1000}, {QStringLiteral("page_size"), 1}, {QStringLiteral("page_token"), QJsonValue()}});
  }));
}

void ServerMapService::searchNearbyChargingStations(const GeoCoordinate &center, int radiusMeters, PoiCallback callback) {
  const quint64 generation = generation_;
  const QString user = wireId(userId_);
  if (user.isEmpty()) { callback(MapResult<QVector<MapPoi>>::failure(mapError(1100, QStringLiteral("请先登录")))); return; }
  if (!isValidCoordinate(center) || radiusMeters < 10 || radiusMeters > 1000) {
    callback(MapResult<QVector<MapPoi>>::failure(mapError(1002, QStringLiteral("搜索坐标或半径无效")))); return;
  }
  const QString host = host_; const quint16 port = port_; const int timeout = timeoutMs_;
  auto *watcher = new QFutureWatcher<Reply>(this); remember(watcher);
  connect(watcher, &QFutureWatcher<Reply>::finished, this, [this, watcher, generation, callback = std::move(callback)] {
    const Reply reply = watcher->result(); watcher->deleteLater(); watchers_.removeAll(watcher);
    if (generation != generation_) return;
    if (!reply.ok) { callback(MapResult<QVector<MapPoi>>::failure(mapError(reply.code, reply.error))); return; }
    MapSource source; QString dataSource; MapWarning warning;
    if (!readResponseMetadata(reply.payload, &source, &dataSource, &warning)) {
      callback(MapResult<QVector<MapPoi>>::failure(mapError(1407, QStringLiteral("服务端地图元数据无效")))); return;
    }
    const auto array = reply.payload.value(QStringLiteral("stations")).toArray();
    QVector<MapPoi> result;
    for (const auto &value : array) {
      const auto object = value.toObject(); MapPoi poi;
      poi.id = QString::number(object.value(QStringLiteral("id")).toInteger());
      poi.title = object.value(QStringLiteral("name")).toString();
      poi.address = object.value(QStringLiteral("address")).toString();
      if (poi.id == QStringLiteral("0") || poi.title.isEmpty() || poi.address.isEmpty() || !readCoordinate(object, &poi.coordinate)) continue;
      poi.distanceMeters = object.value(QStringLiteral("distance_meters")).toInteger(-1);
      poi.source = source; result.push_back(poi);
    }
    if (!array.isEmpty() && result.isEmpty()) { callback(MapResult<QVector<MapPoi>>::failure(mapError(1407, QStringLiteral("服务端站点字段无效")))); return; }
    lastPois_ = result;
    MapResult<QVector<MapPoi>> mapped = MapResult<QVector<MapPoi>>::success(result);
    mapped.notice = warning.message; mapped.dataSource = dataSource; mapped.warning = warning;
    callback(mapped);
  });
  watcher->setFuture(QtConcurrent::run([host, port, timeout, user, center, radiusMeters] {
    return request(host, port, timeout, QStringLiteral("map.station.search"),
                   {{QStringLiteral("user_id"), user.toLongLong()}, {QStringLiteral("origin"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("coordinate")}, {QStringLiteral("latitude"), center.latitude}, {QStringLiteral("longitude"), center.longitude}}},
                    {QStringLiteral("radius_meters"), radiusMeters}, {QStringLiteral("page_size"), 20}, {QStringLiteral("page_token"), QJsonValue()}});
  }));
}

void ServerMapService::getPoiDetail(const QString &poiId, PoiDetailCallback callback) {
  for (const auto &poi : std::as_const(lastPois_)) if (poi.id == poiId.trimmed()) {
    callback(MapResult<MapPoi>::success(poi)); return;
  }
  callback(MapResult<MapPoi>::failure(mapError(1200, QStringLiteral("该地图站点详情不在当前结果中"))));
}

void ServerMapService::queryRoute(const GeoCoordinate &origin, const GeoCoordinate &destination,
                                  RouteMode mode, RouteCallback callback) {
  const quint64 generation = generation_;
  const QString user = wireId(userId_);
  const QString station = wireId(targetStationId_);
  if (user.isEmpty()) { callback(MapResult<MapRoute>::failure(mapError(1100, QStringLiteral("请先登录")))); return; }
  if (station.isEmpty() || !isValidCoordinate(origin) || !isValidCoordinate(destination)) {
    callback(MapResult<MapRoute>::failure(mapError(1002, QStringLiteral("路线起点或目标站点无效")))); return;
  }
  const QString host = host_; const quint16 port = port_; const int timeout = timeoutMs_;
  auto *watcher = new QFutureWatcher<Reply>(this); remember(watcher);
  connect(watcher, &QFutureWatcher<Reply>::finished, this, [this, watcher, generation, mode, callback = std::move(callback)] {
    const Reply reply = watcher->result(); watcher->deleteLater(); watchers_.removeAll(watcher);
    if (generation != generation_) return;
    if (!reply.ok) { callback(MapResult<MapRoute>::failure(mapError(reply.code, reply.error))); return; }
    MapSource source; QString dataSource; MapWarning warning;
    if (!readResponseMetadata(reply.payload, &source, &dataSource, &warning)) {
      callback(MapResult<MapRoute>::failure(mapError(1407, QStringLiteral("服务端地图元数据无效")))); return;
    }
    MapRoute route; route.mode = mode; route.source = source;
    route.distanceMeters = reply.payload.value(QStringLiteral("distance_meters")).toInteger(-1);
    route.durationSeconds = reply.payload.value(QStringLiteral("duration_seconds")).toInt(-1);
    const auto line = reply.payload.value(QStringLiteral("polyline")).toArray();
    for (const auto &point : line) {
      const auto pair = point.toArray(); GeoCoordinate coordinatePoint;
      if (pair.size() != 2 || !finiteNumber(pair.at(0)) || !finiteNumber(pair.at(1))) { route.polyline.clear(); break; }
      coordinatePoint = {pair.at(0).toDouble(), pair.at(1).toDouble()};
      if (!isValidCoordinate(coordinatePoint)) { route.polyline.clear(); break; }
      route.polyline.push_back(coordinatePoint);
    }
    if (route.distanceMeters < 0 || route.durationSeconds < 0 || route.polyline.size() == 1) {
      callback(MapResult<MapRoute>::failure(mapError(1407, QStringLiteral("服务端路线字段无效")))); return;
    }
    route.summary = QStringLiteral("服务端地图 %1 路线").arg(mode == RouteMode::Driving ? QStringLiteral("驾车") : QStringLiteral("步行"));
    MapResult<MapRoute> mapped = MapResult<MapRoute>::success(route);
    mapped.notice = warning.message; mapped.dataSource = dataSource; mapped.warning = warning; callback(mapped);
  });
  watcher->setFuture(QtConcurrent::run([host, port, timeout, user, station, origin, mode] {
    return request(host, port, timeout, QStringLiteral("map.route.plan"),
                   {{QStringLiteral("user_id"), user.toLongLong()}, {QStringLiteral("origin"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("coordinate")}, {QStringLiteral("latitude"), origin.latitude}, {QStringLiteral("longitude"), origin.longitude}}},
                    {QStringLiteral("station_id"), station.toLongLong()}, {QStringLiteral("mode"), mode == RouteMode::Driving ? QStringLiteral("driving") : QStringLiteral("walking")}});
  }));
}

} // namespace ev
