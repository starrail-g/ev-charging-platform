#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVector>

namespace ev::database {

enum class ErrorKind {
    None,
    InvalidArgument,
    Unauthorized,
    AccountFrozen,
    NotFound,
    Conflict,
    InsufficientBalance,
    Database
};

struct MapPoi {
    QString providerPoiId;
    QString name;
    QString address;
    double latitude = 0.0;
    double longitude = 0.0;
    qint64 distanceMeters = 0;
};

struct SimulationChange {
    qint64 pileId = 0;
    QString fromStatus;
    QString toStatus;
    QString reason;
};

// Owns one SQLite connection. Instances must only be used from their owning
// thread, matching Qt's QSqlDatabase connection affinity requirements.
class Database final {
public:
    Database(QString databasePath, QString schemaPath, QString seedPath = {});
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(QString *error = nullptr);
    bool isOpen() const;

    // Looks up a phone number or creates a new active user atomically.
    bool loginUser(const QString &phone, QJsonObject *user, QString *error = nullptr,
                   ErrorKind *kind = nullptr);
    bool getUserProfile(qint64 userId, QJsonObject *user, QString *error = nullptr,
                        ErrorKind *kind = nullptr);
    bool updateUserProfile(const QString &requestId, qint64 userId,
                           const QJsonObject &changes, QJsonObject *user,
                           QString *error = nullptr, ErrorKind *kind = nullptr);
    bool rechargeWallet(const QString &requestId, qint64 userId, qint64 amountCents,
                        qint64 *balanceCents, qint64 *transactionId,
                        QString *error = nullptr, ErrorKind *kind = nullptr);
    bool listStations(QJsonArray *stations, QString *error = nullptr,
                      ErrorKind *kind = nullptr);
    bool listPiles(qint64 stationId, QJsonArray *piles, QString *error = nullptr,
                   ErrorKind *kind = nullptr);
    bool getActiveOrder(qint64 userId, QJsonObject *order, bool *found,
                        QString *error = nullptr, ErrorKind *kind = nullptr);
    bool listOrderHistory(qint64 userId, QJsonArray *orders, QString *error = nullptr,
                          ErrorKind *kind = nullptr);
    bool loginAdministrator(const QString &username, const QString &password,
                            QJsonObject *administrator, QString *error = nullptr,
                            ErrorKind *kind = nullptr);
    bool getAdministrator(qint64 administratorId, QJsonObject *administrator,
                          QString *error = nullptr, ErrorKind *kind = nullptr);
    bool getStatistics(const QString &range, QJsonObject *statistics,
                       QString *error = nullptr, ErrorKind *kind = nullptr);
    bool listAdminStations(const QString &queryText, QJsonArray *stations,
                           QString *error = nullptr, ErrorKind *kind = nullptr);
    // Lists one ID-cursor page of piles, including piles at inactive stations.
    // hasMore is true when a later page exists after the returned rows.
    bool listAdminPiles(qint64 afterId, qint64 limit, QJsonArray *piles,
                        bool *hasMore, QString *error = nullptr,
                        ErrorKind *kind = nullptr);
    bool createStation(const QString &requestId, qint64 administratorId, const QString &name,
                       const QString &address, double latitude, double longitude,
                       qint64 pileCount, QJsonObject *station,
                       QString *error = nullptr, ErrorKind *kind = nullptr);
    bool restartPile(const QString &requestId, qint64 administratorId, qint64 pileId, QJsonObject *pile,
                     QString *error = nullptr, ErrorKind *kind = nullptr);
    bool listAdminUsers(const QString &phoneQuery, QJsonArray *users,
                        QString *error = nullptr, ErrorKind *kind = nullptr);
    bool findRequestReplay(const QString &requestId, const QString &operation,
                           const QString &fingerprint, QJsonObject *response,
                           bool *found, QString *error = nullptr,
                           ErrorKind *kind = nullptr);

