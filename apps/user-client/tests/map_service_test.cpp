#include "map_service.h"
#include "map_web_view.h"
#include "tencent_map_service.h"

#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace ev;

namespace {

class EnvironmentGuard final {
public:
  EnvironmentGuard() {
    keySet_ = qEnvironmentVariableIsSet("TENCENT_MAP_KEY");
    enabledSet_ = qEnvironmentVariableIsSet("TENCENT_MAP_ENABLED");
    key_ = qgetenv("TENCENT_MAP_KEY");
    enabled_ = qgetenv("TENCENT_MAP_ENABLED");
  }
  ~EnvironmentGuard() {
    if (keySet_) qputenv("TENCENT_MAP_KEY", key_);
    else qunsetenv("TENCENT_MAP_KEY");
    if (enabledSet_) qputenv("TENCENT_MAP_ENABLED", enabled_);
    else qunsetenv("TENCENT_MAP_ENABLED");
  }
private:
  bool keySet_{};
  bool enabledSet_{};
  QByteArray key_;
  QByteArray enabled_;
};

class FakeHttpServer final : public QObject {
public:
  explicit FakeHttpServer(const QByteArray &body, int statusCode = 200, bool respond = true,
                          QObject *parent = nullptr)
      : QObject(parent), body_(body), statusCode_(statusCode), respond_(respond) {
    connect(&server_, &QTcpServer::newConnection, this, [this] {
      auto *socket = server_.nextPendingConnection();
      connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        const QByteArray request = socket->readAll();
        const QString path = QString::fromLatin1(request).section(QChar(' '), 1, 1);
        if (!path.isEmpty()) paths_.push_back(path);
        if (!respond_) return;
        const QByteArray reason = statusCode_ == 200 ? QByteArrayLiteral("OK") : QByteArrayLiteral("ERROR");
        const QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode_) + " " + reason
            + "\r\nContent-Type: application/json\r\nContent-Length: "
            + QByteArray::number(body_.size()) + "\r\nConnection: close\r\n\r\n" + body_;
        socket->write(response);
        socket->disconnectFromHost();
      });
    });
  }

  bool start() { return server_.listen(QHostAddress::LocalHost, 0); }
  QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()); }
  const QStringList &paths() const { return paths_; }

private:
  QTcpServer server_;
  QByteArray body_;
  int statusCode_{200};
  bool respond_{true};
  QStringList paths_;
};

void enableTestKey() {
  qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("unit-test-key-123"));
  qputenv("TENCENT_MAP_ENABLED", QByteArrayLiteral("1"));
}

} // namespace

class MapServiceTest final : public QObject {
  Q_OBJECT
private slots:
  void coordinateValidation() {
    QVERIFY(isValidCoordinate({0.0, 0.0}));
    QVERIFY(isValidCoordinate({90.0, 180.0}));
    QVERIFY(!isValidCoordinate({91.0, 0.0}));
    QVERIFY(!isValidCoordinate({0.0, 181.0}));
    QVERIFY(!isValidCoordinate({qInf(), 0.0}));

    Station business;
    business.id = QStringLiteral("s001");
    business.name = QStringLiteral("科技园充电站");
    business.address = QStringLiteral("科苑路 1 号");
    business.latitude = 22.5401;
    business.longitude = 113.9345;
    const MapPoi byName{QStringLiteral("p1"), QStringLiteral("科技园充电站"), QStringLiteral("其他地址"),
                        {22.0, 113.0}, 100, MapSource::Tencent};
    QCOMPARE(matchingBusinessStationId(byName, {business}), QStringLiteral("s001"));
    const MapPoi byCoordinate{QStringLiteral("p2"), QStringLiteral("其他充电站"), QStringLiteral("其他地址"),
                              {22.5402, 113.9344}, 100, MapSource::Tencent};
    QCOMPARE(matchingBusinessStationId(byCoordinate, {business}), QStringLiteral("s001"));
    const MapPoi unrelated{QStringLiteral("p3"), QStringLiteral("未关联充电站"), QStringLiteral("未知路"),
                           {23.0, 114.0}, 100, MapSource::Tencent};
    QVERIFY(matchingBusinessStationId(unrelated, {business}).isEmpty());
    QCOMPARE(business.id, QStringLiteral("s001"));
    QCOMPARE(business.name, QStringLiteral("科技园充电站"));
  }

