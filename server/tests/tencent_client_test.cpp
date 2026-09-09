#include "map/tencent_client.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>

using ev::protocol::ErrorCode;
using ev::server::map::HttpTencentClient;
using ev::server::map::RouteResult;

namespace {

bool require(bool condition, const QString &message)
{
    if (!condition) qCritical().noquote() << message;
    return condition;
}

class FakeTencentServer final : public QTcpServer {
public:
    FakeTencentServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    const QByteArray request = socket->readAll();
                    const int lineEnd = request.indexOf("\r\n");
                    const QList<QByteArray> parts = request.left(lineEnd).split(' ');
                    if (parts.size() < 2) return;
                    const QUrl url = QUrl::fromEncoded(parts.at(1));
                    const QUrlQuery query(url);
                    const QString key = query.queryItemValue(QStringLiteral("key"));
                    if (key == QStringLiteral("fake-timeout")) return;

                    int status = 200;
                    QByteArray body;
                    if (key == QStringLiteral("fake-permission")) {
                        status = 403;
                        body = R"({"status":311,"message":"key format error"})";
                    } else if (key == QStringLiteral("fake-quota")) {
                        status = 429;
                        body = R"({"status":121,"message":"quota exceeded"})";
                    } else if (key == QStringLiteral("fake-invalid")) {
                        body = QByteArrayLiteral("not-json");
                    } else if (key == QStringLiteral("fake-large")) {
                        body = QByteArray(4 * 1024 * 1024 + 1, 'x');
                    } else if (url.path() == QStringLiteral("/ws/geocoder/v1/")) {
                        if (query.queryItemValue(QStringLiteral("address")) != QStringLiteral("沈阳市浑南区软件园")) {
                            body = R"({"status":110,"message":"no result"})";
                        } else {
                            body = R"({"status":0,"message":"Success","result":{"location":{"lat":41.7192,"lng":123.4315}}})";
                        }
                    } else if (url.path() == QStringLiteral("/ws/place/v1/search")) {
                        const int pageIndex = query.queryItemValue(QStringLiteral("page_index")).toInt();
                        const bool valid = query.queryItemValue(QStringLiteral("keyword")) == QStringLiteral("充电站")
                            && query.queryItemValue(QStringLiteral("page_size")) == QStringLiteral("20")
                            && pageIndex >= 1
                            && query.queryItemValue(QStringLiteral("orderby")) == QStringLiteral("_distance")
                            && query.queryItemValue(QStringLiteral("boundary")).startsWith(QStringLiteral("nearby("));
                        if (valid && key == QStringLiteral("fake-paged")) {
                            QJsonArray data;
                            const int begin = (pageIndex - 1) * 20;
                            const int end = pageIndex == 1 ? 20 : 21;
                            for (int index = begin; index < end; ++index) {
                                data.append(QJsonObject{
                                    {QStringLiteral("id"), QStringLiteral("paged-poi-%1").arg(index + 1)},
                                    {QStringLiteral("title"), QStringLiteral("分页测试充电站%1").arg(index + 1)},
                                    {QStringLiteral("address"), QStringLiteral("分页测试路%1号").arg(index + 1)},
                                    {QStringLiteral("location"), QJsonObject{{QStringLiteral("lat"), 41.7202 + index * 0.00001},
                                                                                 {QStringLiteral("lng"), 123.4335}}},
                                    {QStringLiteral("distance"), 200 + index}});
                            }
                            const QJsonObject response{{QStringLiteral("status"), 0},
                                                       {QStringLiteral("message"), QStringLiteral("Success")},
                                                       {QStringLiteral("count"), 21},
                                                       {QStringLiteral("data"), data}};
                            body = QJsonDocument(response).toJson(QJsonDocument::Compact);
                        } else {
                            body = valid
                                ? R"({"status":0,"message":"Success","data":[{"id":"real-poi-1","title":"腾讯测试充电站","address":"测试路1号","location":{"lat":41.7202,"lng":123.4335},"distance":200}]})"
                                : R"({"status":310,"message":"invalid parameters"})";
                        }
                    } else if (url.path().startsWith(QStringLiteral("/ws/direction/v1/"))) {
                        const bool valid = !query.queryItemValue(QStringLiteral("from")).isEmpty()
                            && !query.queryItemValue(QStringLiteral("to")).isEmpty();
                        if (!valid) {
                            body = R"({"status":310,"message":"invalid parameters"})";
                        } else if (key == QStringLiteral("fake-malformed")) {
                            body = R"({"status":0,"message":"Success","result":{"routes":[{"distance":1234,"duration":7,"polyline":[41.7192,123.4315,1]}]}})";
                        } else if (query.queryItemValue(QStringLiteral("from"))
                                   == query.queryItemValue(QStringLiteral("to"))) {
                            body = R"({"status":0,"message":"Success","result":{"routes":[{"distance":1,"duration":1,"polyline":[41.7192,123.4315]}]}})";
                        } else {
                            body = R"({"status":0,"message":"Success","result":{"routes":[{"distance":1234,"duration":7,"polyline":[41.7192,123.4315,1000,2000]}]}})";
                        }
                    } else {
                        status = 404;
                        body = R"({"status":404,"message":"not found"})";
                    }
                    const QByteArray reason = status == 200 ? QByteArrayLiteral("OK")
                        : status == 403 ? QByteArrayLiteral("Forbidden")
                        : status == 429 ? QByteArrayLiteral("Too Many Requests")
                        : QByteArrayLiteral("Not Found");
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Connection: close\r\n"
                                  "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    FakeTencentServer server;
    if (!require(server.listen(QHostAddress::LocalHost, 0), QStringLiteral("fake server listen failed"))) return 1;
    qputenv("TENCENT_MAP_BASE_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    qputenv("TENCENT_MAP_TIMEOUT_MS", QByteArrayLiteral("500"));
    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("fake-success"));

