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
  explicit MapProtocolServer(QObject *parent = nullptr) : QObject(parent) {
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
                {QStringLiteral("data_source"), QStringLiteral("tencent_live")},
                {QStringLiteral("resolved_origin"),
                 QJsonObject{{QStringLiteral("latitude"), origin.value(QStringLiteral("latitude")).toDouble(22.53)},
                             {QStringLiteral("longitude"), origin.value(QStringLiteral("longitude")).toDouble(113.93)}}},
                {QStringLiteral("stations"), QJsonArray{
                    QJsonObject{{QStringLiteral("id"), 42}, {QStringLiteral("name"), QStringLiteral("服务端充电站")},
                                {QStringLiteral("address"), QStringLiteral("服务端路 1 号")},
                                {QStringLiteral("latitude"), 22.54}, {QStringLiteral("longitude"), 113.94},
                                {QStringLiteral("distance_meters"), 320}}}},
                {QStringLiteral("warning"), QJsonValue()}};
          } else if (request.type == QStringLiteral("map.route.plan")) {
            response.payload = {
                {QStringLiteral("data_source"), QStringLiteral("tencent_cache")},
                {QStringLiteral("distance_meters"), 1250},
                {QStringLiteral("duration_seconds"), 180},
                {QStringLiteral("polyline"), QJsonArray{QJsonArray{22.53, 113.93}, QJsonArray{22.54, 113.94}}},
                {QStringLiteral("warning"), QJsonObject{{QStringLiteral("message"), QStringLiteral("使用服务端缓存")}}}};
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

    MapResult<MapRoute> route;
    service.queryRoute({22.53, 113.93}, {22.54, 113.94}, RouteMode::Walking,
                       [&route](const auto &result) { route = result; });
    QTRY_VERIFY_WITH_TIMEOUT(route.ok, 2000);
    QCOMPARE(route.value.source, MapSource::Server);
    QCOMPARE(route.value.mode, RouteMode::Walking);
    QCOMPARE(route.value.distanceMeters, qint64(1250));
    QCOMPARE(route.value.durationSeconds, 180);
    QCOMPARE(route.value.polyline.size(), 2);
    QVERIFY(route.notice.contains(QStringLiteral("缓存")));
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
};

QTEST_MAIN(ServerMapServiceTest)
#include "server_map_service_test.moc"
