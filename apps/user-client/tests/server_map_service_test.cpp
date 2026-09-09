#include "server_map_service.h"

#include "ev_protocol/frame_codec.h"
#include "ev_protocol/message.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace ev;
using namespace ev::protocol;

namespace {

class MapProtocolServer final : public QObject {
public:
  explicit MapProtocolServer(QObject *parent = nullptr, bool serverMock = false)
      : QObject(parent), serverMock_(serverMock) {
    connect(&server_, &QTcpServer::newConnection, this, [this] {
      auto *socket = server_.nextPendingConnection();
      connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        QString error;
        const auto messages = decoder_.feed(socket->readAll(), &error);
        for (const auto &request : messages) {
          Message response;
          response.id = request.id;
          response.type = request.type + QStringLiteral(".result");
          if (request.type == QStringLiteral("map.station.search")) {
            const auto origin = request.payload.value(QStringLiteral("origin")).toObject();
            response.payload = {
                {QStringLiteral("provider"), QStringLiteral("tencent")},
                {QStringLiteral("data_source"), serverMock_ ? QStringLiteral("server_mock") : QStringLiteral("tencent_live")},
                {QStringLiteral("resolved_origin"),
                 QJsonObject{{QStringLiteral("latitude"), origin.value(QStringLiteral("latitude")).toDouble(22.53)},
                             {QStringLiteral("longitude"), origin.value(QStringLiteral("longitude")).toDouble(113.93)}}},
                {QStringLiteral("stations"), QJsonArray{
                    QJsonObject{{QStringLiteral("id"), 42}, {QStringLiteral("name"), QStringLiteral("服务端充电站")},
                                {QStringLiteral("address"), QStringLiteral("服务端路 1 号")},
                                {QStringLiteral("latitude"), 22.54}, {QStringLiteral("longitude"), 113.94},
                                {QStringLiteral("distance_meters"), 320}}}},
                {QStringLiteral("warning"), serverMock_ ? QJsonValue(QJsonObject{
                    {QStringLiteral("code"), 1410}, {QStringLiteral("name"), QStringLiteral("MAP_SERVER_MOCK")},
                    {QStringLiteral("message"), QStringLiteral("服务端演示数据")},
                    {QStringLiteral("retryable"), false}, {QStringLiteral("degraded"), true}})
                    : QJsonValue(QJsonValue::Null)}};
          } else if (request.type == QStringLiteral("map.route.plan")) {
            response.payload = {
                {QStringLiteral("provider"), QStringLiteral("tencent")},
                {QStringLiteral("data_source"), serverMock_ ? QStringLiteral("server_mock") : QStringLiteral("tencent_cache")},
                {QStringLiteral("distance_meters"), 1250},
                {QStringLiteral("duration_seconds"), 180},
                {QStringLiteral("polyline"), QJsonArray{QJsonArray{22.53, 113.93}, QJsonArray{22.54, 113.94}}},
                {QStringLiteral("warning"), serverMock_ ? QJsonValue(QJsonObject{
                    {QStringLiteral("code"), 1410}, {QStringLiteral("name"), QStringLiteral("MAP_SERVER_MOCK")},
                    {QStringLiteral("message"), QStringLiteral("服务端演示数据")},
                    {QStringLiteral("retryable"), false}, {QStringLiteral("degraded"), true}})
                    : QJsonValue(QJsonValue::Null)}};
          }
          socket->write(encodeFrame(response));
          socket->flush();
        }
      });
    });
  }

  bool start() { return server_.listen(QHostAddress::LocalHost, 0); }
  quint16 port() const { return server_.serverPort(); }

private:
  QTcpServer server_;
  FrameDecoder decoder_;
  bool serverMock_{false};
};

} // namespace

class ServerMapServiceTest final : public QObject {
  Q_OBJECT
private slots:
  void protocolMappingUsesServerAndNoTencentCredential() {
    MapProtocolServer server;
    QVERIFY(server.start());
    ServerMapService service(QStringLiteral("127.0.0.1"), server.port(), 1000);
    service.setUserId(QStringLiteral("7"));
    service.setTargetStationId(QStringLiteral("42"));

    MapResult<QVector<MapPoi>> stations;
    service.searchNearbyChargingStations({22.53, 113.93}, 1000,
                                         [&stations](const auto &result) { stations = result; });
    QTRY_VERIFY_WITH_TIMEOUT(stations.ok, 2000);
    QCOMPARE(stations.value.size(), 1);
    QCOMPARE(stations.value.first().id, QStringLiteral("42"));
    QCOMPARE(stations.value.first().source, MapSource::Server);
    QCOMPARE(stations.dataSource, QStringLiteral("tencent_live"));
    QVERIFY(!stations.warning.isPresent());

    MapResult<MapRoute> route;
    service.queryRoute({22.53, 113.93}, {22.54, 113.94}, RouteMode::Walking,
                       [&route](const auto &result) { route = result; });
    QTRY_VERIFY_WITH_TIMEOUT(route.ok, 2000);
    QCOMPARE(route.value.source, MapSource::Server);
    QCOMPARE(route.value.mode, RouteMode::Walking);
    QCOMPARE(route.value.distanceMeters, qint64(1250));
    QCOMPARE(route.value.durationSeconds, 180);
    QCOMPARE(route.value.polyline.size(), 2);
    QCOMPARE(route.dataSource, QStringLiteral("tencent_cache"));
    QVERIFY(route.notice.isEmpty());
    QVERIFY(!route.warning.isPresent());
  }

