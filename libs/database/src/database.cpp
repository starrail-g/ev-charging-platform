#include "ev_database/database.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QUuid>

#include <atomic>
#include <limits>

namespace ev::database {
namespace {

std::atomic_uint64_t nextConnectionId{1};

QString utcNow()
{
    QString value = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (value.endsWith(QStringLiteral("+00:00"))) {
        value.chop(6);
        value.append(QLatin1Char('Z'));
    }
    return value;
}

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

QString queryError(const QSqlQuery &query)
{
    return query.lastError().text();
}

bool isDigitsOnly(const QString &value)
{
    if (value.size() != 11) return false;
    for (const QChar character : value) {
        if (!character.isDigit() || character.unicode() > 0x7f) return false;
    }
    return true;
}

QString jsonFingerprint(const QJsonObject &payload)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(payload).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}

// Split statements while retaining semicolons inside CREATE TRIGGER bodies.
// The project schema contains no semicolons in quoted literals, but quoted
// strings are handled here so future schema messages remain safe.
QStringList splitSchemaStatements(const QString &script)
{
    QStringList statements;
    QString current;
    bool singleQuote = false;
    bool doubleQuote = false;
    bool trigger = false;
    for (int i = 0; i < script.size(); ++i) {
        const QChar character = script.at(i);
        current.append(character);
        if (character == QLatin1Char('\'') && !doubleQuote) {
            if (i + 1 < script.size() && script.at(i + 1) == QLatin1Char('\'')) {
                current.append(script.at(++i));
            } else {
                singleQuote = !singleQuote;
            }
        } else if (character == QLatin1Char('"') && !singleQuote) {
            doubleQuote = !doubleQuote;
        }
        if (!singleQuote && !doubleQuote && character == QLatin1Char(';')) {
            const QString trimmed = current.trimmed();
            const QString upper = trimmed.toUpper();
            if (!trigger && upper.startsWith(QStringLiteral("CREATE TRIGGER"))) trigger = true;
            if (!trigger || upper.endsWith(QStringLiteral("END;"))) {
                if (!trimmed.isEmpty()) statements.append(trimmed);
                current.clear();
                trigger = false;
            }
        }
    }
    if (!current.trimmed().isEmpty()) statements.append(current.trimmed());
    return statements;
}

} // namespace

Database::Database(QString databasePath, QString schemaPath, QString seedPath)
    : connectionName_(QStringLiteral("ev_database_%1").arg(nextConnectionId.fetch_add(1))),
      databasePath_(std::move(databasePath)), schemaPath_(std::move(schemaPath)),
      seedPath_(std::move(seedPath))
{
}

Database::~Database()
{
    close();
}

bool Database::open(QString *error)
{
    if (connection_.isOpen()) return true;
    connection_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    connection_.setDatabaseName(databasePath_);
    if (!connection_.open()) {
        setError(error, QStringLiteral("open database failed: %1").arg(connection_.lastError().text()));
        close();
        return false;
    }

    QSqlQuery pragma(connection_);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(error, QStringLiteral("enable foreign keys failed: %1").arg(queryError(pragma)));
        close();
        return false;
    }
    if (!pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"))) {
        setError(error, QStringLiteral("set busy timeout failed: %1").arg(queryError(pragma)));
        close();
        return false;
    }
    if (!initializeSchema(error)) {
        close();
        return false;
    }
    return true;
}

bool Database::isOpen() const
{
    return connection_.isOpen();
}

bool Database::initializeSchema(QString *error)
{
    QSqlQuery query(connection_);
    if (!query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'schema_meta'"))) {
        setError(error, QStringLiteral("inspect schema failed: %1").arg(queryError(query)));
        return false;
    }
    const bool hasSchemaMeta = query.next();
    bool emptyDatabase = !hasSchemaMeta;
    if (!hasSchemaMeta) {
        if (!query.exec(QStringLiteral(
                "SELECT COUNT(*) FROM sqlite_master "
                "WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"))) {
            setError(error, QStringLiteral("inspect database tables failed: %1").arg(queryError(query)));
            return false;
        }
        emptyDatabase = query.next() && query.value(0).toInt() == 0;
    }
    if (!hasSchemaMeta) {
        QFile schemaFile(schemaPath_);
        if (!schemaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            setError(error, QStringLiteral("open schema file failed: %1").arg(schemaFile.errorString()));
            return false;
        }
        if (!executeSchemaScript(QString::fromUtf8(schemaFile.readAll()), error)) return false;
    }

    if (!query.exec(QStringLiteral("SELECT value FROM schema_meta WHERE key = 'schema_version'"))) {
        setError(error, QStringLiteral("read schema version failed: %1").arg(queryError(query)));
        return false;
    }
    if (!query.next() || query.value(0).toString() != QStringLiteral("0.3")) {
        setError(error, QStringLiteral("unsupported or missing schema version (expected 0.3; migrate v0.2 databases first)"));
        return false;
    }
    if (!ensureRequestTable(error)) return false;
    if (emptyDatabase && !seedPath_.isEmpty()) {
        QFile seedFile(seedPath_);
        if (!seedFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            setError(error, QStringLiteral("open seed file failed: %1").arg(seedFile.errorString()));
            return false;
        }
        if (!executeSchemaScript(QString::fromUtf8(seedFile.readAll()), error)) return false;
    }
    return true;
}

bool Database::ensureRequestTable(QString *error)
{
    QSqlQuery query(connection_);
    if (query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS request_records ("
            "request_id TEXT PRIMARY KEY CHECK(length(request_id) BETWEEN 1 AND 64),"
            "operation TEXT NOT NULL CHECK(length(operation) BETWEEN 1 AND 64),"
            "fingerprint TEXT NOT NULL CHECK(length(fingerprint) > 0),"
            "response_json TEXT NOT NULL CHECK(length(response_json) > 0),"
            "created_at TEXT NOT NULL)"))) {
        return query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS ix_request_records_created ON request_records(created_at)"));
    }
    setError(error, QStringLiteral("initialize request records failed: %1").arg(queryError(query)));
    return false;
}

bool Database::executeSchemaScript(const QString &script, QString *error)
{
    QString cleanedScript = script;
    cleanedScript.replace(QRegularExpression(QStringLiteral("(?m)--[^\\n]*$")), QString());
    const QStringList statements = splitSchemaStatements(cleanedScript);
    if (statements.isEmpty()) {
        setError(error, QStringLiteral("schema file is empty"));
        return false;
    }

    bool transactionStarted = false;
    QSqlQuery query(connection_);
    for (const QString &statement : statements) {
        const QString upper = statement.trimmed().toUpper();
        if (upper == QStringLiteral("BEGIN;") || upper == QStringLiteral("BEGIN")) transactionStarted = true;
        if (!query.exec(statement)) {
            if (transactionStarted) {
                QSqlQuery rollback(connection_);
                rollback.exec(QStringLiteral("ROLLBACK"));
            }
            setError(error, QStringLiteral("schema statement failed: %1").arg(queryError(query)));
            return false;
        }
        if (upper == QStringLiteral("COMMIT;") || upper == QStringLiteral("COMMIT")) transactionStarted = false;
    }
    return true;
}

