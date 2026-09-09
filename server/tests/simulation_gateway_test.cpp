#include "map/simulation_gateway.h"
#include "ev_database/database.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlQuery>

using ev::database::Database;
using ev::database::ErrorKind;

namespace {

bool require(bool condition, const QString &message)
{
    if (!condition) qCritical().noquote() << message;
    return condition;
}

QJsonObject proposal(qint64 stationId, qint64 version, qint64 pileId,
                     const QString &from, const QString &to, qint64 tickId)
{
    return QJsonObject{
        {QStringLiteral("simulator_id"), QStringLiteral("simulator-test")},
        {QStringLiteral("seed_id"), QStringLiteral("seed-test")},
        {QStringLiteral("tick_id"), tickId},
        {QStringLiteral("expected_versions"), QJsonObject{{QString::number(stationId), version}}},
        {QStringLiteral("changes"), QJsonArray{QJsonObject{
            {QStringLiteral("pile_id"), pileId},
            {QStringLiteral("from"), from},
            {QStringLiteral("to"), to},
            {QStringLiteral("reason"), QStringLiteral("test")}}}}
    };
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (!require(argc == 2, QStringLiteral("usage: simulation-gateway-test <schema.sql>")))
        return 2;

    QTemporaryDir directory;
    if (!require(directory.isValid(), QStringLiteral("temporary directory unavailable"))) return 2;
    qputenv("EV_PILE_SIMULATION_SEED", QByteArrayLiteral("seed-test"));
    Database database(directory.filePath(QStringLiteral("simulation.sqlite")), argv[1]);
    QString error;
    if (!require(database.open(&error), QStringLiteral("open failed: %1").arg(error))) return 1;
    QJsonObject testUser;
    ErrorKind kind = ErrorKind::None;
    if (!require(database.loginUser(QStringLiteral("13900000001"), &testUser, &error, &kind),
                 QStringLiteral("create test user failed: %1").arg(error))) return 1;

    const QVector<ev::database::MapPoi> pois{
        ev::database::MapPoi{QStringLiteral("sim-poi"), QStringLiteral("模拟站"),
                             QStringLiteral("测试地址"), 41.72, 123.43, 100}};
    QJsonObject imported;
    if (!require(database.importMapStations(
                     QStringLiteral("simulation-import"), 1,
                     QJsonObject{{QStringLiteral("operation"), QStringLiteral("map.station.search")}},
                     QJsonObject{{QStringLiteral("latitude"), 41.72},
                                 {QStringLiteral("longitude"), 123.43}},
                     pois, QStringLiteral("server_mock"), QJsonObject(), false, {},
                     &imported, &error, &kind),
                 QStringLiteral("import failed: %1").arg(error))) return 1;

    QJsonArray stations;
    if (!require(database.listStations(&stations, &error, &kind) && !stations.isEmpty(),
                 QStringLiteral("station lookup failed: %1").arg(error))) return 1;
    const qint64 stationId = stations.first().toObject().value(QStringLiteral("id")).toInteger();
    QJsonArray piles;
    if (!require(database.listPiles(stationId, &piles, &error, &kind) && !piles.isEmpty(),
                 QStringLiteral("pile lookup failed: %1").arg(error))) return 1;
    QJsonObject selected;
    for (const QJsonValue &value : piles) {
        if (value.toObject().value(QStringLiteral("simulated")).toBool()
            && value.toObject().value(QStringLiteral("status")).toString() == QStringLiteral("idle")) {
            selected = value.toObject();
            break;
        }
    }
    if (!require(!selected.isEmpty(), QStringLiteral("generated station has no idle simulated pile"))) return 1;
    const qint64 pileId = selected.value(QStringLiteral("id")).toInteger();

    ev::server::map::SimulationGateway gateway(&database);
    QJsonObject accepted;
    ev::protocol::ErrorCode code = ev::protocol::ErrorCode::InternalError;
    const QJsonObject firstProposal = proposal(stationId, 0, pileId,
                                               QStringLiteral("idle"), QStringLiteral("fault"), 1);
    if (!require(gateway.apply(firstProposal, &accepted, &code, &error)
                     && code == ev::protocol::ErrorCode::Ok
                     && accepted.value(QStringLiteral("accepted")).toBool(),
                 QStringLiteral("first tick failed: %1").arg(error))) return 1;

    QJsonObject replay;
    if (!require(gateway.apply(firstProposal, &replay, &code, &error)
                     && replay == accepted,
                 QStringLiteral("accepted tick replay failed: %1").arg(error))) return 1;
    QSqlDatabase auditDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                            QStringLiteral("simulation_audit"));
    auditDatabase.setDatabaseName(directory.filePath(QStringLiteral("simulation.sqlite")));
    if (!require(auditDatabase.open(), QStringLiteral("audit connection failed"))) return 1;
    {
        QSqlQuery auditQuery(auditDatabase);
        if (!require(auditQuery.exec(QStringLiteral(
                         "SELECT COUNT(*) FROM pile_status_events WHERE pile_id = %1")
                         .arg(pileId)) && auditQuery.next() && auditQuery.value(0).toInt() == 1,
                     QStringLiteral("accepted replay duplicated status event"))) return 1;
    }
    auditDatabase.close();
    auditDatabase = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("simulation_audit"));

    const QJsonObject staleProposal = proposal(stationId, 0, pileId,
                                               QStringLiteral("fault"), QStringLiteral("idle"), 2);
    QJsonObject staleResult;
    if (!require(!gateway.apply(staleProposal, &staleResult, &code, &error)
                     && code == ev::protocol::ErrorCode::Conflict
                     && !staleResult.value(QStringLiteral("accepted")).toBool(),
                 QStringLiteral("stale tick was not rejected: %1").arg(error))) return 1;
    const QJsonObject staleFirstResult = staleResult;
    QJsonObject staleReplay;
    if (!require(!gateway.apply(staleProposal, &staleReplay, &code, &error)
                     && code == ev::protocol::ErrorCode::Conflict
                     && staleReplay == staleFirstResult,
                 QStringLiteral("stale tick replay did not preserve conflict: %1").arg(error))) return 1;

    QJsonObject fingerprintConflict;
    if (!require(!gateway.apply(proposal(stationId, 0, pileId,
                                         QStringLiteral("fault"), QStringLiteral("offline"), 2),
                                &fingerprintConflict, &code, &error)
                     && code == ev::protocol::ErrorCode::Conflict,
                 QStringLiteral("tick fingerprint conflict was not rejected"))) return 1;

    QJsonObject staleNoOp{{QStringLiteral("simulator_id"), QStringLiteral("simulator-test")},
                          {QStringLiteral("seed_id"), QStringLiteral("seed-test")},
                          {QStringLiteral("tick_id"), 3},
                          {QStringLiteral("expected_versions"),
                           QJsonObject{{QString::number(stationId), 0}}},
                          {QStringLiteral("changes"), QJsonArray()}};
    QJsonObject noOpResult;
    if (!require(!gateway.apply(staleNoOp, &noOpResult, &code, &error)
                     && code == ev::protocol::ErrorCode::Conflict,
                 QStringLiteral("stale no-op tick bypassed version validation"))) return 1;
    staleNoOp.insert(QStringLiteral("tick_id"), 4);
    staleNoOp.insert(QStringLiteral("expected_versions"),
                     QJsonObject{{QString::number(stationId), 1}});
    if (!require(gateway.apply(staleNoOp, &noOpResult, &code, &error)
                     && noOpResult.value(QStringLiteral("accepted")).toBool(),
                 QStringLiteral("fresh no-op tick was rejected: %1").arg(error))) return 1;

    qInfo() << "simulation gateway tests passed";
    return 0;
}