  void missingContextFailsWithoutNetwork() {
    ServerMapService service(QStringLiteral("127.0.0.1"), 1, 20);
    MapResult<QVector<MapPoi>> result;
    bool called = false;
    service.searchNearbyChargingStations({22.53, 113.93}, 1000,
        [&result, &called](const auto &value) { result = value; called = true; });
    QVERIFY(called);
    QVERIFY(!result.ok);
    QCOMPARE(result.error.category, MapErrorCategory::InvalidInput);
  }

  void serverMockWarningIsStructured() {
    MapProtocolServer server(nullptr, true);
    QVERIFY(server.start());
    ServerMapService service(QStringLiteral("127.0.0.1"), server.port(), 1000);
    service.setUserId(QStringLiteral("7"));
    MapResult<QVector<MapPoi>> stations;
    service.searchNearbyChargingStations({22.53, 113.93}, 1000,
                                         [&stations](const auto &result) { stations = result; });
    QTRY_VERIFY_WITH_TIMEOUT(stations.ok, 2000);
    QCOMPARE(stations.dataSource, QStringLiteral("server_mock"));
    QCOMPARE(stations.warning.code, 1410);
    QVERIFY(stations.warning.degraded);
  }

  void serverRuntimeIntegration() {
    if (qEnvironmentVariable("EV_RUN_MAP_SOCKET_INTEGRATION") != QStringLiteral("1"))
      QSKIP("set EV_RUN_MAP_SOCKET_INTEGRATION=1 to run against the B map service");

    const QString userId = qEnvironmentVariable("EV_MAP_TEST_USER_ID").trimmed();
    QVERIFY2(!userId.isEmpty(), "EV_MAP_TEST_USER_ID must identify an existing server user");

    ServerMapService service({}, 0, 3000);
    service.setUserId(userId);
    const auto verifyMetadata = [](const auto &result) {
      const QString source = result.dataSource;
      QVERIFY(source == QStringLiteral("tencent_live") ||
              source == QStringLiteral("tencent_cache") ||
              source == QStringLiteral("tencent_stale") ||
              source == QStringLiteral("server_mock"));
      if (source == QStringLiteral("server_mock")) {
        QCOMPARE(result.warning.code, 1410);
        QVERIFY(result.warning.degraded);
      } else if (source == QStringLiteral("tencent_stale")) {
        QVERIFY(result.warning.isPresent());
        QVERIFY(result.warning.degraded);
      } else {
        QVERIFY(!result.warning.isPresent());
      }
    };

    bool geocodeDone = false;
    MapResult<GeoCoordinate> geocode;
    service.geocode(QStringLiteral("沈阳市浑南区软件园"),
                    [&geocode, &geocodeDone](const auto &result) {
                      geocode = result;
                      geocodeDone = true;
                    });
    QTRY_VERIFY_WITH_TIMEOUT(geocodeDone, 5000);
    QVERIFY2(geocode.ok, qPrintable(geocode.error.userMessage));
    verifyMetadata(geocode);

    bool searchDone = false;
    MapResult<QVector<MapPoi>> stations;
    service.searchNearbyChargingStations(geocode.value, 1000,
                                         [&stations, &searchDone](const auto &result) {
                                           stations = result;
                                           searchDone = true;
                                         });
    QTRY_VERIFY_WITH_TIMEOUT(searchDone, 5000);
    QVERIFY2(stations.ok, qPrintable(stations.error.userMessage));
    QVERIFY(!stations.value.isEmpty());
    verifyMetadata(stations);

    const MapPoi target = stations.value.first();
    service.setTargetStationId(target.id);
    for (const RouteMode mode : {RouteMode::Driving, RouteMode::Walking}) {
      bool routeDone = false;
      MapResult<MapRoute> route;
      service.queryRoute(geocode.value, target.coordinate, mode,
                         [&route, &routeDone](const auto &result) {
                           route = result;
                           routeDone = true;
                         });
      QTRY_VERIFY_WITH_TIMEOUT(routeDone, 5000);
      QVERIFY2(route.ok, qPrintable(route.error.userMessage));
      QCOMPARE(route.value.mode, mode);
      QVERIFY(route.value.distanceMeters >= 0);
      QVERIFY(route.value.durationSeconds >= 0);
      QVERIFY(route.value.polyline.size() >= 2);
      verifyMetadata(route);
    }
  }
};

QTEST_MAIN(ServerMapServiceTest)
#include "server_map_service_test.moc"