bool Database::readUser(QSqlQuery &query, QJsonObject *user, QString *error) const
{
    if (!query.next()) {
        setError(error, QStringLiteral("user row not found"));
        return false;
    }
    if (!user) {
        setError(error, QStringLiteral("user output is null"));
        return false;
    }
    const QJsonValue avatar = query.value(3).isNull()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(query.value(3).toString());
    *user = QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                        {QStringLiteral("phone"), query.value(1).toString()},
                        {QStringLiteral("nickname"), query.value(2).toString()},
                        {QStringLiteral("avatar_path"), avatar},
                        {QStringLiteral("balance_cents"), query.value(4).toLongLong()},
                        {QStringLiteral("status"), query.value(5).toString()}};
    return true;
}

bool Database::loginUser(const QString &phone, QJsonObject *user, QString *error,
                         ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!user) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("user output is null"));
        return false;
    }
    if (!isDigitsOnly(phone)) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("phone must contain exactly 11 ASCII digits"));
        return false;
    }
    if (!open(error)) return false;

    QSqlQuery query(connection_);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("begin login transaction failed: %1").arg(queryError(query)));
        return false;
    }

    query.prepare(QStringLiteral(
        "SELECT id, phone, nickname, avatar_path, balance_cents, status "
        "FROM users WHERE phone = :phone"));
    query.bindValue(QStringLiteral(":phone"), phone);
    if (!query.exec()) {
        QSqlQuery rollback(connection_);
        rollback.exec(QStringLiteral("ROLLBACK"));
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("lookup user failed: %1").arg(queryError(query)));
        return false;
    }
    if (query.next()) {
        const QJsonValue avatar = query.value(3).isNull()
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(query.value(3).toString());
        *user = QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                            {QStringLiteral("phone"), query.value(1).toString()},
                            {QStringLiteral("nickname"), query.value(2).toString()},
                            {QStringLiteral("avatar_path"), avatar},
                            {QStringLiteral("balance_cents"), query.value(4).toLongLong()},
                            {QStringLiteral("status"), query.value(5).toString()}};
    } else {
        const QString timestamp = utcNow();
        query.prepare(QStringLiteral(
            "INSERT INTO users(phone, nickname, balance_cents, status, created_at, updated_at) "
            "VALUES (:phone, :nickname, 0, 'active', :created_at, :updated_at)"));
        query.bindValue(QStringLiteral(":phone"), phone);
        query.bindValue(QStringLiteral(":nickname"), QStringLiteral("用户") + phone.right(4));
        query.bindValue(QStringLiteral(":created_at"), timestamp);
        query.bindValue(QStringLiteral(":updated_at"), timestamp);
        if (!query.exec()) {
            QSqlQuery rollback(connection_);
            rollback.exec(QStringLiteral("ROLLBACK"));
            setFailure(error, kind, ErrorKind::Database,
                       QStringLiteral("register user failed: %1").arg(queryError(query)));
            return false;
        }
        query.prepare(QStringLiteral(
            "SELECT id, phone, nickname, avatar_path, balance_cents, status "
            "FROM users WHERE phone = :phone"));
        query.bindValue(QStringLiteral(":phone"), phone);
        if (!query.exec() || !readUser(query, user, error)) {
            QSqlQuery rollback(connection_);
            rollback.exec(QStringLiteral("ROLLBACK"));
            if (error && error->isEmpty())
                setFailure(error, kind, ErrorKind::Database,
                           QStringLiteral("read registered user failed"));
            return false;
        }
    }

    QSqlQuery commit(connection_);
    if (!commit.exec(QStringLiteral("COMMIT"))) {
        QSqlQuery rollback(connection_);
        rollback.exec(QStringLiteral("ROLLBACK"));
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("commit login transaction failed: %1").arg(queryError(commit)));
        return false;
    }
    return true;
}

bool Database::getUserProfile(qint64 userId, QJsonObject *user, QString *error,
                              ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!user || userId <= 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("user_id must be positive"));
        return false;
    }
    if (!open(error)) {
        if (kind) *kind = ErrorKind::Database;
        return false;
    }

    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT id, phone, nickname, avatar_path, balance_cents, status "
        "FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("read user profile failed: %1").arg(queryError(query)));
        return false;
    }
    if (!query.next()) {
        setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found"));
        return false;
    }
    const QJsonValue avatar = query.value(3).isNull()
        ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(3).toString());
    *user = QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                        {QStringLiteral("phone"), query.value(1).toString()},
                        {QStringLiteral("nickname"), query.value(2).toString()},
                        {QStringLiteral("avatar_path"), avatar},
                        {QStringLiteral("balance_cents"), query.value(4).toLongLong()},
                        {QStringLiteral("status"), query.value(5).toString()}};
    return true;
}

