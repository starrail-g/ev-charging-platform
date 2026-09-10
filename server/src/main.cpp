#include "ev_protocol/frame_codec.h"
#include "ev_database/database.h"
#include "map/map_service.h"
#include "map/simulation_gateway.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QDateTime>
#include <QUuid>
#include <QTcpServer>
#include <QTcpSocket>

#include <cmath>
#include <limits>

using namespace ev::protocol;
using ev::database::ErrorKind;

class SimulationSessionRegistry final {
public:
    void attach(const QString &simulatorId, QTcpSocket *socket)
    {
        if (simulatorId.isEmpty() || !socket) return;
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it.value() == socket || it.key() == simulatorId) it = sessions_.erase(it);
            else ++it;
        }
        sessions_.insert(simulatorId, socket);
        socketIds_.insert(socket, simulatorId);
    }

    void detach(QTcpSocket *socket)
    {
        const QString id = socketIds_.take(socket);
        if (!id.isEmpty() && sessions_.value(id) == socket) sessions_.remove(id);
    }

    bool sendCommand(const QJsonObject &payload)
    {
        bool sent = false;
        const QByteArray frame = encodeFrame(Message{kProtocolVersion,
                                                     payload.value(QStringLiteral("command_id")).toString(),
                                                     QStringLiteral("simulator.command"), payload});
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (!it.value() || it.value()->state() == QAbstractSocket::UnconnectedState) {
                socketIds_.remove(it.value()); it = sessions_.erase(it); continue;
            }
            it.value()->write(frame); sent = true; ++it;
        }
        return sent;
    }

private:
    QHash<QString, QTcpSocket*> sessions_;
    QHash<QTcpSocket*, QString> socketIds_;
};

class AdminSessionStore final {
public:
    struct Session {
        qint64 administratorId = 0;
        QDateTime expiresAt;
    };

    QString issue(qint64 administratorId)
    {
        QByteArray bytes;
        bytes.reserve(32);
        for (int i = 0; i < 4; ++i) {
            const quint64 value = QRandomGenerator::system()->generate64();
            for (int shift = 0; shift < 64; shift += 8)
                bytes.append(static_cast<char>((value >> shift) & 0xff));
        }
        const QString token = QString::fromLatin1(bytes.toHex());
        sessions_.insert(token, Session{administratorId,
                                        QDateTime::currentDateTimeUtc().addSecs(kLifetimeSeconds)});
        return token;
    }

    bool resolve(const QString &token, qint64 *administratorId)
    {
        if (!administratorId || token.isEmpty()) return false;
        const auto iterator = sessions_.constFind(token);
        if (iterator == sessions_.constEnd()) return false;
        if (iterator->expiresAt <= QDateTime::currentDateTimeUtc()) {
            sessions_.remove(token);
            return false;
        }
        *administratorId = iterator->administratorId;
        return true;
    }

    static constexpr qint64 kLifetimeSeconds = 8 * 60 * 60;

private:
    QHash<QString, Session> sessions_;
};

class ClientConnection final : public QObject {
public:
    explicit ClientConnection(QTcpSocket *socket, QString databasePath, QString schemaPath,
                              QString seedPath, AdminSessionStore *adminSessions,
                              SimulationSessionRegistry *simulators,
                              QObject *parent = nullptr)
        : QObject(parent), socket_(socket),
          database_(std::move(databasePath), std::move(schemaPath), std::move(seedPath)),
          mapService_(&database_),
          adminSessions_(adminSessions), simulators_(simulators)
    {
        socket_->setParent(this);
        connect(socket_, &QTcpSocket::readyRead, this, [this] { readAvailable(); });
        connect(socket_, &QTcpSocket::disconnected, this, [this] {
            if (simulators_) simulators_->detach(socket_);
            deleteLater();
        });
        connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            qWarning() << "socket error" << socket_->errorString();
        });
    }

