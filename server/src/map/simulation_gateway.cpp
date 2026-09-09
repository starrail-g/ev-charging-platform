#include "simulation_gateway.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace ev::server::map {
namespace {

bool positiveInteger(const QJsonValue &value, qint64 *result)
{
    if (!result || !value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
        || number > 9007199254740991.0) return false;
    *result = static_cast<qint64>(number);
    return true;
}

} // namespace

SimulationGateway::SimulationGateway(ev::database::Database *database)
    : database_(database)
{
}

bool SimulationGateway::apply(const QJsonObject &proposal, QJsonObject *response,
                              ev::protocol::ErrorCode *code, QString *error)
{
    if (code) *code = ev::protocol::ErrorCode::InternalError;
    if (!database_ || !response) {
        if (code) *code = ev::protocol::ErrorCode::InvalidRequest;
        if (error) *error = QStringLiteral("simulation gateway is unavailable");
        return false;
    }
    const QString simulatorId = proposal.value(QStringLiteral("simulator_id")).toString();
    const QString seedId = proposal.value(QStringLiteral("seed_id")).toString();
    qint64 tickId = 0;
    if (simulatorId.trimmed().isEmpty() || seedId.trimmed().isEmpty()
        || !positiveInteger(proposal.value(QStringLiteral("tick_id")), &tickId)
        || !proposal.value(QStringLiteral("expected_versions")).isObject()
        || !proposal.value(QStringLiteral("changes")).isArray()) {
        if (code) *code = ev::protocol::ErrorCode::InvalidRequest;
        if (error) *error = QStringLiteral("simulation proposal fields are invalid");
        return false;
    }
    QHash<qint64, qint64> expectedVersions;
    const QJsonObject versions = proposal.value(QStringLiteral("expected_versions")).toObject();
    for (auto iterator = versions.constBegin(); iterator != versions.constEnd(); ++iterator) {
        bool ok = false;
        const qint64 stationId = iterator.key().toLongLong(&ok);
        qint64 version = 0;
        if (!ok || stationId <= 0 || !positiveInteger(iterator.value(), &version)) {
            if (code) *code = ev::protocol::ErrorCode::InvalidRequest;
            if (error) *error = QStringLiteral("expected_versions is invalid");
            return false;
        }
        expectedVersions.insert(stationId, version);
    }
    QVector<ev::database::SimulationChange> changes;
    const QJsonArray changeArray = proposal.value(QStringLiteral("changes")).toArray();
    for (const QJsonValue &value : changeArray) {
        const QJsonObject object = value.toObject();
        qint64 pileId = 0;
        if (object.isEmpty() || !positiveInteger(object.value(QStringLiteral("pile_id")), &pileId)
            || !object.value(QStringLiteral("from")).isString()
            || !object.value(QStringLiteral("to")).isString()) {
            if (code) *code = ev::protocol::ErrorCode::InvalidRequest;
            if (error) *error = QStringLiteral("simulation change is invalid");
            return false;
        }
        changes.append(ev::database::SimulationChange{
            pileId,
            object.value(QStringLiteral("from")).toString(),
            object.value(QStringLiteral("to")).toString(),
            object.value(QStringLiteral("reason")).toString()});
    }
    ev::database::ErrorKind kind = ev::database::ErrorKind::None;
    if (!database_->applySimulationProposal(simulatorId, seedId, tickId,
                                             expectedVersions, changes, response,
                                             error, &kind)) {
        if (kind == ev::database::ErrorKind::InvalidArgument)
            *code = ev::protocol::ErrorCode::InvalidRequest;
        else if (kind == ev::database::ErrorKind::NotFound)
            *code = ev::protocol::ErrorCode::NotFound;
        else if (kind == ev::database::ErrorKind::Conflict)
            *code = ev::protocol::ErrorCode::Conflict;
        else
            *code = ev::protocol::ErrorCode::DatabaseError;
        return false;
    }
    if (code) *code = ev::protocol::ErrorCode::Ok;
    return true;
}

} // namespace ev::server::map