    HttpTencentClient client;
    ErrorCode code = ErrorCode::InternalError;
    QString error;
    QJsonObject origin;
    if (!require(client.geocode(QStringLiteral("沈阳市浑南区软件园"), &origin, &code, &error)
                 && origin.value(QStringLiteral("latitude")).toDouble() == 41.7192,
                 QStringLiteral("geocode failed: %1").arg(error))) return 1;

    QVector<ev::database::MapPoi> pois;
    if (!require(client.searchStations(origin, 1000, &pois, &code, &error)
                 && pois.size() == 1 && pois.first().providerPoiId == QStringLiteral("real-poi-1")
                 && pois.first().distanceMeters == 200,
                 QStringLiteral("POI search failed: %1").arg(error))) return 1;

    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("fake-paged"));
    if (!require(client.searchStations(origin, 1000, &pois, &code, &error)
                 && pois.size() == 21
                 && pois.last().providerPoiId == QStringLiteral("paged-poi-21"),
                 QStringLiteral("POI upstream pagination failed: %1").arg(error))) return 1;
    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("fake-success"));

    const QJsonObject destination{{QStringLiteral("latitude"), 41.7202},
                                  {QStringLiteral("longitude"), 123.4335}};
    RouteResult route;
    if (!require(client.planRoute(origin, destination, QStringLiteral("driving"), &route, &code, &error)
                 && route.distanceMeters == 1234 && route.durationSeconds == 420
                 && route.polyline.size() == 2
                 && qAbs(route.polyline.last().first - 41.7202) < 0.0000001
                 && qAbs(route.polyline.last().second - 123.4335) < 0.0000001,
                 QStringLiteral("driving route failed: %1").arg(error))) return 1;
    if (!require(client.planRoute(origin, destination, QStringLiteral("walking"), &route, &code, &error),
                 QStringLiteral("walking route failed: %1").arg(error))) return 1;
    if (!require(client.planRoute(origin, origin, QStringLiteral("driving"), &route, &code, &error)
                 && route.polyline.size() == 2
                 && route.polyline.first() == route.polyline.last(),
                 QStringLiteral("single-point route normalization failed: %1").arg(error))) return 1;

    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("fake-permission"));
    if (!require(!client.geocode(QStringLiteral("沈阳市浑南区软件园"), &origin, &code, &error)
                 && code == ErrorCode::MapPermissionDenied
                 && !error.contains(QStringLiteral("fake-permission")),
                 QStringLiteral("permission mapping failed"))) return 1;
    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("fake-quota"));
    if (!require(!client.geocode(QStringLiteral("沈阳市浑南区软件园"), &origin, &code, &error)
                 && code == ErrorCode::MapQuotaExceeded,
                 QStringLiteral("quota mapping failed"))) return 1;
    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("fake-invalid"));
    if (!require(!client.geocode(QStringLiteral("沈阳市浑南区软件园"), &origin, &code, &error)
                 && code == ErrorCode::MapResponseInvalid,
                 QStringLiteral("invalid JSON mapping failed"))) return 1;
    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("fake-malformed"));
    if (!require(!client.planRoute(origin, destination, QStringLiteral("driving"), &route, &code, &error)
                 && code == ErrorCode::MapResponseInvalid,
                 QStringLiteral("malformed polyline mapping failed"))) return 1;
    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("fake-large"));
    if (!require(!client.geocode(QStringLiteral("沈阳市浑南区软件园"), &origin, &code, &error)
                 && code == ErrorCode::MapResponseInvalid,
                 QStringLiteral("large response mapping failed"))) return 1;
    qputenv("TENCENT_MAP_KEY", QByteArrayLiteral("fake-timeout"));
    if (!require(!client.geocode(QStringLiteral("沈阳市浑南区软件园"), &origin, &code, &error)
                 && code == ErrorCode::MapUpstreamTimeout,
                 QStringLiteral("timeout mapping failed"))) return 1;

    qInfo() << "Tencent HTTP client tests passed";
    return 0;
}