bool Database::updateUserProfile(const QString &requestId, qint64 userId,
                                 const QJsonObject &changes, QJsonObject *user,
                                 QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    const bool hasNickname = changes.contains(QStringLiteral("nickname"));
    const bool hasAvatar = changes.contains(QStringLiteral("avatar_path"));
    const QJsonValue nicknameValue = changes.value(QStringLiteral("nickname"));
    const QJsonValue avatarValue = changes.value(QStringLiteral("avatar_path"));
    if (!user || userId <= 0 || requestId.isEmpty() || (!hasNickname && !hasAvatar)
        || (hasNickname && (!nicknameValue.isString()
            || nicknameValue.toString().trimmed().isEmpty()))
        || (hasAvatar && !avatarValue.isString() && !avatarValue.isNull())) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("profile update fields are invalid"));
        return false;
    }
    if (!open(error)) {
        if (kind) *kind = ErrorKind::Database;
        return false;
    }

    QJsonObject fingerprintPayload{{QStringLiteral("user_id"), userId}};
    if (hasNickname) {
        fingerprintPayload.insert(QStringLiteral("nickname"),
                                  nicknameValue.toString().trimmed());
    }
    if (hasAvatar) fingerprintPayload.insert(QStringLiteral("avatar_path"), avatarValue);
    const QString fingerprint = jsonFingerprint(fingerprintPayload);
    if (!begin(error, kind)) return false;

    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT id FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("inspect user profile failed: %1").arg(queryError(query)));
        return false;
    }
    if (!query.next()) {
        rollback();
        setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found"));
        return false;
    }
    QJsonObject replay;
    bool found = false;
    if (!loadRequest(requestId, QStringLiteral("user.profile.update"), fingerprint,
                     &replay, &found, error, kind)) {
        rollback();
        return false;
    }
    if (found) {
        *user = replay.value(QStringLiteral("user")).toObject();
        return commit(error, kind);
    }

    const QString timestamp = utcNow();
    QStringList assignments{QStringLiteral("updated_at = :updated_at")};
    if (hasNickname) assignments.append(QStringLiteral("nickname = :nickname"));
    if (hasAvatar) assignments.append(QStringLiteral("avatar_path = :avatar_path"));
    query.prepare(QStringLiteral("UPDATE users SET %1 WHERE id = :id")
                      .arg(assignments.join(QStringLiteral(", "))));
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    query.bindValue(QStringLiteral(":id"), userId);
    if (hasNickname) {
        query.bindValue(QStringLiteral(":nickname"), nicknameValue.toString().trimmed());
    }
    if (hasAvatar) {
        query.bindValue(QStringLiteral(":avatar_path"),
                        avatarValue.isNull() ? QVariant() : avatarValue.toString());
    }
    if (!query.exec()) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("update user profile failed: %1").arg(queryError(query)));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("user is unavailable"));
        return false;
    }

    query.prepare(QStringLiteral(
        "SELECT id, phone, nickname, avatar_path, balance_cents, status "
        "FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec() || !readUser(query, user, error)) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("read updated profile failed"));
        return false;
    }
    if (!saveRequest(requestId, QStringLiteral("user.profile.update"), fingerprint,
                     QJsonObject{{QStringLiteral("user"), *user}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
}

bool Database::rechargeWallet(const QString &requestId, qint64 userId,
                              qint64 amountCents, qint64 *balanceCents,
                              qint64 *transactionId, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!balanceCents || !transactionId || userId <= 0 || amountCents <= 0
        || requestId.isEmpty()) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("user_id, amount_cents and request id are required"));
        return false;
    }
    if (!open(error)) {
        if (kind) *kind = ErrorKind::Database;
        return false;
    }

    const QString fingerprint = jsonFingerprint(QJsonObject{
        {QStringLiteral("amount_cents"), amountCents},
        {QStringLiteral("user_id"), userId}});
    if (!begin(error, kind)) return false;

    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT balance_cents, status FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("inspect wallet user failed: %1").arg(queryError(query)));
        return false;
    }
    if (!query.next()) {
        rollback();
        setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found"));
        return false;
    }
    QJsonObject replay;
    bool found = false;
    if (!loadRequest(requestId, QStringLiteral("wallet.recharge"), fingerprint,
                     &replay, &found, error, kind)) {
        rollback();
        return false;
    }
    if (found) {
        *balanceCents = replay.value(QStringLiteral("balance_cents")).toVariant().toLongLong();
        *transactionId = replay.value(QStringLiteral("transaction_id")).toVariant().toLongLong();
        return commit(error, kind);
    }
    if (query.value(1).toString() != QStringLiteral("active")) {
        rollback();
        setFailure(error, kind, ErrorKind::AccountFrozen,
                   QStringLiteral("user is frozen"));
        return false;
    }
    const qint64 before = query.value(0).toLongLong();
    if (amountCents > std::numeric_limits<qint64>::max() - before) {
        rollback();
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("amount_cents is too large"));
        return false;
    }
    const qint64 after = before + amountCents;
    const QString timestamp = utcNow();
    query.prepare(QStringLiteral(
        "UPDATE users SET balance_cents = :after, updated_at = :updated_at "
        "WHERE id = :id AND status = 'active' AND balance_cents = :before"));
    query.bindValue(QStringLiteral(":after"), after);
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    query.bindValue(QStringLiteral(":id"), userId);
    query.bindValue(QStringLiteral(":before"), before);
    if (!query.exec() || query.numRowsAffected() != 1) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("update wallet balance failed"));
        return false;
    }

    query.prepare(QStringLiteral(
        "INSERT INTO wallet_transactions(user_id, transaction_type, amount_cents, "
        "balance_after_cents, idempotency_key, created_at) "
        "VALUES (:user_id, 'recharge', :amount, :after, :key, :created_at)"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":amount"), amountCents);
    query.bindValue(QStringLiteral(":after"), after);
    query.bindValue(QStringLiteral(":key"), requestId);
    query.bindValue(QStringLiteral(":created_at"), timestamp);
    if (!query.exec()) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("write recharge transaction failed: %1").arg(queryError(query)));
        return false;
    }
    const qint64 insertedTransactionId = query.lastInsertId().toLongLong();
    const QJsonObject response{{QStringLiteral("balance_cents"), after},
                               {QStringLiteral("transaction_id"), insertedTransactionId}};
    if (!saveRequest(requestId, QStringLiteral("wallet.recharge"), fingerprint,
                     response, error, kind)) {
        rollback();
        return false;
    }
    if (!commit(error, kind)) return false;
    *balanceCents = after;
    *transactionId = insertedTransactionId;
    return true;
}

void Database::setFailure(QString *error, ErrorKind *kind, ErrorKind value,
                           const QString &message) const
{
    if (kind) *kind = value;
    if (error) *error = message;
}

bool Database::begin(QString *error, ErrorKind *kind)
{
    QSqlQuery query(connection_);
    if (query.exec(QStringLiteral("BEGIN IMMEDIATE"))) return true;
    setFailure(error, kind, ErrorKind::Database,
               QStringLiteral("begin transaction failed: %1").arg(queryError(query)));
    return false;
}

bool Database::rollback()
{
    QSqlQuery query(connection_);
    return query.exec(QStringLiteral("ROLLBACK"));
}

bool Database::commit(QString *error, ErrorKind *kind)
{
    QSqlQuery query(connection_);
    if (query.exec(QStringLiteral("COMMIT"))) return true;
    setFailure(error, kind, ErrorKind::Database,
               QStringLiteral("commit transaction failed: %1").arg(queryError(query)));
    rollback();
    return false;
}

