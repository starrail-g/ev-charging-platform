#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

namespace ev::database {

enum class ErrorKind {
    None,
    InvalidArgument,
    NotFound,
    Conflict,
    InsufficientBalance,
    Database
};

// Owns one SQLite connection. Instances must only be used from their owning
// thread, matching Qt's QSqlDatabase connection affinity requirements.
class Database final {
public:
    Database(QString databasePath, QString schemaPath);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(QString *error = nullptr);
    bool isOpen() const;

    // Looks up a phone number or creates a new active user atomically.
    bool loginUser(const QString &phone, QJsonObject *user, QString *error = nullptr,
                   ErrorKind *kind = nullptr);
    bool listStations(QJsonArray *stations, QString *error = nullptr,
                      ErrorKind *kind = nullptr);
    bool listPiles(qint64 stationId, QJsonArray *piles, QString *error = nullptr,
                   ErrorKind *kind = nullptr);
    bool getActiveOrder(qint64 userId, QJsonObject *order, bool *found,
                        QString *error = nullptr, ErrorKind *kind = nullptr);
    bool createReservation(qint64 userId, qint64 pileId, QJsonObject *order,
                           QJsonObject *pile, QString *error = nullptr,
                           ErrorKind *kind = nullptr);
    bool confirmReservation(qint64 userId, qint64 orderId, QJsonObject *order,
                            QString *error = nullptr, ErrorKind *kind = nullptr);
    bool cancelReservation(qint64 userId, qint64 orderId, QJsonObject *order,
                           QJsonObject *pile, QString *error = nullptr,
                           ErrorKind *kind = nullptr);
    bool startCharging(qint64 userId, qint64 orderId, qint64 pileId,
                       QJsonObject *order, QJsonObject *pile,
                       QString *error = nullptr, ErrorKind *kind = nullptr);
    bool stopCharging(qint64 userId, qint64 orderId, const QString &endedAt,
                      QJsonObject *order, QString *error = nullptr,
                      ErrorKind *kind = nullptr);
    bool settleCharging(qint64 userId, qint64 orderId, QJsonObject *order,
                        qint64 *balanceCents, QString *error = nullptr,
                        ErrorKind *kind = nullptr);

private:
    bool initializeSchema(QString *error);
    bool executeSchemaScript(const QString &script, QString *error);
    bool readUser(QSqlQuery &query, QJsonObject *user, QString *error) const;
    bool readOrder(QSqlQuery &query, QJsonObject *order, QString *error) const;
    bool readPile(QSqlQuery &query, QJsonObject *pile, QString *error) const;
    bool begin(QString *error, ErrorKind *kind);
    bool rollback();
    bool commit(QString *error, ErrorKind *kind);
    void setFailure(QString *error, ErrorKind *kind, ErrorKind value,
                    const QString &message) const;
    void close();

    QSqlDatabase connection_;
    QString connectionName_;
    QString databasePath_;
    QString schemaPath_;
};

} // namespace ev::database