  void configurationPriorityAndRedaction() {
    EnvironmentGuard guard;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString configPath = dir.filePath(QStringLiteral("local.env"));
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write("TENCENT_MAP_KEY=local-config-key-456\nTENCENT_MAP_ENABLED=1\n");
    config.close();

    qunsetenv("TENCENT_MAP_KEY");
    qunsetenv("TENCENT_MAP_ENABLED");
    TencentMapService local(nullptr, {}, 100, configPath);
    QVERIFY(local.configured());

    qputenv("TENCENT_MAP_KEY", QByteArray());
    TencentMapService emptyEnvironment(nullptr, {}, 100, configPath);
    QVERIFY(!emptyEnvironment.configured());
    QVERIFY(emptyEnvironment.configurationMessage().contains(QStringLiteral("Key")));

    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("replace-with-local-key"));
    TencentMapService placeholder(nullptr, {}, 100, configPath);
    QVERIFY(!placeholder.configured());

    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("environment-key-789"));
    qputenv("TENCENT_MAP_ENABLED", QByteArrayLiteral("0"));
    TencentMapService disabled(nullptr, {}, 100, configPath);
    QVERIFY(!disabled.configured());
    QVERIFY(disabled.configurationMessage().contains(QStringLiteral("Mock")));
    QVERIFY(!disabled.configurationMessage().contains(QStringLiteral("environment-key-789")));
  }

  void mockDataAndModesAreDeterministic() {
    MockMapService service;
    MapResult<GeoCoordinate> geocode;
    service.geocode(QStringLiteral("深圳市南山区"), [&geocode](const auto &result) { geocode = result; });
    QTRY_VERIFY_WITH_TIMEOUT(geocode.ok, 1000);
    QCOMPARE(geocode.value.latitude, 22.5300);

    MapResult<QVector<MapPoi>> pois;
    service.searchNearbyChargingStations(geocode.value, 5000, [&pois](const auto &result) { pois = result; });
    QTRY_VERIFY_WITH_TIMEOUT(pois.ok, 1000);
    QCOMPARE(pois.value.size(), 2);
    QCOMPARE(pois.value.at(0).source, MapSource::Mock);

    MapResult<MapRoute> driving;
    MapResult<MapRoute> walking;
    service.queryRoute({22.530, 113.930}, {22.540, 113.934}, RouteMode::Driving,
                       [&driving](const auto &result) { driving = result; });
    service.queryRoute({22.530, 113.930}, {22.540, 113.934}, RouteMode::Walking,
                       [&walking](const auto &result) { walking = result; });
    QTRY_VERIFY_WITH_TIMEOUT(driving.ok && walking.ok, 1000);
    QCOMPARE(driving.value.mode, RouteMode::Driving);
    QCOMPARE(walking.value.mode, RouteMode::Walking);
    QVERIFY(driving.value.distanceMeters != walking.value.distanceMeters);
    QCOMPARE(driving.value.source, MapSource::Offline);

    bool cancelledCallback = false;
    service.geocode(QStringLiteral("旧地址"), [&cancelledCallback](const auto &) { cancelledCallback = true; });
    service.cancelPending();
    QTest::qWait(10);
    QVERIFY(!cancelledCallback);
  }

  void tencentGeocodeAndPoiValidation() {
    EnvironmentGuard guard;
    enableTestKey();
    FakeHttpServer geocodeServer(R"({"status":0,"result":{"location":{"lat":22.5301,"lng":113.9302}}})");
    QVERIFY(geocodeServer.start());
    TencentMapService geocodeService(nullptr, geocodeServer.baseUrl(), 500);
    MapResult<GeoCoordinate> geocode;
    geocodeService.geocode(QStringLiteral("科技园"), [&geocode](const auto &value) { geocode = value; });
    QTRY_VERIFY_WITH_TIMEOUT(geocode.ok, 1000);
    QCOMPARE(geocode.value.latitude, 22.5301);
    QTRY_VERIFY_WITH_TIMEOUT(!geocodeServer.paths().isEmpty(), 1000);
    QVERIFY(geocodeServer.paths().first().startsWith(QStringLiteral("/ws/geocoder/v1/")));

    FakeHttpServer poiServer(R"({"status":0,"data":[{"id":"p1","title":"测试充电站","address":"测试路1号","location":{"lat":22.531,"lng":113.931},"_distance":321}]})");
    QVERIFY(poiServer.start());
    TencentMapService poiService(nullptr, poiServer.baseUrl(), 500);
    MapResult<QVector<MapPoi>> pois;
    poiService.searchNearbyChargingStations({22.53, 113.93}, 1000,
        [&pois](const auto &value) { pois = value; });
    QTRY_VERIFY_WITH_TIMEOUT(pois.ok, 1000);
    QCOMPARE(pois.value.size(), 1);
    QCOMPARE(pois.value.first().distanceMeters, qint64(321));
    QCOMPARE(pois.value.first().source, MapSource::Tencent);
    QVERIFY(poiServer.paths().first().contains(QStringLiteral("orderby=_distance")));

    MapResult<QVector<MapPoi>> invalidRadius;
    bool invalidDone = false;
    poiService.searchNearbyChargingStations({22.53, 113.93}, 5000,
        [&invalidRadius, &invalidDone](const auto &value) { invalidRadius = value; invalidDone = true; });
    QVERIFY(invalidDone);
    QVERIFY(!invalidRadius.ok);
    QCOMPARE(invalidRadius.error.category, MapErrorCategory::InvalidInput);

    FakeHttpServer missingFieldServer(R"({"status":0,"data":[{"id":"p1","title":"缺地址","location":{"lat":22.531,"lng":113.931}}]})");
    QVERIFY(missingFieldServer.start());
    TencentMapService missingFieldService(nullptr, missingFieldServer.baseUrl(), 500);
    MapResult<QVector<MapPoi>> missingField;
    bool missingDone = false;
    missingFieldService.searchNearbyChargingStations({22.53, 113.93}, 1000,
        [&missingField, &missingDone](const auto &value) { missingField = value; missingDone = true; });
    QTRY_VERIFY_WITH_TIMEOUT(missingDone, 1000);
    QVERIFY(!missingField.ok);
    QCOMPARE(missingField.error.category, MapErrorCategory::Parse);
  }

  void tencentRoutesUseCorrectUnitsEndpointsAndPolylineScale() {
    EnvironmentGuard guard;
    enableTestKey();
    FakeHttpServer server(R"({"status":0,"result":{"routes":[{"distance":1200,"duration":10,"polyline":[22.530000,113.930000,100,200]}]}})");
    QVERIFY(server.start());
    TencentMapService service(nullptr, server.baseUrl(), 500);
    MapResult<MapRoute> driving;
    MapResult<MapRoute> walking;
    service.queryRoute({22.530, 113.930}, {22.540, 113.940}, RouteMode::Driving,
                       [&driving](const auto &value) { driving = value; });
    QTRY_VERIFY_WITH_TIMEOUT(driving.ok, 1000);
    service.queryRoute({22.530, 113.930}, {22.540, 113.940}, RouteMode::Walking,
                       [&walking](const auto &value) { walking = value; });
    QTRY_VERIFY_WITH_TIMEOUT(walking.ok, 1000);
    QCOMPARE(driving.value.distanceMeters, qint64(1200));
    QCOMPARE(driving.value.durationSeconds, 600);
    QCOMPARE(driving.value.polyline.size(), 2);
    QCOMPARE(driving.value.polyline.at(1).latitude, 22.5301);
    QCOMPARE(driving.value.polyline.at(1).longitude, 113.9302);
    QCOMPARE(driving.value.source, MapSource::Tencent);
    QCOMPARE(server.paths().size(), 2);
    QVERIFY(server.paths().at(0).startsWith(QStringLiteral("/ws/direction/v1/driving/")));
    QVERIFY(server.paths().at(1).startsWith(QStringLiteral("/ws/direction/v1/walking/")));

    FakeHttpServer invalidRouteServer(R"({"status":0,"result":{"routes":[{"distance":-1,"duration":10}]}})");
    QVERIFY(invalidRouteServer.start());
    TencentMapService invalidRouteService(nullptr, invalidRouteServer.baseUrl(), 500);
    MapResult<MapRoute> invalidRoute;
    bool invalidDone = false;
    invalidRouteService.queryRoute({22.530, 113.930}, {22.540, 113.940}, RouteMode::Driving,
        [&invalidRoute, &invalidDone](const auto &value) { invalidRoute = value; invalidDone = true; });
    QTRY_VERIFY_WITH_TIMEOUT(invalidDone, 1000);
    QVERIFY(!invalidRoute.ok);
    QCOMPARE(invalidRoute.error.category, MapErrorCategory::Parse);
  }

  void failuresAreClassifiedAndRedacted() {
    EnvironmentGuard guard;
    enableTestKey();
    FakeHttpServer apiServer(R"({"status":120,"message":"key denied"})");
    QVERIFY(apiServer.start());
    TencentMapService apiService(nullptr, apiServer.baseUrl(), 500);
    MapResult<GeoCoordinate> apiResult;
    bool apiDone = false;
    apiService.geocode(QStringLiteral("测试地址"), [&apiResult, &apiDone](const auto &value) { apiResult = value; apiDone = true; });
    QTRY_VERIFY_WITH_TIMEOUT(apiDone, 1000);
    QVERIFY(!apiResult.ok);
    QCOMPARE(apiResult.error.category, MapErrorCategory::Api);
    QVERIFY(apiResult.error.userMessage.contains(QStringLiteral("权限")));
    QVERIFY(!apiResult.error.userMessage.contains(QStringLiteral("unit-test-key-123")));

    FakeHttpServer quotaServer(R"({"status":121,"message":"此key每日调用量已达到上限"})");
    QVERIFY(quotaServer.start());
    TencentMapService quotaService(nullptr, quotaServer.baseUrl(), 500);
    MapResult<GeoCoordinate> quotaResult;
    bool quotaDone = false;
    quotaService.geocode(QStringLiteral("测试地址"), [&quotaResult, &quotaDone](const auto &value) { quotaResult = value; quotaDone = true; });
    QTRY_VERIFY_WITH_TIMEOUT(quotaDone, 1000);
    QVERIFY(!quotaResult.ok);
    QVERIFY(quotaResult.error.userMessage.contains(QStringLiteral("额度")));
    QVERIFY(!quotaResult.error.userMessage.contains(QStringLiteral("权限不足")));
    QVERIFY(!quotaResult.error.retryable);

    FakeHttpServer httpServer(QByteArrayLiteral("{}"), 500);
    QVERIFY(httpServer.start());
    TencentMapService httpService(nullptr, httpServer.baseUrl(), 500);
    MapResult<GeoCoordinate> httpResult;
    bool httpDone = false;
    httpService.geocode(QStringLiteral("测试地址"), [&httpResult, &httpDone](const auto &value) { httpResult = value; httpDone = true; });
    QTRY_VERIFY_WITH_TIMEOUT(httpDone, 1000);
    QVERIFY(!httpResult.ok);
    QCOMPARE(httpResult.error.httpStatus, 500);

    FakeHttpServer malformedServer(QByteArrayLiteral("not-json"));
    QVERIFY(malformedServer.start());
    TencentMapService malformedService(nullptr, malformedServer.baseUrl(), 500);
    MapResult<GeoCoordinate> malformed;
    bool malformedDone = false;
    malformedService.geocode(QStringLiteral("测试地址"), [&malformed, &malformedDone](const auto &value) { malformed = value; malformedDone = true; });
    QTRY_VERIFY_WITH_TIMEOUT(malformedDone, 1000);
    QVERIFY(!malformed.ok);
    QCOMPARE(malformed.error.category, MapErrorCategory::Parse);

    FakeHttpServer timeoutServer({}, 200, false);
    QVERIFY(timeoutServer.start());
    TencentMapService timeoutService(nullptr, timeoutServer.baseUrl(), 20);
    MapResult<GeoCoordinate> timeout;
    bool timeoutDone = false;
    timeoutService.geocode(QStringLiteral("测试地址"), [&timeout, &timeoutDone](const auto &value) { timeout = value; timeoutDone = true; });
    QTRY_VERIFY_WITH_TIMEOUT(timeoutDone, 1000);
    QVERIFY(!timeout.ok);
    QCOMPARE(timeout.error.category, MapErrorCategory::Timeout);
    QVERIFY(timeout.error.retryable);
  }

  void resilientServiceFallsBackOnErrorsAndEmptyResults() {
    EnvironmentGuard guard;
    enableTestKey();
    FakeHttpServer errorServer(R"({"status":120,"message":"denied"})");
    QVERIFY(errorServer.start());
    TencentMapService primary(nullptr, errorServer.baseUrl(), 500);
    MockMapService fallback;
    ResilientMapService service(&primary, &fallback);

    MapResult<GeoCoordinate> geocode;
    service.geocode(QStringLiteral("深圳市南山区"), [&geocode](const auto &value) { geocode = value; });
    QTRY_VERIFY_WITH_TIMEOUT(geocode.ok, 1000);
    QVERIFY(!geocode.notice.isEmpty());

    MapResult<MapRoute> route;
    service.queryRoute({22.53, 113.93}, {22.54, 113.94}, RouteMode::Driving,
                       [&route](const auto &value) { route = value; });
    QTRY_VERIFY_WITH_TIMEOUT(route.ok, 1000);
    QCOMPARE(route.value.source, MapSource::Offline);
    QVERIFY(!route.notice.isEmpty());

    FakeHttpServer emptyServer(R"({"status":0,"data":[]})");
    QVERIFY(emptyServer.start());
    TencentMapService emptyPrimary(nullptr, emptyServer.baseUrl(), 500);
    ResilientMapService emptyService(&emptyPrimary, &fallback);
    MapResult<QVector<MapPoi>> pois;
    emptyService.searchNearbyChargingStations({22.53, 113.93}, 1000,
        [&pois](const auto &value) { pois = value; });
    QTRY_VERIFY_WITH_TIMEOUT(pois.ok, 1000);
    QVERIFY(!pois.value.isEmpty());
    QCOMPARE(pois.value.first().source, MapSource::Mock);
    QVERIFY(pois.notice.contains(QStringLiteral("空结果")));
  }

  void webEngineOfflineSmoke() {
    MapWebView view;
    QSignalSpy loadSpy(&view, &MapWebView::pageLoadFinished);
    view.setMarkers({{QStringLiteral("poi"), QStringLiteral("测试站"), QStringLiteral("测试地址"),
                      {22.53, 113.93}, 100, MapSource::Mock}});
    MapRoute route;
    route.source = MapSource::Offline;
    route.polyline = {{22.53, 113.93}, {22.54, 113.94}};
    view.setRoute(route);
    view.showOffline(QStringLiteral("Key 未配置"));
    QTRY_VERIFY_WITH_TIMEOUT(view.isOfflinePageLoaded(), 3000);
    QVERIFY(!view.isRealPageLoaded());
    QVERIFY(!loadSpy.isEmpty());
    view.loadTencent(QString());
    QVERIFY(!view.isRealPageLoaded());
  }

  void webEngineRealIntegration() {
    QSKIP("Tencent WebService and GL JS are server-owned after Protocol v1 map contract; use B server acceptance instead");
  }
};

QTEST_MAIN(MapServiceTest)
#include "map_service_test.moc"