bool Database::loadRequest(const QString &requestId, const QString &operation,
                           const QString &fingerprint, QJsonObject *response,
                           bool *found, QString *error, ErrorKind *kind)
{
    if (found) *found = false;
    if (requestId.isEmpty()) return true;
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT operation, fingerprint, response_json FROM request_records "
        "WHERE request_id = :request_id"));
    query.bindValue(QStringLiteral(":request_id"), requestId);
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("load request record failed: %1").arg(queryError(query)));
        return false;
    }
    if (!query.next()) return true;
    if (found) *found = true;
    if (query.value(0).toString() != operation || query.value(1).toString() != fingerprint) {
        setFailure(error, kind, ErrorKind::Conflict,
                   QStringLiteral("request id was already used for a different operation"));
        return false;
    }
    if (!response) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("request response output is null"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        query.value(2).toString().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("request record contains invalid response JSON"));
        return false;
    }
    *response = document.object();
    return true;
}

bool Database::saveRequest(const QString &requestId, const QString &operation,
                           const QString &fingerprint, const QJsonObject &response,
                           QString *error, ErrorKind *kind)
{
    if (requestId.isEmpty()) return true;
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "INSERT INTO request_records(request_id, operation, fingerprint, response_json, created_at) "
        "VALUES (:request_id, :operation, :fingerprint, :response_json, :created_at)"));
    query.bindValue(QStringLiteral(":request_id"), requestId);
    query.bindValue(QStringLiteral(":operation"), operation);
    query.bindValue(QStringLiteral(":fingerprint"), fingerprint);
    query.bindValue(QStringLiteral(":response_json"),
                    QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":created_at"), utcNow());
    if (query.exec()) return true;
    setFailure(error, kind, ErrorKind::Database,
               QStringLiteral("save request record failed: %1").arg(queryError(query)));
    return false;
}

bool Database::readPile(QSqlQuery &query, QJsonObject *pile, QString *error) const
{
    if (!pile) {
        setFailure(error, nullptr, ErrorKind::InvalidArgument,
                   QStringLiteral("pile output is null"));
        return false;
    }
    *pile = QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                        {QStringLiteral("station_id"), query.value(1).toLongLong()},
                        {QStringLiteral("pile_code"), query.value(2).toString()},
                        {QStringLiteral("pile_type"), query.value(3).toString()},
                        {QStringLiteral("power_kw"), query.value(4).toDouble()},
                        {QStringLiteral("unit_price_cents_per_kwh"), query.value(5).toLongLong()},
                        {QStringLiteral("status"), query.value(6).toString()},
                        {QStringLiteral("total_charge_count"), query.value(7).toLongLong()},
                        {QStringLiteral("total_charge_seconds"), query.value(8).toLongLong()},
                        {QStringLiteral("restart_count"), query.value(9).toLongLong()},
                        {QStringLiteral("last_restart_at"), query.value(10).isNull()
                            ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(10).toString())}};
    return true;
}

bool Database::readOrder(QSqlQuery &query, QJsonObject *order, QString *error)
{
    if (!order) {
        setError(error, QStringLiteral("order output is null"));
        return false;
    }
    auto nullable = [&query](int index) {
        return query.value(index).isNull()
            ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(index).toString());
    };
    *order = QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                         {QStringLiteral("order_no"), query.value(1).toString()},
                         {QStringLiteral("user_id"), query.value(2).toLongLong()},
                         {QStringLiteral("pile_id"), query.value(3).toLongLong()},
                         {QStringLiteral("status"), query.value(4).toString()},
                         {QStringLiteral("reserved_at"), nullable(5)},
                         {QStringLiteral("started_at"), nullable(6)},
                         {QStringLiteral("ended_at"), nullable(7)},
                         {QStringLiteral("energy_wh"), query.value(8).toLongLong()},
                         {QStringLiteral("unit_price_cents_per_kwh"), query.value(9).toLongLong()},
                         {QStringLiteral("service_fee_cents"), query.value(10).toLongLong()},
                         {QStringLiteral("total_amount_cents"), query.value(11).toLongLong()},
                         {QStringLiteral("settled_at"), nullable(12)},
                         {QStringLiteral("created_at"), query.value(13).toString()},
                         {QStringLiteral("updated_at"), query.value(14).toString()}};
    QSqlQuery displayQuery(connection_);
    displayQuery.prepare(QStringLiteral(
        "SELECT s.name, s.address, p.pile_code FROM charging_piles AS p "
        "JOIN stations AS s ON s.id = p.station_id WHERE p.id = :pile_id"));
    displayQuery.bindValue(QStringLiteral(":pile_id"), query.value(3).toLongLong());
    if (!displayQuery.exec() || !displayQuery.next()) {
        setError(error, QStringLiteral("order display fields not found"));
        return false;
    }
    order->insert(QStringLiteral("station_name"), displayQuery.value(0).toString());
    order->insert(QStringLiteral("station_address"), displayQuery.value(1).toString());
    order->insert(QStringLiteral("pile_code"), displayQuery.value(2).toString());
    return true;
}

bool Database::listStations(QJsonArray *stations, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!stations) {
        setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("stations output is null"));
        return false;
    }
    if (!open(error)) {
        if (kind) *kind = ErrorKind::Database;
        return false;
    }
    QSqlQuery query(connection_);
    if (!query.exec(QStringLiteral(
            "SELECT s.id, s.name, s.address, s.latitude, s.longitude, s.status, "
            "COUNT(p.id), "
            "COALESCE(SUM(CASE WHEN p.status = 'idle' THEN 1 ELSE 0 END), 0), "
            "COALESCE(SUM(CASE WHEN p.status = 'reserved' THEN 1 ELSE 0 END), 0), "
            "COALESCE(SUM(CASE WHEN p.status = 'charging' THEN 1 ELSE 0 END), 0) "
            "FROM stations s LEFT JOIN charging_piles p ON p.station_id = s.id "
            "WHERE s.status = 'active' GROUP BY s.id ORDER BY s.id"))) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("list stations failed: %1").arg(queryError(query)));
        return false;
    }
    *stations = QJsonArray();
    while (query.next()) {
        stations->append(QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                                     {QStringLiteral("name"), query.value(1).toString()},
                                     {QStringLiteral("address"), query.value(2).toString()},
                                     {QStringLiteral("latitude"), query.value(3).toDouble()},
                                     {QStringLiteral("longitude"), query.value(4).toDouble()},
                                     {QStringLiteral("status"), query.value(5).toString()},
                                     {QStringLiteral("pile_total"), query.value(6).toLongLong()},
                                     {QStringLiteral("pile_idle"), query.value(7).toLongLong()},
                                     {QStringLiteral("pile_reserved"), query.value(8).toLongLong()},
                                     {QStringLiteral("pile_charging"), query.value(9).toLongLong()}});
    }
    return true;
}