    // Map search persistence. The method owns the import transaction: a POI,
    // its first deterministic pile set, audit row, and successful replay row
    // either all commit or none do. `mapOnlyPayload` is deliberately separate
    // from the returned business snapshot so cache callers cannot persist pile
    // status by accident.
    bool importMapStations(const QString &requestId, qint64 userId,
                           const QJsonObject &normalizedQuery,
                           const QJsonObject &resolvedOrigin,
                           const QVector<MapPoi> &pois,
                           const QString &dataSource,
                           const QJsonObject &warning,
                           bool hasMore, const QString &nextPageToken,
                           QJsonObject *response,
                           QString *error = nullptr, ErrorKind *kind = nullptr);
    bool readMapCache(const QString &cacheKey, QJsonObject *mapOnlyPayload,
                      bool *fresh, bool *stale,
                      QString *error = nullptr, ErrorKind *kind = nullptr);
    bool writeMapCache(const QString &cacheKey, const QString &operation,
                       const QJsonObject &mapOnlyPayload,
                       const QString &providerCursor,
                       const QDateTime &fetchedAt, const QDateTime &expiresAt,
                       const QDateTime &staleUntil,
                       QString *error = nullptr, ErrorKind *kind = nullptr);
    bool getStationMapDestination(qint64 stationId, QJsonObject *destination,
                                  QString *error = nullptr, ErrorKind *kind = nullptr);
    bool recordMapRequest(const QString &requestId, const QString &operation,
                          qint64 userId, const QJsonObject &normalizedQuery,
                          const QString &cacheKey, const QString &resultStatus,
                          const QString &dataSource, int errorCode, int warningCode,
                          qint64 *logId, QString *error = nullptr,
                          ErrorKind *kind = nullptr);
    bool recordMapUpstreamCall(qint64 mapRequestLogId, const QString &callType,
                               const QString &resultStatus, int httpStatus,
                               qint64 latencyMs, int errorCode,
                               QString *error = nullptr, ErrorKind *kind = nullptr);
    bool listMapAudit(const QString &operation, const QString &resultStatus,
                      const QString &beforeCreatedAt, qint64 beforeId,
                      qint64 limit, QJsonArray *records,
                      bool *hasMore, QString *error = nullptr,
                      ErrorKind *kind = nullptr);

    // Authoritative simulator gateway operation. The simulator only submits a
    // proposal; this method validates and persists it in one transaction.
    bool applySimulationProposal(const QString &simulatorId, const QString &seedId,
                                 qint64 tickId,
                                 const QHash<qint64, qint64> &expectedVersions,
                                 const QVector<SimulationChange> &changes,
                                 QJsonObject *result,
                                 QString *error = nullptr,
                                 ErrorKind *kind = nullptr);
    // Returns the current server-owned snapshot consumed by a simulator
    // session. This is read-only; the simulator never opens SQLite directly.
    bool getSimulationSnapshot(QJsonObject *snapshot, QString *error = nullptr,
                               ErrorKind *kind = nullptr);
    bool setUserStatus(const QString &requestId, qint64 administratorId, qint64 userId, const QString &status,
                       QJsonObject *user, QString *error = nullptr,
                       ErrorKind *kind = nullptr);
    bool createReservation(const QString &requestId, qint64 userId, qint64 pileId, QJsonObject *order,
                           QJsonObject *pile, QString *error = nullptr,
                           ErrorKind *kind = nullptr);
    bool confirmReservation(const QString &requestId, qint64 userId, qint64 orderId, QJsonObject *order,
                            QString *error = nullptr, ErrorKind *kind = nullptr);
    bool cancelReservation(const QString &requestId, qint64 userId, qint64 orderId, QJsonObject *order,
                           QJsonObject *pile, QString *error = nullptr,
                           ErrorKind *kind = nullptr);
    bool startCharging(const QString &requestId, qint64 userId, qint64 orderId, qint64 pileId,
                       QJsonObject *order, QJsonObject *pile,
                       QString *error = nullptr, ErrorKind *kind = nullptr);
    bool stopCharging(const QString &requestId, qint64 userId, qint64 orderId, const QString &endedAt,
                      QJsonObject *order, QString *error = nullptr,
                      ErrorKind *kind = nullptr);
    bool settleCharging(const QString &requestId, qint64 userId, qint64 orderId, QJsonObject *order,
                        qint64 *balanceCents, QString *error = nullptr,
                        ErrorKind *kind = nullptr);

private:
    bool initializeSchema(QString *error);
    bool migrateV03ToV04(QString *error);
    bool executeSchemaScript(const QString &script, QString *error);
    bool ensureRequestTable(QString *error);
    bool bumpPileStationSnapshot(qint64 pileId, QSqlQuery *query,
                                 QString *error, ErrorKind *kind);
    bool readUser(QSqlQuery &query, QJsonObject *user, QString *error) const;
    bool readOrder(QSqlQuery &query, QJsonObject *order, QString *error);
    bool readPile(QSqlQuery &query, QJsonObject *pile, QString *error) const;
    bool begin(QString *error, ErrorKind *kind);
    bool rollback();
    bool commit(QString *error, ErrorKind *kind);
    bool loadRequest(const QString &requestId, const QString &operation,
                     const QString &fingerprint, QJsonObject *response, bool *found,
                     QString *error, ErrorKind *kind);
    bool saveRequest(const QString &requestId, const QString &operation,
                     const QString &fingerprint, const QJsonObject &response,
                     QString *error, ErrorKind *kind);
    bool getStationUtilizations(const QDateTime &periodStart, const QDateTime &periodEnd,
                                QHash<qint64, double> *utilizations,
                                QString *error = nullptr, ErrorKind *kind = nullptr);
    bool checkAdministrator(qint64 administratorId, bool superAdminOnly,
                            QString *error, ErrorKind *kind);
    void setFailure(QString *error, ErrorKind *kind, ErrorKind value,
                    const QString &message) const;
    void close();

    QSqlDatabase connection_;
    QString connectionName_;
    QString databasePath_;
    QString schemaPath_;
    QString seedPath_;
};

} // namespace ev::database