private:
    void readAvailable()
    {
        QString error;
        ErrorCode errorCode = ErrorCode::Ok;
        const QList<Message> requests = decoder_.feed(socket_->readAll(), &error, &errorCode);
        for (const Message &request : requests) handle(request);
        if (!error.isEmpty()) {
            sendError(QString(), errorCode, error);
            socket_->disconnectFromHost();
        }
    }

    void handle(const Message &request)
    {
        qInfo() << "request" << request.id << request.type;
        if (request.type == QStringLiteral("simulator.register")) {
            handleSimulatorRegister(request);
        } else if (request.type == QStringLiteral("simulator.tick")) {
            handleSimulatorTick(request);
        } else if (request.type == QStringLiteral("simulator.pile.report")) {
            handleSimulatorReport(request);
        } else if (request.type == QStringLiteral("simulator.command.result")) {
            handleSimulatorCommandResult(request);
        } else if (request.type == QStringLiteral("simulator.snapshot.get")) {
            handleSimulatorSnapshot(request);
        } else if (request.type == QStringLiteral("health")) {
            sendResponse(request, QStringLiteral("health.result"),
                         QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")},
                                     {QStringLiteral("service"), QStringLiteral("ev-server")}});
        } else if (request.type == QStringLiteral("echo")) {
            sendResponse(request, QStringLiteral("echo.result"), request.payload);
        } else if (request.type == QStringLiteral("user.login")) {
            handleUserLogin(request);
        } else if (request.type == QStringLiteral("user.profile.get")) {
            handleUserProfileGet(request);
        } else if (request.type == QStringLiteral("user.profile.update")) {
            handleUserProfileUpdate(request);
        } else if (request.type == QStringLiteral("wallet.recharge")) {
            handleWalletRecharge(request);
        } else if (request.type == QStringLiteral("station.list")) {
            handleStationList(request);
        } else if (request.type == QStringLiteral("pile.list")) {
            handlePileList(request);
        } else if (request.type == QStringLiteral("map.station.search")) {
            handleMapStationSearch(request);
        } else if (request.type == QStringLiteral("map.route.plan")) {
            handleMapRoutePlan(request);
        } else if (request.type == QStringLiteral("order.active.get")) {
            handleActiveOrder(request);
        } else if (request.type == QStringLiteral("order.history.list")) {
            handleOrderHistory(request);
        } else if (request.type == QStringLiteral("admin.login")) {
            handleAdministratorLogin(request);
        } else if (request.type.startsWith(QStringLiteral("admin."))
                   && !authorizeAdministrator(request)) {
            return;
        } else if (request.type == QStringLiteral("admin.statistics.get")) {
            handleAdministratorStatistics(request);
        } else if (request.type == QStringLiteral("admin.station.list")) {
            handleAdministratorStationList(request);
        } else if (request.type == QStringLiteral("admin.pile.list")) {
            handleAdministratorPileList(request);
        } else if (request.type == QStringLiteral("admin.station.create")) {
            handleAdministratorStationCreate(request);
        } else if (request.type == QStringLiteral("admin.pile.restart")) {
            handleAdministratorPileRestart(request);
        } else if (request.type == QStringLiteral("admin.user.list")) {
            handleAdministratorUserList(request);
        } else if (request.type == QStringLiteral("admin.user.status.set")) {
            handleAdministratorUserStatus(request);
        } else if (request.type == QStringLiteral("admin.map.audit.list")) {
            handleMapAuditList(request);
        } else if (request.type == QStringLiteral("reservation.create")) {
            handleReservationCreate(request);
        } else if (request.type == QStringLiteral("reservation.confirm")) {
            handleReservationConfirm(request);
        } else if (request.type == QStringLiteral("reservation.cancel")) {
            handleReservationCancel(request);
        } else if (request.type == QStringLiteral("charging.start")) {
            handleChargingStart(request);
        } else if (request.type == QStringLiteral("charging.stop")) {
            handleChargingStop(request);
        } else if (request.type == QStringLiteral("charging.settle")) {
            handleChargingSettle(request);
        } else {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("unsupported request type: %1").arg(request.type));
        }
    }

    bool authorizeAdministrator(const Message &request)
    {
        const QJsonValue tokenValue = request.payload.value(QStringLiteral("token"));
        qint64 administratorId = 0;
        if (!tokenValue.isString() || !adminSessions_
            || !adminSessions_->resolve(tokenValue.toString(), &administratorId)) {
            sendError(request.id, ErrorCode::Unauthorized,
                      QStringLiteral("administrator token is missing or expired"));
            return false;
        }
        QJsonObject administrator;
        QString error;
        ErrorKind kind = ErrorKind::None;
        if (!database_.getAdministrator(administratorId, &administrator, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("administrator authentication failed"));
            return false;
        }
        if (request.payload.contains(QStringLiteral("administrator_id"))) {
            qint64 suppliedId = 0;
            if (!positiveId(request.payload.value(QStringLiteral("administrator_id")), &suppliedId)
                || suppliedId != administratorId) {
                sendError(request.id, ErrorCode::Unauthorized,
                          QStringLiteral("administrator token does not match administrator_id"));
                return false;
            }
        }
        return true;
    }

    void handleUserLogin(const Message &request)
    {
        const QJsonValue phoneValue = request.payload.value(QStringLiteral("phone"));
        const QString phone = phoneValue.toString();
        static const QRegularExpression phonePattern(QStringLiteral("^[0-9]{11}$"));
        if (!phoneValue.isString() || !phonePattern.match(phone).hasMatch()) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("phone must contain exactly 11 ASCII digits"));
            return;
        }

        QJsonObject user;
        QString error;
        ErrorKind kind = ErrorKind::None;
        if (!database_.loginUser(phone, &user, &error, &kind)) {
            sendDatabaseError(request.id, kind, error, QStringLiteral("user login database failure"));
            return;
        }
        sendResponse(request, QStringLiteral("user.login.result"),
                     QJsonObject{{QStringLiteral("user"), user}});
    }

    void handleSimulatorRegister(const Message &request)
    {
        const QString simulatorId = request.payload.value(QStringLiteral("simulator_id")).toString().trimmed();
        if (simulatorId.isEmpty() || simulatorId.size() > 128) {
            sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("simulator_id is required"));
            return;
        }
        if (simulators_) simulators_->attach(simulatorId, socket_);
        QJsonObject snapshot; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.getSimulationSnapshot(&snapshot, &error, &kind)) {
            sendDatabaseError(request.id, kind, error, QStringLiteral("read simulator snapshot failed"));
            return;
        }
        snapshot.insert(QStringLiteral("simulator_id"), simulatorId);
        sendResponse(request, QStringLiteral("simulator.register.result"), snapshot);
    }

    void handleSimulatorSnapshot(const Message &request)
    {
        QJsonObject snapshot; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.getSimulationSnapshot(&snapshot, &error, &kind)) {
            sendDatabaseError(request.id, kind, error, QStringLiteral("read simulator snapshot failed"));
            return;
        }
        sendResponse(request, QStringLiteral("simulator.snapshot.result"), snapshot);
    }

    void handleSimulatorTick(const Message &request)
    {
        QJsonObject result; ErrorCode code = ErrorCode::InternalError; QString error;
        if (!simulationGateway_.apply(request.payload, &result, &code, &error)) {
            sendError(request.id, code, error);
            return;
        }
        sendResponse(request, QStringLiteral("simulator.tick.result"), result);
    }

    void handleSimulatorReport(const Message &request)
    {
        const QJsonObject payload = request.payload;
        qint64 pileId = 0;
        qint64 stationId = 0;
        qint64 expectedVersion = 0;
        qint64 tickId = QDateTime::currentMSecsSinceEpoch();
        if (!positiveId(payload.value(QStringLiteral("pile_id")), &pileId)
            || !payload.value(QStringLiteral("from")).isString()
            || !payload.value(QStringLiteral("to")).isString()) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("pile report requires pile_id, from and to"));
            return;
        }
        if (payload.value(QStringLiteral("tick_id")).isDouble())
            tickId = payload.value(QStringLiteral("tick_id")).toInteger();
        QJsonObject snapshot; QString snapshotError; ErrorKind snapshotKind = ErrorKind::None;
        if (!database_.getSimulationSnapshot(&snapshot, &snapshotError, &snapshotKind)) {
            sendDatabaseError(request.id, snapshotKind, snapshotError,
                              QStringLiteral("read simulator snapshot failed"));
            return;
        }
        for (const QJsonValue &stationValue : snapshot.value(QStringLiteral("stations")).toArray()) {
            const QJsonObject station = stationValue.toObject();
            for (const QJsonValue &pileValue : station.value(QStringLiteral("piles")).toArray()) {
                if (pileValue.toObject().value(QStringLiteral("pile_id")).toInteger() == pileId) {
                    stationId = station.value(QStringLiteral("station_id")).toInteger();
                    expectedVersion = station.value(QStringLiteral("snapshot_version")).toInteger();
                    break;
                }
            }
            if (stationId > 0) break;
        }
        if (stationId <= 0) {
            sendError(request.id, ErrorCode::NotFound, QStringLiteral("pile not found in simulator snapshot"));
            return;
        }
        QJsonObject proposal{{QStringLiteral("simulator_id"), payload.value(QStringLiteral("simulator_id"))},
                             {QStringLiteral("seed_id"), payload.value(QStringLiteral("seed_id"))},
                             {QStringLiteral("tick_id"), tickId},
                             {QStringLiteral("expected_versions"),
                              QJsonObject{{QString::number(stationId), expectedVersion}}},
                             {QStringLiteral("changes"), QJsonArray{QJsonObject{
                                  {QStringLiteral("pile_id"), pileId},
                                  {QStringLiteral("from"), payload.value(QStringLiteral("from"))},
                                  {QStringLiteral("to"), payload.value(QStringLiteral("to"))},
                                  {QStringLiteral("reason"), payload.value(QStringLiteral("reason"))}}}}};
        QJsonObject result; ErrorCode code = ErrorCode::InternalError; QString error;
        if (!simulationGateway_.apply(proposal, &result, &code, &error)) {
            sendError(request.id, code, error);
            return;
        }
        sendResponse(request, QStringLiteral("simulator.pile.report.result"), result);
    }

    void handleSimulatorCommandResult(const Message &request)
    {
        // Command ACKs are intentionally lightweight for the demo path. The
        // server has already committed the user/admin business transaction;
        // retain the ACK as an observable response for the simulator client.
        sendResponse(request, QStringLiteral("simulator.command.result.ack"),
                     QJsonObject{{QStringLiteral("accepted"), true},
                                 {QStringLiteral("command_id"), request.payload.value(QStringLiteral("command_id"))}});
    }

    void notifySimulator(const QString &command, qint64 pileId, qint64 orderId = 0)
    {
        if (!simulators_ || pileId <= 0) return;
        QJsonObject payload{{QStringLiteral("command_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                            {QStringLiteral("command"), command},
                            {QStringLiteral("pile_id"), pileId}};
        if (orderId > 0) payload.insert(QStringLiteral("order_id"), orderId);
        simulators_->sendCommand(payload);
    }

    static bool positiveId(const QJsonValue &value, qint64 *id)
    {
        if (!id || !value.isDouble()) return false;
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < 1.0 || std::floor(number) != number
            || number > 9007199254740991.0) return false;
        *id = static_cast<qint64>(number);
        return true;
    }

    static bool nonNegativeId(const QJsonValue &value, qint64 *id)
    {
        if (!id || !value.isDouble()) return false;
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
            || number > 9007199254740991.0) return false;
        *id = static_cast<qint64>(number);
        return true;
    }

    static bool hasOnlyFields(const QJsonObject &payload, const QStringList &allowed)
    {
        for (auto iterator = payload.constBegin(); iterator != payload.constEnd(); ++iterator) {
            if (!allowed.contains(iterator.key())) return false;
        }
        return true;
    }

    static bool requestIds(const QJsonObject &payload, qint64 *userId, qint64 *resourceId,
                           const QString &resourceName)
    {
        return positiveId(payload.value(QStringLiteral("user_id")), userId)
            && positiveId(payload.value(resourceName), resourceId);
    }

    void sendDatabaseError(const QString &requestId, ErrorKind kind, const QString &message,
                           const QString &fallback)
    {
        ErrorCode code = ErrorCode::DatabaseError;
        if (kind == ErrorKind::InvalidArgument) code = ErrorCode::InvalidRequest;
        else if (kind == ErrorKind::Unauthorized) code = ErrorCode::Unauthorized;
        else if (kind == ErrorKind::AccountFrozen) code = ErrorCode::AccountFrozen;
        else if (kind == ErrorKind::NotFound) code = ErrorCode::NotFound;
        else if (kind == ErrorKind::Conflict) code = ErrorCode::Conflict;
        else if (kind == ErrorKind::InsufficientBalance) code = ErrorCode::InsufficientBalance;
        const QString publicMessage = kind == ErrorKind::Database
            ? fallback : (message.isEmpty() ? fallback : message);
        sendError(requestId, code, publicMessage);
    }

    void handleUserProfileGet(const Message &request)
    {
        qint64 userId = 0;
        if (!hasOnlyFields(request.payload, {QStringLiteral("user_id")})
            || !positiveId(request.payload.value(QStringLiteral("user_id")), &userId)) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("user_id must be a positive integer"));
            return;
        }
        QJsonObject user;
        QString error;
        ErrorKind kind = ErrorKind::None;
        if (!database_.getUserProfile(userId, &user, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("get user profile failed"));
            return;
        }
        sendResponse(request, QStringLiteral("user.profile.get.result"),
                     QJsonObject{{QStringLiteral("user"), user}});
    }

    void handleUserProfileUpdate(const Message &request)
    {
        qint64 userId = 0;
        const bool hasNickname = request.payload.contains(QStringLiteral("nickname"));
        const bool hasAvatar = request.payload.contains(QStringLiteral("avatar_path"));
        const QJsonValue nickname = request.payload.value(QStringLiteral("nickname"));
        const QJsonValue avatar = request.payload.value(QStringLiteral("avatar_path"));
        if (!hasOnlyFields(request.payload,
                           {QStringLiteral("user_id"), QStringLiteral("nickname"),
                            QStringLiteral("avatar_path")})
            || !positiveId(request.payload.value(QStringLiteral("user_id")), &userId)
            || (!hasNickname && !hasAvatar)
            || (hasNickname && (!nickname.isString() || nickname.toString().trimmed().isEmpty()))
            || (hasAvatar && !avatar.isString() && !avatar.isNull())) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("provide a valid nickname or avatar_path"));
            return;
        }
        QJsonObject changes;
        if (hasNickname) changes.insert(QStringLiteral("nickname"), nickname.toString().trimmed());
        if (hasAvatar) changes.insert(QStringLiteral("avatar_path"), avatar);
        QJsonObject user;
        QString error;
        ErrorKind kind = ErrorKind::None;
        if (!database_.updateUserProfile(request.id, userId, changes, &user, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("update user profile failed"));
            return;
        }
        sendResponse(request, QStringLiteral("user.profile.update.result"),
                     QJsonObject{{QStringLiteral("user"), user}});
    }

    void handleWalletRecharge(const Message &request)
    {
        qint64 userId = 0;
        qint64 amountCents = 0;
        if (!hasOnlyFields(request.payload,
                           {QStringLiteral("user_id"), QStringLiteral("amount_cents")})
            || !positiveId(request.payload.value(QStringLiteral("user_id")), &userId)
            || !positiveId(request.payload.value(QStringLiteral("amount_cents")), &amountCents)) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("user_id and amount_cents must be positive integers"));
            return;
        }
        qint64 balanceCents = 0;
        qint64 transactionId = 0;
        QString error;
        ErrorKind kind = ErrorKind::None;
        if (!database_.rechargeWallet(request.id, userId, amountCents,
                                      &balanceCents, &transactionId, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("wallet recharge failed"));
            return;
        }
        sendResponse(request, QStringLiteral("wallet.recharge.result"),
                     QJsonObject{{QStringLiteral("balance_cents"), balanceCents},
                                 {QStringLiteral("transaction_id"), transactionId}});
    }

    void handleStationList(const Message &request)
    {
        QJsonArray stations;
        QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.listStations(&stations, &error, &kind)) {
            sendDatabaseError(request.id, kind, error, QStringLiteral("list stations failed")); return;
        }
        sendResponse(request, QStringLiteral("station.list.result"), QJsonObject{{QStringLiteral("stations"), stations}});
    }

    void handlePileList(const Message &request)
    {
        qint64 stationId = 0;
        if (!positiveId(request.payload.value(QStringLiteral("station_id")), &stationId)) {
            sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("station_id must be a positive integer")); return;
        }
        QJsonArray piles; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.listPiles(stationId, &piles, &error, &kind)) {
            sendDatabaseError(request.id, kind, error, QStringLiteral("list piles failed")); return;
        }
        sendResponse(request, QStringLiteral("pile.list.result"), QJsonObject{{QStringLiteral("piles"), piles}});
    }

    void handleMapStationSearch(const Message &request)
    {
        qint64 userId = 0;
        if (!positiveId(request.payload.value(QStringLiteral("user_id")), &userId)) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("user_id must be a positive integer"));
            return;
        }
        QJsonObject user;
        QString userError;
        ErrorKind userKind = ErrorKind::None;
        if (!database_.getUserProfile(userId, &user, &userError, &userKind)) {
            sendDatabaseError(request.id, userKind, userError,
                              QStringLiteral("map search user lookup failed"));
            return;
        }
        QJsonObject result;
        ev::server::map::MapFailure failure;
        if (!mapService_.stationSearch(request.id, userId, request.payload, &result, &failure)) {
            sendError(request.id, failure.code, failure.message);
            return;
        }
        sendResponse(request, QStringLiteral("map.station.search.result"), result);
    }

    void handleMapRoutePlan(const Message &request)
    {
        qint64 userId = 0;
        if (!positiveId(request.payload.value(QStringLiteral("user_id")), &userId)) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("user_id must be a positive integer"));
            return;
        }
        QJsonObject user;
        QString userError;
        ErrorKind userKind = ErrorKind::None;
        if (!database_.getUserProfile(userId, &user, &userError, &userKind)) {
            sendDatabaseError(request.id, userKind, userError,
                              QStringLiteral("map route user lookup failed"));
            return;
        }
        QJsonObject result;
        ev::server::map::MapFailure failure;
        if (!mapService_.routePlan(request.id, userId, request.payload, &result, &failure)) {
            sendError(request.id, failure.code, failure.message);
            return;
        }
        sendResponse(request, QStringLiteral("map.route.plan.result"), result);
    }

    void handleMapAuditList(const Message &request)
    {
        const QJsonValue operation = request.payload.value(QStringLiteral("operation"));
        const QJsonValue resultStatus = request.payload.value(QStringLiteral("result_status"));
        const QJsonValue pageToken = request.payload.value(QStringLiteral("page_token"));
        qint64 limit = 50;
        if (!hasOnlyFields(request.payload, {QStringLiteral("token"), QStringLiteral("operation"),
                                             QStringLiteral("result_status"), QStringLiteral("page_token"),
                                             QStringLiteral("limit")})
            || (!operation.isUndefined() && !operation.isString())
            || (!resultStatus.isUndefined() && !resultStatus.isString())
            || (!pageToken.isUndefined() && !pageToken.isNull() && !pageToken.isString())
            || (request.payload.contains(QStringLiteral("limit"))
                && (!positiveId(request.payload.value(QStringLiteral("limit")), &limit) || limit > 100))) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("map audit filters are invalid"));
            return;
        }
        QJsonObject result;
        ev::server::map::MapFailure failure;
        if (!mapService_.listAudit(operation.toString(), resultStatus.toString(),
                                   pageToken.toString(), limit, &result, &failure)) {
            sendError(request.id, failure.code, failure.message);
            return;
        }
        sendResponse(request, QStringLiteral("admin.map.audit.list.result"), result);
    }

    void handleActiveOrder(const Message &request)
    {
        qint64 userId = 0;
        if (!positiveId(request.payload.value(QStringLiteral("user_id")), &userId)) {
            sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("user_id must be a positive integer")); return;
        }
        QJsonObject order; bool found = false; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.getActiveOrder(userId, &order, &found, &error, &kind)) {
            sendDatabaseError(request.id, kind, error, QStringLiteral("get active order failed")); return;
        }
        sendResponse(request, QStringLiteral("order.active.get.result"), QJsonObject{{QStringLiteral("order"), found ? QJsonValue(order) : QJsonValue(QJsonValue::Null)}});
    }

    void handleOrderHistory(const Message &request)
    {
        qint64 userId = 0;
        if (!positiveId(request.payload.value(QStringLiteral("user_id")), &userId)) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("user_id must be a positive integer"));
            return;
        }
        QJsonArray orders;
        QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.listOrderHistory(userId, &orders, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("list order history failed"));
            return;
        }
        sendResponse(request, QStringLiteral("order.history.list.result"),
                     QJsonObject{{QStringLiteral("orders"), orders}});
    }

    void handleAdministratorLogin(const Message &request)
    {
        const QJsonValue username = request.payload.value(QStringLiteral("username"));
        const QJsonValue password = request.payload.value(QStringLiteral("password"));
        if (!hasOnlyFields(request.payload, {QStringLiteral("username"), QStringLiteral("password")})
            || !username.isString() || !password.isString()
            || username.toString().trimmed().isEmpty() || password.toString().isEmpty()) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("username and password are required"));
            return;
        }
        QJsonObject administrator;
        QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.loginAdministrator(username.toString(), password.toString(),
                                           &administrator, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("administrator login failed"));
            return;
        }
        const QString token = adminSessions_ ? adminSessions_->issue(
            administrator.value(QStringLiteral("id")).toInteger()) : QString();
        if (token.isEmpty()) {
            sendError(request.id, ErrorCode::InternalError,
                      QStringLiteral("administrator session could not be created"));
            return;
        }
        sendResponse(request, QStringLiteral("admin.login.result"),
                     QJsonObject{{QStringLiteral("admin"), administrator},
                                 {QStringLiteral("token"), token},
                                 {QStringLiteral("expires_in_seconds"),
                                  AdminSessionStore::kLifetimeSeconds}});
    }

    void handleAdministratorStatistics(const Message &request)
    {
        const QJsonValue range = request.payload.value(QStringLiteral("range"));
        if (!hasOnlyFields(request.payload, {QStringLiteral("range"), QStringLiteral("token")})
            || !range.isString()
            || (range.toString() != QStringLiteral("7d")
                && range.toString() != QStringLiteral("30d"))) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("range must be 7d or 30d"));
            return;
        }
        QJsonObject statistics; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.getStatistics(range.toString(), &statistics, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("get administrator statistics failed"));
            return;
        }
        sendResponse(request, QStringLiteral("admin.statistics.get.result"),
                     QJsonObject{{QStringLiteral("statistics"), statistics}});
    }

    void handleAdministratorStationList(const Message &request)
    {
        const QJsonValue queryValue = request.payload.value(QStringLiteral("query"));
        if (!hasOnlyFields(request.payload, {QStringLiteral("query"), QStringLiteral("token")})
            || (!queryValue.isUndefined() && !queryValue.isString())) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("query must be a string"));
            return;
        }
        QJsonArray stations; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.listAdminStations(queryValue.toString(), &stations, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("list administrator stations failed"));
            return;
        }
        sendResponse(request, QStringLiteral("admin.station.list.result"),
                     QJsonObject{{QStringLiteral("stations"), stations}});
    }

    void handleAdministratorPileList(const Message &request)
    {
        constexpr qint64 kDefaultPageSize = 100;
        constexpr qint64 kMaximumPageSize = 250;
        qint64 afterId = 0;
        qint64 limit = kDefaultPageSize;
        const QJsonValue afterIdValue = request.payload.value(QStringLiteral("after_id"));
        const QJsonValue limitValue = request.payload.value(QStringLiteral("limit"));
        if (!hasOnlyFields(request.payload,
                           {QStringLiteral("token"), QStringLiteral("after_id"),
                            QStringLiteral("limit")})
            || (!afterIdValue.isUndefined() && !nonNegativeId(afterIdValue, &afterId))
            || (!limitValue.isUndefined()
                && (!positiveId(limitValue, &limit) || limit > kMaximumPageSize))) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("admin.pile.list requires optional non-negative after_id and limit 1..250"));
            return;
        }
        QJsonArray databasePage;
        bool hasMore = false;
        QString error;
        ErrorKind kind = ErrorKind::None;
        if (!database_.listAdminPiles(afterId, limit, &databasePage, &hasMore,
                                      &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("list administrator piles failed"));
            return;
        }

        // Do not depend solely on row count: legacy SQLite rows can contain a
        // legal pile_code large enough to exceed the protocol's 1 MiB frame.
        // Use the exact compact JSON envelope size and leave a cursor for the
        // first row that does not fit in this response.
        QJsonArray piles;
        bool truncatedForFrame = false;
        for (const QJsonValue &value : databasePage) {
            const QJsonObject pile = value.toObject();
            const qint64 lastId = pile.value(QStringLiteral("id")).toInteger();
            QJsonArray candidate = piles;
            candidate.append(pile);
            const bool candidateHasMore = hasMore || candidate.size() < databasePage.size();
            QJsonObject candidatePayload{{QStringLiteral("piles"), candidate}};
            if (candidateHasMore)
                candidatePayload.insert(QStringLiteral("next_after_id"), lastId);
            if (!responseFitsFrameLimit(request.id, QStringLiteral("admin.pile.list.result"),
                                        candidatePayload)) {
                truncatedForFrame = true;
                break;
            }
            piles = candidate;
        }
        if (piles.isEmpty() && !databasePage.isEmpty()) {
            sendError(request.id, ErrorCode::InternalError,
                      QStringLiteral("a charging pile cannot fit in one protocol frame"));
            return;
        }
        const bool pageHasMore = hasMore || truncatedForFrame;
        QJsonObject payload{{QStringLiteral("piles"), piles}};
        if (pageHasMore)
            payload.insert(QStringLiteral("next_after_id"),
                           piles.last().toObject().value(QStringLiteral("id")).toInteger());
        sendResponse(request, QStringLiteral("admin.pile.list.result"), payload);
    }

    void handleAdministratorStationCreate(const Message &request)
    {
        qint64 administratorId = 0;
        qint64 pileCount = 0;
        const QJsonValue name = request.payload.value(QStringLiteral("name"));
        const QJsonValue address = request.payload.value(QStringLiteral("address"));
        const QJsonValue latitude = request.payload.value(QStringLiteral("latitude"));
        const QJsonValue longitude = request.payload.value(QStringLiteral("longitude"));
        if (!hasOnlyFields(request.payload, {QStringLiteral("administrator_id"), QStringLiteral("token"), QStringLiteral("name"),
                                             QStringLiteral("address"), QStringLiteral("latitude"),
                                             QStringLiteral("longitude"), QStringLiteral("pile_count")})
            || !positiveId(request.payload.value(QStringLiteral("administrator_id")), &administratorId)
            || !name.isString() || !address.isString() || name.toString().trimmed().isEmpty()
            || address.toString().trimmed().isEmpty() || !latitude.isDouble() || !longitude.isDouble()
            || !positiveId(request.payload.value(QStringLiteral("pile_count")), &pileCount)) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("administrator_id, station fields and pile_count are invalid"));
            return;
        }
        QJsonObject station; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.createStation(request.id, administratorId, name.toString(), address.toString(),
                                     latitude.toDouble(), longitude.toDouble(), pileCount,
                                     &station, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("create administrator station failed"));
            return;
        }
        sendResponse(request, QStringLiteral("admin.station.create.result"),
                     QJsonObject{{QStringLiteral("station"), station}});
    }

    void handleAdministratorPileRestart(const Message &request)
    {
        qint64 administratorId = 0;
        qint64 pileId = 0;
        if (!hasOnlyFields(request.payload, {QStringLiteral("administrator_id"), QStringLiteral("token"), QStringLiteral("pile_id")})
            || !positiveId(request.payload.value(QStringLiteral("administrator_id")), &administratorId)
            || !positiveId(request.payload.value(QStringLiteral("pile_id")), &pileId)) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("administrator_id and pile_id must be positive integers"));
            return;
        }
        QJsonObject pile; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.restartPile(request.id, administratorId, pileId, &pile, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("restart charging pile failed"));
            return;
        }
        notifySimulator(QStringLiteral("restart"), pileId);
        sendResponse(request, QStringLiteral("admin.pile.restart.result"),
                     QJsonObject{{QStringLiteral("pile"), pile}});
    }

    void handleAdministratorUserList(const Message &request)
    {
        const QJsonValue phoneQuery = request.payload.value(QStringLiteral("phone_query"));
        if (!hasOnlyFields(request.payload, {QStringLiteral("phone_query"), QStringLiteral("token")})
            || (!phoneQuery.isUndefined() && !phoneQuery.isString())) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("phone_query must be a string"));
            return;
        }
        QJsonArray users; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.listAdminUsers(phoneQuery.toString(), &users, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("list administrator users failed"));
            return;
        }
        sendResponse(request, QStringLiteral("admin.user.list.result"),
                     QJsonObject{{QStringLiteral("users"), users}});
    }

    void handleAdministratorUserStatus(const Message &request)
    {
        qint64 administratorId = 0;
        qint64 userId = 0;
        const QJsonValue status = request.payload.value(QStringLiteral("status"));
        if (!hasOnlyFields(request.payload, {QStringLiteral("administrator_id"), QStringLiteral("token"), QStringLiteral("user_id"),
                                             QStringLiteral("status")})
            || !positiveId(request.payload.value(QStringLiteral("administrator_id")), &administratorId)
            || !positiveId(request.payload.value(QStringLiteral("user_id")), &userId)
            || !status.isString()
            || (status.toString() != QStringLiteral("active")
                && status.toString() != QStringLiteral("frozen"))) {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("administrator_id, user_id and status are invalid"));
            return;
        }
        QJsonObject user; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.setUserStatus(request.id, administratorId, userId, status.toString(),
                                     &user, &error, &kind)) {
            sendDatabaseError(request.id, kind, error,
                              QStringLiteral("set user status failed"));
            return;
        }
        sendResponse(request, QStringLiteral("admin.user.status.set.result"),
                     QJsonObject{{QStringLiteral("user"), user}});
    }

    void handleReservationCreate(const Message &request)
    {
        qint64 userId = 0, pileId = 0;
        if (!requestIds(request.payload, &userId, &pileId, QStringLiteral("pile_id"))) {
            sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("user_id and pile_id must be positive integers")); return;
        }
        QJsonObject order, pile; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.createReservation(request.id, userId, pileId, &order, &pile, &error, &kind)) {
            sendDatabaseError(request.id, kind, error, QStringLiteral("create reservation failed")); return;
        }
        notifySimulator(QStringLiteral("reserve"), pileId, order.value(QStringLiteral("id")).toInteger());
        sendResponse(request, QStringLiteral("reservation.create.result"), QJsonObject{{QStringLiteral("order"), order}, {QStringLiteral("pile"), pile}});
    }

    void handleReservationConfirm(const Message &request)
    {
        qint64 userId = 0, orderId = 0;
        if (!requestIds(request.payload, &userId, &orderId, QStringLiteral("order_id"))) { sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("user_id and order_id must be positive integers")); return; }
        QJsonObject order; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.confirmReservation(request.id, userId, orderId, &order, &error, &kind)) { sendDatabaseError(request.id, kind, error, QStringLiteral("confirm reservation failed")); return; }
        sendResponse(request, QStringLiteral("reservation.confirm.result"), QJsonObject{{QStringLiteral("order"), order}});
    }

    void handleReservationCancel(const Message &request)
    {
        qint64 userId = 0, orderId = 0;
        if (!requestIds(request.payload, &userId, &orderId, QStringLiteral("order_id"))) { sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("user_id and order_id must be positive integers")); return; }
        QJsonObject order, pile; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.cancelReservation(request.id, userId, orderId, &order, &pile, &error, &kind)) { sendDatabaseError(request.id, kind, error, QStringLiteral("cancel reservation failed")); return; }
        notifySimulator(QStringLiteral("release"), pile.value(QStringLiteral("id")).toInteger(), orderId);
        sendResponse(request, QStringLiteral("reservation.cancel.result"), QJsonObject{{QStringLiteral("order"), order}, {QStringLiteral("pile"), pile}});
    }

    void handleChargingStart(const Message &request)
    {
        qint64 userId = 0, orderId = 0, pileId = 0;
        if (!positiveId(request.payload.value(QStringLiteral("user_id")), &userId)
            || (request.payload.contains(QStringLiteral("order_id")) && !positiveId(request.payload.value(QStringLiteral("order_id")), &orderId))
            || (request.payload.contains(QStringLiteral("pile_id")) && !positiveId(request.payload.value(QStringLiteral("pile_id")), &pileId))
            || (orderId == 0 && pileId == 0) || (orderId > 0 && pileId > 0)) {
            sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("provide exactly one positive order_id or pile_id")); return;
        }
        QJsonObject order, pile; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.startCharging(request.id, userId, orderId, pileId, &order, &pile, &error, &kind)) { sendDatabaseError(request.id, kind, error, QStringLiteral("start charging failed")); return; }
        notifySimulator(QStringLiteral("start_charging"), pile.value(QStringLiteral("id")).toInteger(), order.value(QStringLiteral("id")).toInteger());
        sendResponse(request, QStringLiteral("charging.start.result"), QJsonObject{{QStringLiteral("order"), order}, {QStringLiteral("pile"), pile}});
    }

    void handleChargingStop(const Message &request)
    {
        qint64 userId = 0, orderId = 0;
        if (!requestIds(request.payload, &userId, &orderId, QStringLiteral("order_id"))) { sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("user_id and order_id must be positive integers")); return; }
        const QJsonValue endedValue = request.payload.value(QStringLiteral("ended_at"));
        if (!endedValue.isUndefined() && !endedValue.isString()) { sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("ended_at must be an ISO-8601 string")); return; }
        QJsonObject order; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.stopCharging(request.id, userId, orderId, endedValue.toString(), &order, &error, &kind)) { sendDatabaseError(request.id, kind, error, QStringLiteral("stop charging failed")); return; }
        notifySimulator(QStringLiteral("stop_charging"), order.value(QStringLiteral("pile_id")).toInteger(), orderId);
        sendResponse(request, QStringLiteral("charging.stop.result"), QJsonObject{{QStringLiteral("order"), order}, {QStringLiteral("estimated_amount_cents"), order.value(QStringLiteral("total_amount_cents"))}});
    }

    void handleChargingSettle(const Message &request)
    {
        qint64 userId = 0, orderId = 0;
        if (!requestIds(request.payload, &userId, &orderId, QStringLiteral("order_id"))) { sendError(request.id, ErrorCode::InvalidRequest, QStringLiteral("user_id and order_id must be positive integers")); return; }
        QJsonObject order; qint64 balance = 0; QString error; ErrorKind kind = ErrorKind::None;
        if (!database_.settleCharging(request.id, userId, orderId, &order, &balance, &error, &kind)) { sendDatabaseError(request.id, kind, error, QStringLiteral("settle charging failed")); return; }
        notifySimulator(QStringLiteral("settle"), order.value(QStringLiteral("pile_id")).toInteger(), orderId);
        sendResponse(request, QStringLiteral("charging.settle.result"), QJsonObject{{QStringLiteral("order"), order}, {QStringLiteral("balance_cents"), balance}});
    }

    void sendResponse(const Message &request, const QString &type, const QJsonObject &payload)
    {
        if (!responseFitsFrameLimit(request.id, type, payload)) {
            const ErrorCode sizeCode = type.startsWith(QStringLiteral("map."))
                || type.startsWith(QStringLiteral("admin.map."))
                ? ErrorCode::MapResponseTooLarge : ErrorCode::InternalError;
            sendError(request.id, sizeCode,
                      QStringLiteral("response exceeds protocol payload limit"));
            return;
        }
        const QByteArray frame = encodeFrame(Message{kProtocolVersion, request.id, type, payload});
        if (frame.isEmpty()) {
            const ErrorCode sizeCode = type.startsWith(QStringLiteral("map."))
                || type.startsWith(QStringLiteral("admin.map."))
                ? ErrorCode::MapResponseTooLarge : ErrorCode::InternalError;
            sendError(request.id, sizeCode,
                      QStringLiteral("response exceeds protocol payload limit"));
            return;
        }
        socket_->write(frame);
    }

    static bool responseFitsFrameLimit(const QString &id, const QString &type,
                                       const QJsonObject &payload)
    {
        return payloadFitsLimit(Message{kProtocolVersion, id, type, payload});
    }

    void sendError(const QString &id, ErrorCode code, const QString &message)
    {
        const QString responseId = id.isEmpty() ? QStringLiteral("server") : id;
        QJsonObject payload = errorPayload(code, message);
        if (!responseFitsFrameLimit(responseId, QStringLiteral("error"), payload)) {
            payload = errorPayload(ErrorCode::InternalError,
                                   QStringLiteral("error response exceeds protocol payload limit"));
        }
        socket_->write(encodeFrame(Message{kProtocolVersion, responseId,
                                           QStringLiteral("error"), payload}));
    }

    QTcpSocket *socket_;
    FrameDecoder decoder_;
    ev::database::Database database_;
    ev::server::map::MapService mapService_;
    AdminSessionStore *adminSessions_;
    SimulationSessionRegistry *simulators_;
    ev::server::map::SimulationGateway simulationGateway_{&database_};
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ev-server"));
    QTcpServer server;
    AdminSessionStore adminSessions;
    SimulationSessionRegistry simulators;
    const QString databasePath = qEnvironmentVariable("EV_DATABASE_PATH",
                                                       QStringLiteral("var/ev-charging.db"));
    const QString seedPath = qEnvironmentVariable("EV_DATABASE_SEED_PATH");
    QString schemaPath = qEnvironmentVariable("EV_SCHEMA_PATH");
    if (schemaPath.isEmpty()) {
        const QStringList candidates = {
            QDir::current().filePath(QStringLiteral("database/schema/schema.sql")),
            QDir(QCoreApplication::applicationDirPath()).filePath(
                QStringLiteral("../../database/schema/schema.sql")),
            QDir(QCoreApplication::applicationDirPath()).filePath(
                QStringLiteral("../database/schema/schema.sql"))};
        for (const QString &candidate : candidates) {
            if (QFileInfo::exists(candidate)) {
                schemaPath = QFileInfo(candidate).canonicalFilePath();
                break;
            }
        }
        if (schemaPath.isEmpty()) schemaPath = candidates.first();
    }
    if (!databasePath.startsWith(QLatin1Char(':'))) {
        const QFileInfo databaseInfo(databasePath);
        if (!databaseInfo.dir().mkpath(QStringLiteral("."))) {
            qCritical() << "cannot create database directory" << databaseInfo.dir().path();
            return 1;
        }
    }
    qInfo() << "ev-server database" << databasePath << "schema" << schemaPath;
    const QHostAddress host(qEnvironmentVariable("EV_SERVER_HOST", QStringLiteral("127.0.0.1")));
    bool portOk = false;
    const int configuredPort = qEnvironmentVariableIntValue("EV_SERVER_PORT", &portOk);
    if (qEnvironmentVariableIsSet("EV_SERVER_PORT")
        && (!portOk || configuredPort < 1
            || configuredPort > std::numeric_limits<quint16>::max())) {
        qCritical() << "EV_SERVER_PORT must be an integer between 1 and 65535";
        return 1;
    }
    const quint16 listenPort = portOk ? static_cast<quint16>(configuredPort) : 45454;
    if (!server.listen(host, listenPort)) {
        qCritical() << "listen failed" << host.toString() << listenPort << server.errorString();
        return 1;
    }
    qInfo() << "ev-server listening on" << host.toString() << listenPort;
    QObject::connect(&server, &QTcpServer::newConnection, &server,
                     [&server, databasePath, schemaPath, seedPath, &adminSessions, &simulators] {
        while (server.hasPendingConnections())
            new ClientConnection(server.nextPendingConnection(), databasePath, schemaPath, seedPath,
                                 &adminSessions, &simulators, &server);
    });
    return app.exec();
}