bool Database::listPiles(qint64 stationId, QJsonArray *piles, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!piles || stationId <= 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("station_id must be positive"));
        return false;
    }
    if (!open(error)) {
        if (kind) *kind = ErrorKind::Database;
        return false;
    }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT id FROM stations WHERE id = :id AND status = 'active'"));
    query.bindValue(QStringLiteral(":id"), stationId);
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("inspect station failed: %1").arg(queryError(query)));
        return false;
    }
    if (!query.next()) {
        setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("station not found"));
        return false;
    }
    query.prepare(QStringLiteral(
        "SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, "
        "status, total_charge_count, total_charge_seconds, restart_count, last_restart_at "
        "FROM charging_piles WHERE station_id = :station_id ORDER BY id"));
    query.bindValue(QStringLiteral(":station_id"), stationId);
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("list piles failed: %1").arg(queryError(query)));
        return false;
    }
    *piles = QJsonArray();
    while (query.next()) {
        QJsonObject pile;
        if (!readPile(query, &pile, error)) return false;
        piles->append(pile);
    }
    return true;
}

bool Database::getActiveOrder(qint64 userId, QJsonObject *order, bool *found,
                              QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!order || !found || userId <= 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("user_id must be positive"));
        return false;
    }
    if (!open(error)) {
        if (kind) *kind = ErrorKind::Database;
        return false;
    }
    QSqlQuery userQuery(connection_);
    userQuery.prepare(QStringLiteral("SELECT 1 FROM users WHERE id = :id"));
    userQuery.bindValue(QStringLiteral(":id"), userId);
    if (!userQuery.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("inspect user failed: %1").arg(queryError(userQuery)));
        return false;
    }
    if (!userQuery.next()) {
        setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found"));
        return false;
    }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, "
        "energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, "
        "settled_at, created_at, updated_at FROM charging_orders "
        "WHERE user_id = :user_id AND status IN "
        "('pending_reservation','reserved','charging','pending_settlement') "
        "ORDER BY id DESC LIMIT 1"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("get active order failed: %1").arg(queryError(query)));
        return false;
    }
    if (!query.next()) {
        *found = false;
        *order = QJsonObject();
        return true;
    }
    *found = true;
    return readOrder(query, order, error);
}

bool Database::listOrderHistory(qint64 userId, QJsonArray *orders, QString *error,
                                ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!orders || userId <= 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("user_id must be positive"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    QSqlQuery userQuery(connection_);
    userQuery.prepare(QStringLiteral("SELECT 1 FROM users WHERE id = :id"));
    userQuery.bindValue(QStringLiteral(":id"), userId);
    if (!userQuery.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("inspect user failed: %1").arg(queryError(userQuery)));
        return false;
    }
    if (!userQuery.next()) {
        setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found"));
        return false;
    }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, "
        "energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, "
        "settled_at, created_at, updated_at FROM charging_orders "
        "WHERE user_id = :user_id AND status = 'completed' "
        "ORDER BY settled_at DESC, id DESC"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("list order history failed: %1").arg(queryError(query)));
        return false;
    }
    *orders = QJsonArray();
    while (query.next()) {
        QJsonObject order;
        if (!readOrder(query, &order, error)) {
            if (kind) *kind = ErrorKind::Database;
            return false;
        }
        orders->append(order);
    }
    return true;
}

bool Database::createReservation(const QString &requestId, qint64 userId, qint64 pileId, QJsonObject *order,
                                 QJsonObject *pile, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!order || !pile || userId <= 0 || pileId <= 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("user_id and pile_id must be positive"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    const QString fingerprint = QStringLiteral("%1:%2").arg(userId).arg(pileId);
    if (!begin(error, kind)) return false;
    QJsonObject replay; bool found = false;
    if (!loadRequest(requestId, QStringLiteral("reservation.create"), fingerprint,
                     &replay, &found, error, kind)) { rollback(); return false; }
    if (found) {
        *order = replay.value(QStringLiteral("order")).toObject();
        *pile = replay.value(QStringLiteral("pile")).toObject();
        return commit(error, kind);
    }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT status FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("lookup user failed: %1").arg(queryError(query))); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found")); return false; }
    if (query.value(0).toString() != QStringLiteral("active")) {
        rollback(); setFailure(error, kind, ErrorKind::AccountFrozen, QStringLiteral("user is frozen")); return false;
    }
    query.prepare(QStringLiteral(
        "SELECT 1 FROM charging_orders WHERE user_id = :user_id AND status IN "
        "('pending_reservation','reserved','charging','pending_settlement') LIMIT 1"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("check active order failed: %1").arg(queryError(query))); return false; }
    if (query.next()) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("user already has an active order")); return false; }
    query.prepare(QStringLiteral(
        "SELECT p.unit_price_cents_per_kwh, p.status FROM charging_piles AS p "
        "JOIN stations AS s ON s.id = p.station_id AND s.status = 'active' "
        "WHERE p.id = :id"));
    query.bindValue(QStringLiteral(":id"), pileId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("pile not found")); return false; }
    if (query.value(1).toString() != QStringLiteral("idle")) {
        rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("pile is not idle")); return false;
    }
    const qint64 price = query.value(0).toLongLong();
    const QString timestamp = utcNow();
    const QString orderNo = QStringLiteral("ORD-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.prepare(QStringLiteral(
        "INSERT INTO charging_orders(order_no, user_id, pile_id, status, reserved_at, "
        "unit_price_cents_per_kwh, created_at, updated_at) VALUES "
        "(:order_no, :user_id, :pile_id, 'pending_reservation', :reserved_at, :price, :created_at, :updated_at)"));
    query.bindValue(QStringLiteral(":order_no"), orderNo);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":pile_id"), pileId);
    query.bindValue(QStringLiteral(":reserved_at"), timestamp);
    query.bindValue(QStringLiteral(":price"), price);
    query.bindValue(QStringLiteral(":created_at"), timestamp);
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("create reservation failed: %1").arg(queryError(query))); return false; }
    const qint64 orderId = query.lastInsertId().toLongLong();
    query.prepare(QStringLiteral("UPDATE charging_piles SET status = 'reserved', updated_at = :updated_at WHERE id = :id AND status = 'idle'"));
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    query.bindValue(QStringLiteral(":id"), pileId);
    if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("pile was claimed by another request")); return false; }
    query.prepare(QStringLiteral(
        "SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, "
        "unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at "
        "FROM charging_orders WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), orderId);
    if (!query.exec() || !query.next() || !readOrder(query, order, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read reservation failed")); return false; }
    query.prepare(QStringLiteral(
        "SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, status, "
        "total_charge_count, total_charge_seconds, restart_count, last_restart_at FROM charging_piles WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), pileId);
    if (!query.exec() || !query.next() || !readPile(query, pile, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read reserved pile failed")); return false; }
    if (!saveRequest(requestId, QStringLiteral("reservation.create"), fingerprint,
                     QJsonObject{{QStringLiteral("order"), *order},
                                 {QStringLiteral("pile"), *pile}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
}

bool Database::confirmReservation(const QString &requestId, qint64 userId, qint64 orderId, QJsonObject *order,
                                  QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!order || userId <= 0 || orderId <= 0) { setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("user_id and order_id must be positive")); return false; }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    const QString fingerprint = QStringLiteral("%1:%2").arg(userId).arg(orderId);
    if (!begin(error, kind)) return false;
    QJsonObject replay; bool found = false;
    if (!loadRequest(requestId, QStringLiteral("reservation.confirm"), fingerprint,
                     &replay, &found, error, kind)) { rollback(); return false; }
    if (found) { *order = replay.value(QStringLiteral("order")).toObject(); return commit(error, kind); }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT status FROM users WHERE id = :user_id"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found")); return false; }
    if (query.value(0).toString() != QStringLiteral("active")) {
        rollback();
        setFailure(error, kind, ErrorKind::AccountFrozen, QStringLiteral("user is frozen"));
        return false;
    }
    query.prepare(QStringLiteral("UPDATE charging_orders SET status = 'reserved', updated_at = :updated_at WHERE id = :id AND user_id = :user_id AND status = 'pending_reservation'"));
    query.bindValue(QStringLiteral(":updated_at"), utcNow());
    query.bindValue(QStringLiteral(":id"), orderId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
    if (query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("reservation is not pending confirmation")); return false; }
    query.prepare(QStringLiteral("SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at FROM charging_orders WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), orderId);
    if (!query.exec() || !query.next() || !readOrder(query, order, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read confirmed reservation failed")); return false; }
    if (!saveRequest(requestId, QStringLiteral("reservation.confirm"), fingerprint,
                     QJsonObject{{QStringLiteral("order"), *order}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
}

bool Database::cancelReservation(const QString &requestId, qint64 userId, qint64 orderId, QJsonObject *order,
                                 QJsonObject *pile, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!order || !pile || userId <= 0 || orderId <= 0) { setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("user_id and order_id must be positive")); return false; }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    const QString fingerprint = QStringLiteral("%1:%2").arg(userId).arg(orderId);
    if (!begin(error, kind)) return false;
    QJsonObject replay; bool found = false;
    if (!loadRequest(requestId, QStringLiteral("reservation.cancel"), fingerprint,
                     &replay, &found, error, kind)) { rollback(); return false; }
    if (found) {
        *order = replay.value(QStringLiteral("order")).toObject();
        *pile = replay.value(QStringLiteral("pile")).toObject();
        return commit(error, kind);
    }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT pile_id, status FROM charging_orders WHERE id = :id AND user_id = :user_id"));
    query.bindValue(QStringLiteral(":id"), orderId); query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("reservation not found")); return false; }
    const qint64 pileId = query.value(0).toLongLong();
    const QString status = query.value(1).toString();
    if (status != QStringLiteral("pending_reservation") && status != QStringLiteral("reserved")) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("order cannot be cancelled")); return false; }
    const QString timestamp = utcNow();
    query.prepare(QStringLiteral("UPDATE charging_orders SET status = 'cancelled', updated_at = :updated_at WHERE id = :id AND status IN ('pending_reservation','reserved')"));
    query.bindValue(QStringLiteral(":updated_at"), timestamp); query.bindValue(QStringLiteral(":id"), orderId);
    if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("reservation changed concurrently")); return false; }
    query.prepare(QStringLiteral(
        "UPDATE charging_piles SET status = 'idle', updated_at = :updated_at "
        "WHERE id = :pile_id AND status = 'reserved' AND EXISTS ("
        "SELECT 1 FROM charging_orders WHERE id = :order_id AND pile_id = :pile_id "
        "AND user_id = :user_id AND status = 'cancelled')"));
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    query.bindValue(QStringLiteral(":pile_id"), pileId);
    query.bindValue(QStringLiteral(":order_id"), orderId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("reserved pile is not available")); return false; }
    query.prepare(QStringLiteral("SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at FROM charging_orders WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), orderId);
    if (!query.exec() || !query.next() || !readOrder(query, order, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read cancelled order failed")); return false; }
    query.prepare(QStringLiteral("SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, status, total_charge_count, total_charge_seconds, restart_count, last_restart_at FROM charging_piles WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), pileId);
    if (!query.exec() || !query.next() || !readPile(query, pile, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read released pile failed")); return false; }
    if (!saveRequest(requestId, QStringLiteral("reservation.cancel"), fingerprint,
                     QJsonObject{{QStringLiteral("order"), *order},
                                 {QStringLiteral("pile"), *pile}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
}

bool Database::startCharging(const QString &requestId, qint64 userId, qint64 orderId, qint64 pileId,
                             QJsonObject *order, QJsonObject *pile,
                             QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!order || !pile || userId <= 0 || (orderId <= 0 && pileId <= 0)) { setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("user_id and an order_id or pile_id are required")); return false; }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    const QString fingerprint = QStringLiteral("%1:%2:%3").arg(userId).arg(orderId).arg(pileId);
    if (!begin(error, kind)) return false;
    QJsonObject replay; bool found = false;
    if (!loadRequest(requestId, QStringLiteral("charging.start"), fingerprint,
                     &replay, &found, error, kind)) { rollback(); return false; }
    if (found) {
        *order = replay.value(QStringLiteral("order")).toObject();
        *pile = replay.value(QStringLiteral("pile")).toObject();
        return commit(error, kind);
    }
    QSqlQuery query(connection_);
    const QString timestamp = utcNow();
    qint64 selectedPile = pileId;
    qint64 selectedOrder = orderId;
    query.prepare(QStringLiteral("SELECT status FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found")); return false; }
    if (query.value(0).toString() != QStringLiteral("active")) {
        rollback(); setFailure(error, kind, ErrorKind::AccountFrozen, QStringLiteral("user is frozen")); return false;
    }
    if (orderId > 0) {
        query.prepare(QStringLiteral("SELECT pile_id, status FROM charging_orders WHERE id = :id AND user_id = :user_id"));
        query.bindValue(QStringLiteral(":id"), orderId); query.bindValue(QStringLiteral(":user_id"), userId);
        if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
        if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("order not found")); return false; }
        selectedPile = query.value(0).toLongLong();
        if (query.value(1).toString() != QStringLiteral("reserved")) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("order is not reserved")); return false; }
    } else {
        query.prepare(QStringLiteral("SELECT 1 FROM charging_orders WHERE user_id = :id AND status IN ('pending_reservation','reserved','charging','pending_settlement') LIMIT 1")); query.bindValue(QStringLiteral(":id"), userId);
        if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
        if (query.next()) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("user already has an active order")); return false; }
        query.prepare(QStringLiteral("SELECT p.unit_price_cents_per_kwh, p.status FROM charging_piles AS p JOIN stations AS s ON s.id = p.station_id AND s.status = 'active' WHERE p.id = :id")); query.bindValue(QStringLiteral(":id"), pileId);
        if (!query.exec() || !query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("pile not found")); return false; }
        if (query.value(1).toString() != QStringLiteral("idle")) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("pile is not idle")); return false; }
        const qint64 price = query.value(0).toLongLong();
        const QString orderNo = QStringLiteral("ORD-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        query.prepare(QStringLiteral("INSERT INTO charging_orders(order_no,user_id,pile_id,status,started_at,unit_price_cents_per_kwh,created_at,updated_at) VALUES (:order_no,:user_id,:pile_id,'charging',:started_at,:price,:created_at,:updated_at)"));
        query.bindValue(QStringLiteral(":order_no"), orderNo); query.bindValue(QStringLiteral(":user_id"), userId); query.bindValue(QStringLiteral(":pile_id"), pileId); query.bindValue(QStringLiteral(":started_at"), timestamp); query.bindValue(QStringLiteral(":price"), price); query.bindValue(QStringLiteral(":created_at"), timestamp); query.bindValue(QStringLiteral(":updated_at"), timestamp);
        if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("start charging failed: %1").arg(queryError(query))); return false; }
        selectedOrder = query.lastInsertId().toLongLong();
    }
    const QString expectedPileStatus = orderId > 0 ? QStringLiteral("reserved") : QStringLiteral("idle");
    query.prepare(QStringLiteral("UPDATE charging_piles SET status = 'charging', updated_at = :updated_at WHERE id = :id AND status = :expected_status"));
    query.bindValue(QStringLiteral(":updated_at"), timestamp); query.bindValue(QStringLiteral(":id"), selectedPile);
    query.bindValue(QStringLiteral(":expected_status"), expectedPileStatus);
    if (!query.exec() || query.numRowsAffected() != 1) {
        rollback();
        setFailure(error, kind, ErrorKind::Conflict,
                   expectedPileStatus == QStringLiteral("idle")
                       ? QStringLiteral("pile is not idle")
                       : QStringLiteral("pile is not reserved"));
        return false;
    }
    if (orderId > 0) {
        query.prepare(QStringLiteral("UPDATE charging_orders SET status = 'charging', started_at = :started_at, updated_at = :updated_at WHERE id = :id AND status = 'reserved'"));
        query.bindValue(QStringLiteral(":started_at"), timestamp); query.bindValue(QStringLiteral(":updated_at"), timestamp); query.bindValue(QStringLiteral(":id"), selectedOrder);
        if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("order changed concurrently")); return false; }
    }
    query.prepare(QStringLiteral("SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at FROM charging_orders WHERE id = :id")); query.bindValue(QStringLiteral(":id"), selectedOrder);
    if (!query.exec() || !query.next() || !readOrder(query, order, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read started order failed")); return false; }
    query.prepare(QStringLiteral("SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, status, total_charge_count, total_charge_seconds, restart_count, last_restart_at FROM charging_piles WHERE id = :id")); query.bindValue(QStringLiteral(":id"), selectedPile);
    if (!query.exec() || !query.next() || !readPile(query, pile, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read charging pile failed")); return false; }
    if (!saveRequest(requestId, QStringLiteral("charging.start"), fingerprint,
                     QJsonObject{{QStringLiteral("order"), *order},
                                 {QStringLiteral("pile"), *pile}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
}

bool Database::stopCharging(const QString &requestId, qint64 userId, qint64 orderId, const QString &endedAt,
                            QJsonObject *order, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!order || userId <= 0 || orderId <= 0) { setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("user_id and order_id must be positive")); return false; }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    const QString fingerprint = QStringLiteral("%1:%2:%3").arg(userId).arg(orderId).arg(endedAt);
    if (!begin(error, kind)) return false;
    QJsonObject replay; bool found = false;
    if (!loadRequest(requestId, QStringLiteral("charging.stop"), fingerprint,
                     &replay, &found, error, kind)) { rollback(); return false; }
    if (found) { *order = replay.value(QStringLiteral("order")).toObject(); return commit(error, kind); }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT o.pile_id, o.started_at, o.unit_price_cents_per_kwh, p.power_kw FROM charging_orders AS o JOIN charging_piles AS p ON p.id = o.pile_id WHERE o.id = :order_id AND o.user_id = :owner_id AND o.status = 'charging'"));
    query.bindValue(QStringLiteral(":order_id"), orderId);
    query.bindValue(QStringLiteral(":owner_id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("stop lookup failed: %1").arg(queryError(query))); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("order is not charging")); return false; }
    const qint64 pileId = query.value(0).toLongLong();
    const QString started = query.value(1).toString();
    const qint64 price = query.value(2).toLongLong();
    QSqlQuery feeQuery(connection_);
    feeQuery.prepare(QStringLiteral("SELECT service_fee_cents FROM charging_orders WHERE id = :id AND user_id = :user_id AND status = 'charging'"));
    feeQuery.bindValue(QStringLiteral(":id"), orderId);
    feeQuery.bindValue(QStringLiteral(":user_id"), userId);
    if (!feeQuery.exec() || !feeQuery.next()) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("read service fee failed: %1").arg(queryError(feeQuery)));
        return false;
    }
    const qint64 serviceFee = feeQuery.value(0).toLongLong();
    const double powerKw = query.value(3).toDouble();
    const QString finish = endedAt.isEmpty() ? utcNow() : endedAt;
    const QDateTime startedTime = QDateTime::fromString(started, Qt::ISODate);
    const QDateTime finishTime = QDateTime::fromString(finish, Qt::ISODate);
    if (!finishTime.isValid() || !startedTime.isValid() || finishTime < startedTime) {
        rollback();
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("ended_at must be a valid timestamp after started_at"));
        return false;
    }
    const qint64 seconds = qMax<qint64>(1, startedTime.secsTo(finishTime));
    const qint64 energyWh = qMax<qint64>(1, static_cast<qint64>(powerKw * 1000.0 * seconds / 3600.0));
    const qint64 total = (energyWh * price + 999) / 1000 + serviceFee;
    if (total <= 0) {
        rollback();
        setFailure(error, kind, ErrorKind::Conflict,
                   QStringLiteral("charging amount must be positive"));
        return false;
    }
    QSqlQuery updateQuery(connection_);
    updateQuery.prepare(QStringLiteral("UPDATE charging_orders SET status='pending_settlement', ended_at=:ended_at, energy_wh=:energy_wh, total_amount_cents=:total, updated_at=:updated_at WHERE id=:id AND status='charging'"));
    updateQuery.bindValue(QStringLiteral(":ended_at"), finish); updateQuery.bindValue(QStringLiteral(":energy_wh"), energyWh); updateQuery.bindValue(QStringLiteral(":total"), total); updateQuery.bindValue(QStringLiteral(":updated_at"), finish); updateQuery.bindValue(QStringLiteral(":id"), orderId);
    if (!updateQuery.exec() || updateQuery.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("order changed concurrently: %1").arg(queryError(updateQuery))); return false; }
    QSqlQuery pileQuery(connection_);
    pileQuery.prepare(QStringLiteral("UPDATE charging_piles SET status='idle', updated_at=:updated_at WHERE id=:id AND status='charging'"));
    pileQuery.bindValue(QStringLiteral(":updated_at"), finish);
    pileQuery.bindValue(QStringLiteral(":id"), pileId);
    if (!pileQuery.exec() || pileQuery.numRowsAffected() != 1) {
        rollback();
        setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("charging pile is not active"));
        return false;
    }
    QSqlQuery readQuery(connection_);
    readQuery.prepare(QStringLiteral("SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at FROM charging_orders WHERE id=?")); readQuery.bindValue(0, orderId);
    if (!readQuery.exec() || !readQuery.next() || !readOrder(readQuery, order, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read stopped order failed: %1").arg(queryError(readQuery))); return false; }
    if (!saveRequest(requestId, QStringLiteral("charging.stop"), fingerprint,
                     QJsonObject{{QStringLiteral("order"), *order},
                                 {QStringLiteral("estimated_amount_cents"), total}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
}

bool Database::settleCharging(const QString &requestId, qint64 userId, qint64 orderId, QJsonObject *order,
                              qint64 *balanceCents, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!order || !balanceCents || userId <= 0 || orderId <= 0) { setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("user_id and order_id must be positive")); return false; }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    const QString fingerprint = QStringLiteral("%1:%2").arg(userId).arg(orderId);
    if (!begin(error, kind)) return false;
    QJsonObject replay; bool found = false;
    if (!loadRequest(requestId, QStringLiteral("charging.settle"), fingerprint,
                     &replay, &found, error, kind)) { rollback(); return false; }
    if (found) {
        *order = replay.value(QStringLiteral("order")).toObject();
        *balanceCents = replay.value(QStringLiteral("balance_cents")).toVariant().toLongLong();
        return commit(error, kind);
    }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT pile_id, total_amount_cents, started_at, ended_at, status FROM charging_orders WHERE id=:id AND user_id=:user_id")); query.bindValue(QStringLiteral(":id"), orderId); query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("order not found")); return false; }
    const qint64 pileId = query.value(0).toLongLong();
    const qint64 total = query.value(1).toLongLong();
    const QString started = query.value(2).toString();
    const QString ended = query.value(3).toString();
    if (query.value(4).toString() != QStringLiteral("pending_settlement")) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("order is not pending settlement")); return false; }
    query.prepare(QStringLiteral("SELECT balance_cents FROM users WHERE id=:id")); query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read wallet balance failed: %1").arg(queryError(query))); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found")); return false; }
    const qint64 balance = query.value(0).toLongLong();
    if (balance < total) { rollback(); setFailure(error, kind, ErrorKind::InsufficientBalance, QStringLiteral("insufficient balance")); return false; }
    const qint64 after = balance - total;
    const QString settled = utcNow();
    query.prepare(QStringLiteral("INSERT INTO wallet_transactions(user_id,order_id,transaction_type,amount_cents,balance_after_cents,idempotency_key,created_at) VALUES (:user_id,:order_id,'charge',:amount,:after,:key,:created_at)"));
    query.bindValue(QStringLiteral(":user_id"), userId); query.bindValue(QStringLiteral(":order_id"), orderId); query.bindValue(QStringLiteral(":amount"), -total); query.bindValue(QStringLiteral(":after"), after); query.bindValue(QStringLiteral(":key"), QStringLiteral("charge-%1").arg(orderId)); query.bindValue(QStringLiteral(":created_at"), settled);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("write charge ledger failed: %1").arg(queryError(query))); return false; }
    query.prepare(QStringLiteral("UPDATE users SET balance_cents=:balance, updated_at=:updated_at WHERE id=:id AND balance_cents=:before")); query.bindValue(QStringLiteral(":balance"), after); query.bindValue(QStringLiteral(":updated_at"), settled); query.bindValue(QStringLiteral(":id"), userId); query.bindValue(QStringLiteral(":before"), balance);
    if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("update wallet balance failed")); return false; }
    query.prepare(QStringLiteral("UPDATE charging_orders SET status='completed', settled_at=:settled_at, updated_at=:updated_at WHERE id=:id AND status='pending_settlement'")); query.bindValue(QStringLiteral(":settled_at"), settled); query.bindValue(QStringLiteral(":updated_at"), settled); query.bindValue(QStringLiteral(":id"), orderId);
    if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("complete order failed")); return false; }
    const qint64 seconds = QDateTime::fromString(started, Qt::ISODate).secsTo(QDateTime::fromString(ended, Qt::ISODate));
    query.prepare(QStringLiteral("UPDATE charging_piles SET total_charge_count=total_charge_count+1, total_charge_seconds=total_charge_seconds+:seconds, updated_at=:updated_at WHERE id=:id")); query.bindValue(QStringLiteral(":seconds"), qMax<qint64>(0, seconds)); query.bindValue(QStringLiteral(":updated_at"), settled); query.bindValue(QStringLiteral(":id"), pileId);
    if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("update charging pile counters failed")); return false; }
    query.prepare(QStringLiteral("SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at FROM charging_orders WHERE id=:id")); query.bindValue(QStringLiteral(":id"), orderId);
    if (!query.exec() || !query.next() || !readOrder(query, order, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read settled order failed")); return false; }
    *balanceCents = after;
    if (!saveRequest(requestId, QStringLiteral("charging.settle"), fingerprint,
                     QJsonObject{{QStringLiteral("order"), *order},
                                 {QStringLiteral("balance_cents"), after}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
}

void Database::close()
{
    if (!connectionName_.isEmpty()) {
        if (connection_.isOpen()) connection_.close();
        connection_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

} // namespace ev::database
