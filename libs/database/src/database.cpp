#include "ev_database/database.h"
#include "ev_database/pile_generator.h"

#include <QDate>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTime>
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
    if (!query.next()) {
        setError(error, QStringLiteral("unsupported or missing schema version"));
        return false;
    }
    const QString schemaVersion = query.value(0).toString();
    if (schemaVersion == QStringLiteral("0.3")) {
        if (!migrateV03ToV04(error)) return false;
    } else if (schemaVersion != QStringLiteral("0.4")) {
        setError(error, QStringLiteral("unsupported or missing schema version (expected 0.3 or 0.4; migrate older databases first)"));
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

bool Database::migrateV03ToV04(QString *error)
{
    // Keep the server self-starting for deployed v0.3 databases. The same
    // statements are published in database/migrations/003_v0.3_to_v0.4.sql;
    // this inline path is used when the server is launched without the Python
    // migration helper. Every statement runs in one IMMEDIATE transaction.
    QSqlQuery query(connection_);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        setError(error, QStringLiteral("begin v0.4 migration failed: %1").arg(queryError(query)));
        return false;
    }
    const QStringList statements = {
        QStringLiteral("ALTER TABLE stations ADD COLUMN provider TEXT NOT NULL DEFAULT 'internal'"),
        QStringLiteral("ALTER TABLE stations ADD COLUMN provider_poi_id TEXT"),
        QStringLiteral("ALTER TABLE stations ADD COLUMN map_synced_at TEXT"),
        QStringLiteral("ALTER TABLE stations ADD COLUMN map_content_hash TEXT"),
        QStringLiteral("ALTER TABLE charging_piles ADD COLUMN simulated INTEGER NOT NULL DEFAULT 0 CHECK (simulated IN (0, 1))"),
        QStringLiteral("ALTER TABLE charging_piles ADD COLUMN status_source TEXT NOT NULL DEFAULT 'seed' CHECK (status_source IN ('seed', 'business', 'simulation', 'admin'))"),
        QStringLiteral("ALTER TABLE charging_piles ADD COLUMN status_updated_at TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("UPDATE charging_piles SET status_updated_at = updated_at WHERE status_updated_at = ''"),
        QStringLiteral("CREATE UNIQUE INDEX ux_stations_provider_poi ON stations(provider, provider_poi_id) WHERE provider_poi_id IS NOT NULL"),
        QStringLiteral("CREATE TABLE map_request_logs (id INTEGER PRIMARY KEY, request_id TEXT NOT NULL CHECK (length(request_id) BETWEEN 1 AND 64), operation TEXT NOT NULL CHECK (operation IN ('map.station.search', 'map.route.plan')), user_id INTEGER REFERENCES users(id), normalized_query_json TEXT NOT NULL CHECK (length(normalized_query_json) > 0), cache_key TEXT, result_status TEXT NOT NULL CHECK (result_status IN ('success', 'error', 'degraded', 'mock')), data_source TEXT CHECK (data_source IS NULL OR data_source IN ('tencent_live', 'tencent_cache', 'tencent_stale', 'server_mock')), error_code INTEGER, warning_code INTEGER, created_at TEXT NOT NULL, completed_at TEXT)"),
        QStringLiteral("CREATE INDEX ix_map_request_logs_created ON map_request_logs(created_at DESC, id DESC)"),
        QStringLiteral("CREATE INDEX ix_map_request_logs_operation_status ON map_request_logs(operation, result_status, created_at DESC, id DESC)"),
        QStringLiteral("CREATE TABLE map_upstream_call_logs (id INTEGER PRIMARY KEY, map_request_log_id INTEGER NOT NULL REFERENCES map_request_logs(id) ON DELETE CASCADE, call_type TEXT NOT NULL CHECK (call_type IN ('geocode', 'poi_search', 'poi_detail', 'driving', 'walking')), result_status TEXT NOT NULL CHECK (result_status IN ('success', 'error')), http_status INTEGER, latency_ms INTEGER CHECK (latency_ms IS NULL OR latency_ms >= 0), error_code INTEGER, created_at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX ix_map_upstream_logs_request ON map_upstream_call_logs(map_request_log_id, id)"),
        QStringLiteral("CREATE INDEX ix_map_upstream_logs_created ON map_upstream_call_logs(created_at)"),
        QStringLiteral("CREATE TABLE map_cache_entries (cache_key TEXT PRIMARY KEY CHECK (length(cache_key) BETWEEN 1 AND 512), operation TEXT NOT NULL CHECK (operation IN ('map.station.search', 'map.route.plan')), payload_json TEXT NOT NULL CHECK (length(payload_json) > 0), provider_cursor TEXT, fetched_at TEXT NOT NULL, expires_at TEXT NOT NULL, stale_until TEXT NOT NULL, updated_at TEXT NOT NULL, CHECK (stale_until >= expires_at))"),
        QStringLiteral("CREATE INDEX ix_map_cache_expiry ON map_cache_entries(expires_at, stale_until)"),
        QStringLiteral("CREATE TABLE pile_status_events (id INTEGER PRIMARY KEY, pile_id INTEGER NOT NULL REFERENCES charging_piles(id) ON DELETE CASCADE, station_id INTEGER NOT NULL REFERENCES stations(id) ON DELETE CASCADE, from_status TEXT NOT NULL CHECK (from_status IN ('idle', 'reserved', 'charging', 'fault', 'offline')), to_status TEXT NOT NULL CHECK (to_status IN ('idle', 'reserved', 'charging', 'fault', 'offline')), source TEXT NOT NULL CHECK (source IN ('business', 'simulation', 'admin', 'seed')), reason TEXT, tick_id INTEGER, created_at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX ix_pile_status_events_pile_created ON pile_status_events(pile_id, created_at DESC, id DESC)"),
        QStringLiteral("CREATE INDEX ix_pile_status_events_created ON pile_status_events(created_at)"),
        QStringLiteral("CREATE TABLE simulation_state (station_id INTEGER PRIMARY KEY REFERENCES stations(id) ON DELETE CASCADE, seed_id TEXT NOT NULL CHECK (length(seed_id) BETWEEN 1 AND 128), last_tick_id INTEGER, last_run_at TEXT, snapshot_version INTEGER NOT NULL DEFAULT 0 CHECK (snapshot_version >= 0))"),
        QStringLiteral("CREATE TABLE simulation_tick_records (simulator_id TEXT NOT NULL CHECK (length(simulator_id) BETWEEN 1 AND 128), tick_id INTEGER NOT NULL CHECK (tick_id >= 0), fingerprint TEXT NOT NULL CHECK (length(fingerprint) > 0), result_status TEXT NOT NULL CHECK (result_status IN ('accepted', 'conflict', 'rejected')), response_json TEXT NOT NULL CHECK (length(response_json) > 0), created_at TEXT NOT NULL, PRIMARY KEY (simulator_id, tick_id))"),
        QStringLiteral("CREATE INDEX ix_simulation_tick_records_created ON simulation_tick_records(created_at)"),
        QStringLiteral("UPDATE schema_meta SET value = '0.4' WHERE key = 'schema_version'")
    };
    for (const QString &statement : statements) {
        if (!query.exec(statement)) {
            QSqlQuery rollback(connection_);
            rollback.exec(QStringLiteral("ROLLBACK"));
            setError(error, QStringLiteral("v0.4 migration failed: %1").arg(queryError(query)));
            return false;
        }
    }
    if (!query.exec(QStringLiteral("COMMIT"))) {
        QSqlQuery rollback(connection_);
        rollback.exec(QStringLiteral("ROLLBACK"));
        setError(error, QStringLiteral("commit v0.4 migration failed: %1").arg(queryError(query)));
        return false;
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

bool Database::bumpPileStationSnapshot(qint64 pileId, QSqlQuery *query,
                                       QString *error, ErrorKind *kind)
{
    if (!query || pileId <= 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("snapshot update arguments are invalid"));
        return false;
    }
    query->prepare(QStringLiteral(
        "UPDATE simulation_state SET snapshot_version = snapshot_version + 1 "
        "WHERE station_id = (SELECT station_id FROM charging_piles WHERE id = :pile_id)"));
    query->bindValue(QStringLiteral(":pile_id"), pileId);
    if (!query->exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("update station snapshot failed: %1").arg(queryError(*query)));
        return false;
    }
    return true;
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
    const int simulatedIndex = query.record().indexOf(QStringLiteral("simulated"));
    const int sourceIndex = query.record().indexOf(QStringLiteral("status_source"));
    const int updatedIndex = query.record().indexOf(QStringLiteral("status_updated_at"));
    // v0.3 rows are still readable by low-level tooling. Once opened through
    // Database they are migrated to v0.4, and these additive fields are
    // emitted for all public pile responses.
    pile->insert(QStringLiteral("simulated"), simulatedIndex >= 0
                 ? query.value(simulatedIndex).toInt() != 0 : false);
    pile->insert(QStringLiteral("status_source"), sourceIndex >= 0
                 ? query.value(sourceIndex).toString() : QStringLiteral("seed"));
    pile->insert(QStringLiteral("status_updated_at"), updatedIndex >= 0
                 ? query.value(updatedIndex).toString() : query.value(10).toString());
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
        "status, total_charge_count, total_charge_seconds, restart_count, last_restart_at, "
        "simulated, status_source, status_updated_at "
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

bool Database::loginAdministrator(const QString &username, const QString &password,
                                  QJsonObject *administrator, QString *error,
                                  ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!administrator || username.trimmed().isEmpty() || password.isEmpty()) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("username and password are required"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }

    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT id, username, role, status FROM administrators "
        "WHERE username = :username AND password_hash_sha256 = :password_hash"));
    query.bindValue(QStringLiteral(":username"), username.trimmed());
    query.bindValue(QStringLiteral(":password_hash"), digest);
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("administrator login failed: %1").arg(queryError(query)));
        return false;
    }
    if (!query.next() || query.value(3).toString() != QStringLiteral("active")) {
        setFailure(error, kind, ErrorKind::Unauthorized,
                   QStringLiteral("invalid administrator credentials"));
        return false;
    }
    *administrator = QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                                 {QStringLiteral("username"), query.value(1).toString()},
                                 {QStringLiteral("role"), query.value(2).toString()},
                                 {QStringLiteral("status"), query.value(3).toString()}};
    return true;
}

bool Database::getAdministrator(qint64 administratorId, QJsonObject *administrator,
                                QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!administrator || administratorId <= 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("administrator_id must be positive"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT id, username, role, status FROM administrators WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), administratorId);
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("read administrator failed: %1").arg(queryError(query)));
        return false;
    }
    if (!query.next()) {
        setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("administrator not found"));
        return false;
    }
    if (query.value(3).toString() != QStringLiteral("active")) {
        setFailure(error, kind, ErrorKind::Unauthorized,
                   QStringLiteral("administrator is disabled"));
        return false;
    }
    *administrator = QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                                 {QStringLiteral("username"), query.value(1).toString()},
                                 {QStringLiteral("role"), query.value(2).toString()},
                                 {QStringLiteral("status"), query.value(3).toString()}};
    return true;
}

bool Database::checkAdministrator(qint64 administratorId, bool superAdminOnly,
                                  QString *error, ErrorKind *kind)
{
    QJsonObject administrator;
    if (!getAdministrator(administratorId, &administrator, error, kind)) return false;
    if (superAdminOnly && administrator.value(QStringLiteral("role")).toString()
        != QStringLiteral("super_admin")) {
        setFailure(error, kind, ErrorKind::Unauthorized,
                   QStringLiteral("administrator role is not permitted"));
        return false;
    }
    return true;
}

bool Database::getStationUtilizations(const QDateTime &periodStart,
                                      const QDateTime &periodEnd,
                                      QHash<qint64, double> *utilizations,
                                      QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!utilizations || !periodStart.isValid() || !periodEnd.isValid()
        || periodStart >= periodEnd) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("utilization period is invalid"));
        return false;
    }
    if (!open(error)) {
        if (kind) *kind = ErrorKind::Database;
        return false;
    }

    const QDateTime startUtc = periodStart.toUTC();
    const QDateTime endUtc = periodEnd.toUTC();
    const qint64 periodStartMs = startUtc.toMSecsSinceEpoch();
    const qint64 periodEndMs = endUtc.toMSecsSinceEpoch();

    QHash<qint64, qint64> numeratorMsByStation;
    QHash<qint64, qint64> denominatorMsByStation;
    QHash<qint64, qint64> stationByPile;
    utilizations->clear();

    QSqlQuery query(connection_);
    if (!query.exec(QStringLiteral("SELECT id FROM stations ORDER BY id"))) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("read stations for utilization failed: %1")
                       .arg(queryError(query)));
        return false;
    }
    while (query.next()) {
        const qint64 stationId = query.value(0).toLongLong();
        numeratorMsByStation.insert(stationId, 0);
        denominatorMsByStation.insert(stationId, 0);
        utilizations->insert(stationId, 0.0);
    }

    query.prepare(QStringLiteral(
        "SELECT id, station_id, created_at FROM charging_piles ORDER BY id"));
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("read piles for utilization failed: %1")
                       .arg(queryError(query)));
        return false;
    }
    while (query.next()) {
        const qint64 pileId = query.value(0).toLongLong();
        const qint64 stationId = query.value(1).toLongLong();
        const QString createdAtText = query.value(2).toString();
        const QDateTime createdAt = QDateTime::fromString(createdAtText, Qt::ISODate);
        if (!createdAt.isValid()) {
            setFailure(error, kind, ErrorKind::Database,
                       QStringLiteral("invalid charging pile created_at for pile %1")
                           .arg(pileId));
            return false;
        }

        stationByPile.insert(pileId, stationId);
        const qint64 availableStartMs = qMax(periodStartMs,
                                             createdAt.toUTC().toMSecsSinceEpoch());
        const qint64 availableMs = qMax<qint64>(0, periodEndMs - availableStartMs);
        denominatorMsByStation[stationId] += availableMs;
    }

    query.prepare(QStringLiteral(
        "SELECT pile_id, status, started_at, ended_at FROM charging_orders "
        "WHERE status IN ('charging', 'pending_settlement', 'completed') "
        "AND started_at IS NOT NULL"));
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("read orders for utilization failed: %1")
                       .arg(queryError(query)));
        return false;
    }
    while (query.next()) {
        const qint64 pileId = query.value(0).toLongLong();
        const auto pileIterator = stationByPile.constFind(pileId);
        if (pileIterator == stationByPile.constEnd()) continue;

        const QString status = query.value(1).toString();
        const QString startedAtText = query.value(2).toString();
        const QDateTime startedAt = QDateTime::fromString(startedAtText, Qt::ISODate);
        if (!startedAt.isValid()) {
            setFailure(error, kind, ErrorKind::Database,
                       QStringLiteral("invalid charging order started_at for pile %1")
                           .arg(pileId));
            return false;
        }

        qint64 endedAtMs = periodEndMs;
        if (!query.value(3).isNull() && !query.value(3).toString().isEmpty()) {
            const QDateTime endedAt = QDateTime::fromString(query.value(3).toString(),
                                                             Qt::ISODate);
            if (!endedAt.isValid()) {
                setFailure(error, kind, ErrorKind::Database,
                           QStringLiteral("invalid charging order ended_at for pile %1")
                               .arg(pileId));
                return false;
            }
            endedAtMs = endedAt.toUTC().toMSecsSinceEpoch();
        } else if (status != QStringLiteral("charging")) {
            // v0.3 requires ended_at for pending_settlement/completed. Ignore
            // malformed legacy rows instead of attributing an open interval.
            continue;
        }

        const qint64 startedAtMs = startedAt.toUTC().toMSecsSinceEpoch();
        const qint64 actualStartMs = qMax(periodStartMs, startedAtMs);
        const qint64 actualEndMs = qMin(periodEndMs, endedAtMs);
        if (actualEndMs <= actualStartMs) continue;
        numeratorMsByStation[*pileIterator] += actualEndMs - actualStartMs;
    }

    for (auto iterator = utilizations->begin(); iterator != utilizations->end(); ++iterator) {
        const qint64 denominatorMs = denominatorMsByStation.value(iterator.key());
        const qint64 numeratorMs = numeratorMsByStation.value(iterator.key());
        iterator.value() = denominatorMs > 0
            ? static_cast<double>(numeratorMs) / static_cast<double>(denominatorMs)
            : 0.0;
    }
    return true;
}

bool Database::getStatistics(const QString &range, QJsonObject *statistics,
                             QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!statistics || (range != QStringLiteral("7d") && range != QStringLiteral("30d"))) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("range must be 7d or 30d"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }

    const int days = range == QStringLiteral("7d") ? 7 : 30;
    const QString updatedAt = utcNow();
    const QDateTime statisticsEnd = QDateTime::fromString(updatedAt, Qt::ISODate);
    if (!statisticsEnd.isValid()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("build statistics timestamp failed"));
        return false;
    }
    const QDate endDate = statisticsEnd.toUTC().date();
    const QDate startDate = endDate.addDays(1 - days);
    if (!startDate.isValid() || !endDate.isValid()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("build statistics date range failed"));
        return false;
    }

    // Build a complete calendar series, including zero-value days. The wire
    // response is consumed by trend charts, so the aggregate and daily rows
    // must come from the same UTC calendar window and cannot use a rolling
    // 7*24-hour predicate that would produce a different sum.
    QHash<QString, QJsonObject> dailyByDate;
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT substr(settled_at, 1, 10) AS revenue_date, "
        "COALESCE(SUM(total_amount_cents), 0), COUNT(*), "
        "COALESCE(SUM(energy_wh), 0) FROM charging_orders "
        "WHERE status = 'completed' AND settled_at IS NOT NULL "
        "AND substr(settled_at, 1, 10) BETWEEN :start_date AND :end_date "
        "GROUP BY revenue_date ORDER BY revenue_date"));
    query.bindValue(QStringLiteral(":start_date"), startDate.toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":end_date"), endDate.toString(Qt::ISODate));
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("read revenue statistics failed: %1").arg(queryError(query)));
        return false;
    }
    while (query.next()) {
        const QString date = query.value(0).toString();
        dailyByDate.insert(date, QJsonObject{
            {QStringLiteral("date"), date},
            {QStringLiteral("revenue_cents"), query.value(1).toLongLong()},
            {QStringLiteral("completed_order_count"), query.value(2).toLongLong()},
            {QStringLiteral("energy_wh"), query.value(3).toLongLong()}});
    }

    QJsonArray revenueDaily;
    qint64 revenue = 0;
    qint64 completedOrders = 0;
    qint64 energyWh = 0;
    for (QDate date = startDate; date <= endDate; date = date.addDays(1)) {
        const QString dateText = date.toString(Qt::ISODate);
        const QJsonObject row = dailyByDate.value(dateText, QJsonObject{
            {QStringLiteral("date"), dateText},
            {QStringLiteral("revenue_cents"), 0},
            {QStringLiteral("completed_order_count"), 0},
            {QStringLiteral("energy_wh"), 0}});
        revenueDaily.append(row);
        revenue += row.value(QStringLiteral("revenue_cents")).toInteger();
        completedOrders += row.value(QStringLiteral("completed_order_count")).toInteger();
        energyWh += row.value(QStringLiteral("energy_wh")).toInteger();
    }

    query.prepare(QStringLiteral(
        "SELECT "
        "COALESCE(SUM(status = 'idle'), 0), "
        "COALESCE(SUM(status = 'reserved'), 0), "
        "COALESCE(SUM(status = 'charging'), 0), "
        "COALESCE(SUM(status = 'fault'), 0), "
        "COALESCE(SUM(status = 'offline'), 0), COUNT(*) "
        "FROM charging_piles"));
    if (!query.exec() || !query.next()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("read pile statistics failed: %1").arg(queryError(query)));
        return false;
    }
    const qint64 idle = query.value(0).toLongLong();
    const qint64 reserved = query.value(1).toLongLong();
    const qint64 charging = query.value(2).toLongLong();
    const qint64 fault = query.value(3).toLongLong();
    const qint64 offline = query.value(4).toLongLong();
    const qint64 totalPiles = query.value(5).toLongLong();

    // Station utilization always uses the most recent seven UTC calendar days,
    // independently of the revenue range selected by the caller. Keep the
    // period end tied to the same snapshot used by updated_at so an open
    // charging order is measured consistently for this response.
    const QDate utilizationStartDate = endDate.addDays(-6);
    const QDateTime utilizationStart(utilizationStartDate, QTime(0, 0), Qt::UTC);
    QHash<qint64, double> stationUtilizations;
    if (!getStationUtilizations(utilizationStart, statisticsEnd,
                                &stationUtilizations, error, kind)) {
        return false;
    }
    double averageStationUtilization = 0.0;
    if (!stationUtilizations.isEmpty()) {
        for (auto iterator = stationUtilizations.constBegin();
             iterator != stationUtilizations.constEnd(); ++iterator) {
            averageStationUtilization += iterator.value();
        }
        averageStationUtilization /= static_cast<double>(stationUtilizations.size());
    }

    *statistics = QJsonObject{
        {QStringLiteral("range"), range},
        {QStringLiteral("revenue_cents"), revenue},
        {QStringLiteral("revenue_daily"), revenueDaily},
        {QStringLiteral("completed_order_count"), completedOrders},
        {QStringLiteral("energy_wh"), energyWh},
        {QStringLiteral("pile_idle"), idle},
        {QStringLiteral("pile_reserved"), reserved},
        {QStringLiteral("pile_charging"), charging},
        {QStringLiteral("pile_fault"), fault},
        {QStringLiteral("pile_offline"), offline},
        {QStringLiteral("avg_station_utilization"), averageStationUtilization},
        {QStringLiteral("updated_at"), updatedAt},
        {QStringLiteral("has_data"), completedOrders > 0 || totalPiles > 0}};
    return true;
}

bool Database::listAdminStations(const QString &queryText, QJsonArray *stations,
                                 QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!stations) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("stations output is null"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }

    // Keep the list snapshot precision aligned with getStatistics()'s
    // updated_at/period cutoff (whole UTC seconds), avoiding artificial
    // millisecond differences when the two read endpoints are polled together.
    const QDateTime periodEnd = QDateTime::fromString(utcNow(), Qt::ISODate);
    const QDateTime periodStart(periodEnd.date().addDays(-6), QTime(0, 0), Qt::UTC);
    QHash<qint64, double> stationUtilizations;
    if (!getStationUtilizations(periodStart, periodEnd, &stationUtilizations, error, kind)) {
        return false;
    }

    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT s.id, s.name, s.address, s.latitude, s.longitude, s.status, "
        "COUNT(p.id), "
        "COALESCE(SUM(p.status = 'idle'), 0), "
        "COALESCE(SUM(p.status = 'reserved'), 0), "
        "COALESCE(SUM(p.status = 'charging'), 0), "
        "COALESCE(SUM(p.status = 'fault'), 0), "
        "COALESCE(SUM(p.status = 'offline'), 0) "
        "FROM stations s LEFT JOIN charging_piles p ON p.station_id = s.id "
        "WHERE (:query = '' OR s.name LIKE :pattern OR s.address LIKE :pattern) "
        "GROUP BY s.id ORDER BY s.id"));
    query.bindValue(QStringLiteral(":query"), queryText.trimmed());
    query.bindValue(QStringLiteral(":pattern"), QStringLiteral("%%1%").arg(queryText.trimmed()));
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("list administrator stations failed: %1").arg(queryError(query)));
        return false;
    }
    *stations = QJsonArray();
    while (query.next()) {
        stations->append(QJsonObject{
            {QStringLiteral("id"), query.value(0).toLongLong()},
            {QStringLiteral("name"), query.value(1).toString()},
            {QStringLiteral("address"), query.value(2).toString()},
            {QStringLiteral("latitude"), query.value(3).toDouble()},
            {QStringLiteral("longitude"), query.value(4).toDouble()},
            {QStringLiteral("status"), query.value(5).toString()},
            {QStringLiteral("pile_total"), query.value(6).toLongLong()},
            {QStringLiteral("pile_idle"), query.value(7).toLongLong()},
            {QStringLiteral("pile_reserved"), query.value(8).toLongLong()},
            {QStringLiteral("pile_charging"), query.value(9).toLongLong()},
            {QStringLiteral("pile_fault"), query.value(10).toLongLong()},
            {QStringLiteral("pile_offline"), query.value(11).toLongLong()},
            {QStringLiteral("utilization"), stationUtilizations.value(query.value(0).toLongLong(), 0.0)},
            {QStringLiteral("utilization_range"), QStringLiteral("7d")}});
    }
    return true;
}

bool Database::listAdminPiles(qint64 afterId, qint64 limit, QJsonArray *piles,
                              bool *hasMore, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!piles || !hasMore || afterId < 0 || limit <= 0
        || limit >= std::numeric_limits<qint64>::max()) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("administrator pile page arguments are invalid"));
        return false;
    }
    if (!open(error)) {
        if (kind) *kind = ErrorKind::Database;
        return false;
    }

    // Administrative inventory is intentionally broader than user-facing
    // pile.list: include piles at inactive stations so the management view,
    // statistics and station aggregates all describe the same snapshot. Read
    // one extra row so callers can issue a stable ID-cursor continuation.
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
            "SELECT id, station_id, pile_code, pile_type, power_kw, "
            "unit_price_cents_per_kwh, status, total_charge_count, "
            "total_charge_seconds, restart_count, last_restart_at, "
            "simulated, status_source, status_updated_at "
            "FROM charging_piles WHERE id > :after_id ORDER BY id LIMIT :limit"));
    query.bindValue(QStringLiteral(":after_id"), afterId);
    query.bindValue(QStringLiteral(":limit"), limit + 1);
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("list administrator piles failed: %1").arg(queryError(query)));
        return false;
    }
    *piles = QJsonArray();
    *hasMore = false;
    while (query.next()) {
        if (piles->size() == limit) {
            *hasMore = true;
            break;
        }
        QJsonObject pile;
        if (!readPile(query, &pile, error)) {
            if (kind) *kind = ErrorKind::Database;
            return false;
        }
        piles->append(pile);
    }
    return true;
}

bool Database::createStation(const QString &requestId, qint64 administratorId,
                             const QString &name, const QString &address,
                             double latitude, double longitude, qint64 pileCount,
                             QJsonObject *station, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!station || requestId.isEmpty() || administratorId <= 0
        || name.trimmed().isEmpty() || address.trimmed().isEmpty()
        || !std::isfinite(latitude) || latitude < -90.0 || latitude > 90.0
        || !std::isfinite(longitude) || longitude < -180.0 || longitude > 180.0
        || pileCount <= 0 || pileCount > 1000) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("station fields are invalid"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    if (!checkAdministrator(administratorId, true, error, kind)) return false;
    const QString fingerprint = QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(name.trimmed(), address.trimmed()).arg(latitude, 0, 'g', 16)
        .arg(longitude, 0, 'g', 16).arg(pileCount);
    if (!begin(error, kind)) return false;
    QJsonObject replay; bool found = false;
    if (!loadRequest(requestId, QStringLiteral("admin.station.create"), fingerprint,
                     &replay, &found, error, kind)) { rollback(); return false; }
    if (found) {
        *station = replay.value(QStringLiteral("station")).toObject();
        // Requests persisted by an older server may predate the utilization
        // fields. Keep idempotent replay responses on the current contract.
        station->insert(QStringLiteral("utilization"),
                        station->value(QStringLiteral("utilization")).toDouble());
        station->insert(QStringLiteral("utilization_range"), QStringLiteral("7d"));
        return commit(error, kind);
    }
    const QString timestamp = utcNow();
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "INSERT INTO stations(name, address, latitude, longitude, status, created_at, updated_at) "
        "VALUES (:name, :address, :latitude, :longitude, 'active', :created_at, :updated_at)"));
    query.bindValue(QStringLiteral(":name"), name.trimmed());
    query.bindValue(QStringLiteral(":address"), address.trimmed());
    query.bindValue(QStringLiteral(":latitude"), latitude);
    query.bindValue(QStringLiteral(":longitude"), longitude);
    query.bindValue(QStringLiteral(":created_at"), timestamp);
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    if (!query.exec()) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("create station failed: %1").arg(queryError(query)));
        return false;
    }
    const qint64 stationId = query.lastInsertId().toLongLong();
    query.prepare(QStringLiteral(
        "INSERT INTO charging_piles(station_id, pile_code, pile_type, power_kw, "
        "unit_price_cents_per_kwh, status, status_source, status_updated_at, created_at, updated_at) "
        "VALUES (:station_id, :pile_code, 'fast', 60.0, 120, 'idle', 'business', :created_at, :created_at, :updated_at)"));
    query.bindValue(QStringLiteral(":station_id"), stationId);
    query.bindValue(QStringLiteral(":created_at"), timestamp);
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    for (qint64 index = 1; index <= pileCount; ++index) {
        query.bindValue(QStringLiteral(":pile_code"), QStringLiteral("P-%1-%2")
                        .arg(stationId).arg(index, 3, 10, QChar('0')));
        if (!query.exec()) {
            rollback();
            setFailure(error, kind, ErrorKind::Database,
                       QStringLiteral("create station pile failed: %1").arg(queryError(query)));
            return false;
        }
    }
    *station = QJsonObject{{QStringLiteral("id"), stationId},
                           {QStringLiteral("name"), name.trimmed()},
                           {QStringLiteral("address"), address.trimmed()},
                           {QStringLiteral("latitude"), latitude},
                           {QStringLiteral("longitude"), longitude},
                           {QStringLiteral("status"), QStringLiteral("active")},
                           {QStringLiteral("pile_total"), pileCount},
                           {QStringLiteral("pile_idle"), pileCount},
                           {QStringLiteral("pile_reserved"), 0},
                           {QStringLiteral("pile_charging"), 0},
                           {QStringLiteral("pile_fault"), 0},
                           {QStringLiteral("pile_offline"), 0},
                           {QStringLiteral("utilization"), 0.0},
                           {QStringLiteral("utilization_range"), QStringLiteral("7d")}};
    if (!saveRequest(requestId, QStringLiteral("admin.station.create"), fingerprint,
                     QJsonObject{{QStringLiteral("station"), *station}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
}

bool Database::restartPile(const QString &requestId, qint64 administratorId, qint64 pileId,
                           QJsonObject *pile, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!pile || requestId.isEmpty() || administratorId <= 0 || pileId <= 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("administrator_id and pile_id must be positive"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    if (!checkAdministrator(administratorId, false, error, kind)) return false;
    const QString fingerprint = QStringLiteral("%1:%2").arg(administratorId).arg(pileId);
    if (!begin(error, kind)) return false;
    QJsonObject replay; bool found = false;
    if (!loadRequest(requestId, QStringLiteral("admin.pile.restart"), fingerprint,
                     &replay, &found, error, kind)) { rollback(); return false; }
    if (found) {
        *pile = replay.value(QStringLiteral("pile")).toObject();
        return commit(error, kind);
    }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, "
        "status, total_charge_count, total_charge_seconds, restart_count, last_restart_at, "
        "simulated, status_source, status_updated_at "
        "FROM charging_piles WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), pileId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("pile not found")); return false; }
    const QString oldStatus = query.value(6).toString();
    const QString timestamp = utcNow();
    const bool recoverable = oldStatus == QStringLiteral("fault") || oldStatus == QStringLiteral("offline");
    query.prepare(QStringLiteral(
        "INSERT INTO pile_restart_logs(pile_id, administrator_id, requested_at, result, reason) "
        "VALUES (:pile_id, :administrator_id, :requested_at, :result, :reason)"));
    query.bindValue(QStringLiteral(":pile_id"), pileId);
    query.bindValue(QStringLiteral(":administrator_id"), administratorId);
    query.bindValue(QStringLiteral(":requested_at"), timestamp);
    query.bindValue(QStringLiteral(":result"), recoverable ? QStringLiteral("succeeded") : QStringLiteral("rejected"));
    query.bindValue(QStringLiteral(":reason"), recoverable ? QStringLiteral("restart completed")
                                                            : QStringLiteral("pile is not recoverable"));
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
    if (!recoverable) {
        if (!commit(error, kind)) return false;
        setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("pile is not in a restartable state"));
        return false;
    }
    query.prepare(QStringLiteral(
        "UPDATE charging_piles SET status = 'idle', restart_count = restart_count + 1, "
        "last_restart_at = :updated_at, status_source = 'admin', "
        "status_updated_at = :updated_at, updated_at = :updated_at WHERE id = :id"));
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    query.bindValue(QStringLiteral(":id"), pileId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("update restarted pile failed: %1").arg(queryError(query)));
        return false;
    }
    query.prepare(QStringLiteral(
        "INSERT INTO pile_status_events(pile_id, station_id, from_status, to_status, source, reason, created_at) "
        "SELECT id, station_id, :from_status, 'idle', 'admin', 'restart', :created_at "
        "FROM charging_piles WHERE id = :pile_id"));
    query.bindValue(QStringLiteral(":from_status"), oldStatus);
    query.bindValue(QStringLiteral(":created_at"), timestamp);
    query.bindValue(QStringLiteral(":pile_id"), pileId);
    if (!query.exec()) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("record restart pile event failed: %1").arg(queryError(query)));
        return false;
    }
    if (!bumpPileStationSnapshot(pileId, &query, error, kind)) { rollback(); return false; }
    query.prepare(QStringLiteral(
        "SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, "
        "status, total_charge_count, total_charge_seconds, restart_count, last_restart_at, "
        "simulated, status_source, status_updated_at "
        "FROM charging_piles WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), pileId);
    if (!query.exec() || !query.next() || !readPile(query, pile, error)) {
        rollback();
        setFailure(error, kind, ErrorKind::Database, QStringLiteral("read restarted pile failed"));
        return false;
    }
    if (!saveRequest(requestId, QStringLiteral("admin.pile.restart"), fingerprint,
                     QJsonObject{{QStringLiteral("pile"), *pile}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
}

bool Database::listAdminUsers(const QString &phoneQuery, QJsonArray *users,
                              QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!users) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("users output is null"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT u.id, u.phone, u.nickname, u.avatar_path, u.balance_cents, u.status, "
        "u.created_at, "
        "(SELECT o.status FROM charging_orders o WHERE o.user_id = u.id AND o.status IN "
        "('pending_reservation','reserved','charging','pending_settlement') "
        "ORDER BY o.id DESC LIMIT 1) "
        "FROM users u WHERE (:query = '' OR u.phone LIKE :pattern OR u.nickname LIKE :pattern) "
        "ORDER BY u.id"));
    query.bindValue(QStringLiteral(":query"), phoneQuery.trimmed());
    query.bindValue(QStringLiteral(":pattern"), QStringLiteral("%%1%").arg(phoneQuery.trimmed()));
    if (!query.exec()) {
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("list administrator users failed: %1").arg(queryError(query)));
        return false;
    }
    *users = QJsonArray();
    while (query.next()) {
        users->append(QJsonObject{
            {QStringLiteral("id"), query.value(0).toLongLong()},
            {QStringLiteral("phone"), query.value(1).toString()},
            {QStringLiteral("nickname"), query.value(2).toString()},
            {QStringLiteral("avatar_path"), query.value(3).isNull()
                ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(3).toString())},
            {QStringLiteral("balance_cents"), query.value(4).toLongLong()},
            {QStringLiteral("status"), query.value(5).toString()},
            {QStringLiteral("created_at"), query.value(6).toString()},
            {QStringLiteral("active_order_status"), query.value(7).isNull()
                ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(7).toString())}});
    }
    return true;
}

bool Database::findRequestReplay(const QString &requestId, const QString &operation,
                                 const QString &fingerprint, QJsonObject *response,
                                 bool *found, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!response || !found || requestId.isEmpty() || operation.isEmpty()
        || fingerprint.isEmpty()) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("request replay arguments are invalid"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    return loadRequest(requestId, operation, fingerprint, response, found, error, kind);
}

bool Database::setUserStatus(const QString &requestId, qint64 administratorId, qint64 userId,
                             const QString &status, QJsonObject *user,
                             QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!user || requestId.isEmpty() || administratorId <= 0 || userId <= 0
        || (status != QStringLiteral("active") && status != QStringLiteral("frozen"))) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("administrator_id, user_id and status are invalid"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    if (!checkAdministrator(administratorId, true, error, kind)) return false;
    const QString fingerprint = QStringLiteral("%1:%2:%3").arg(administratorId).arg(userId).arg(status);
    if (!begin(error, kind)) return false;
    QJsonObject replay; bool found = false;
    if (!loadRequest(requestId, QStringLiteral("admin.user.status.set"), fingerprint,
                     &replay, &found, error, kind)) { rollback(); return false; }
    if (found) {
        *user = replay.value(QStringLiteral("user")).toObject();
        return commit(error, kind);
    }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT id FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, queryError(query)); return false; }
    if (!query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("user not found")); return false; }
    const QString timestamp = utcNow();
    query.prepare(QStringLiteral("UPDATE users SET status = :status, updated_at = :updated_at WHERE id = :id"));
    query.bindValue(QStringLiteral(":status"), status);
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("update user status failed: %1").arg(queryError(query))); return false; }
    query.prepare(QStringLiteral("SELECT id, phone, nickname, avatar_path, balance_cents, status FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec() || !readUser(query, user, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read updated user failed")); return false; }
    if (!saveRequest(requestId, QStringLiteral("admin.user.status.set"), fingerprint,
                     QJsonObject{{QStringLiteral("user"), *user}}, error, kind)) {
        rollback();
        return false;
    }
    return commit(error, kind);
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
    query.prepare(QStringLiteral("UPDATE charging_piles SET status = 'reserved', status_source = 'business', status_updated_at = :updated_at, updated_at = :updated_at WHERE id = :id AND status = 'idle'"));
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    query.bindValue(QStringLiteral(":id"), pileId);
    if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("pile was claimed by another request")); return false; }
    query.prepare(QStringLiteral(
        "INSERT INTO pile_status_events(pile_id, station_id, from_status, to_status, source, reason, created_at) "
        "SELECT id, station_id, 'idle', 'reserved', 'business', 'reservation', :created_at "
        "FROM charging_piles WHERE id = :pile_id"));
    query.bindValue(QStringLiteral(":created_at"), timestamp);
    query.bindValue(QStringLiteral(":pile_id"), pileId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("record reservation pile event failed: %1").arg(queryError(query))); return false; }
    if (!bumpPileStationSnapshot(pileId, &query, error, kind)) { rollback(); return false; }
    query.prepare(QStringLiteral(
        "SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, "
        "unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at "
        "FROM charging_orders WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), orderId);
    if (!query.exec() || !query.next() || !readOrder(query, order, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read reservation failed")); return false; }
    query.prepare(QStringLiteral(
        "SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, status, "
        "total_charge_count, total_charge_seconds, restart_count, last_restart_at, "
        "simulated, status_source, status_updated_at FROM charging_piles WHERE id = :id"));
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
        "UPDATE charging_piles SET status = 'idle', status_source = 'business', status_updated_at = :updated_at, updated_at = :updated_at "
        "WHERE id = :pile_id AND status = 'reserved' AND EXISTS ("
        "SELECT 1 FROM charging_orders WHERE id = :order_id AND pile_id = :pile_id "
        "AND user_id = :user_id AND status = 'cancelled')"));
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    query.bindValue(QStringLiteral(":pile_id"), pileId);
    query.bindValue(QStringLiteral(":order_id"), orderId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("reserved pile is not available")); return false; }
    query.prepare(QStringLiteral(
        "INSERT INTO pile_status_events(pile_id, station_id, from_status, to_status, source, reason, created_at) "
        "SELECT id, station_id, 'reserved', 'idle', 'business', 'reservation_cancelled', :created_at "
        "FROM charging_piles WHERE id = :pile_id"));
    query.bindValue(QStringLiteral(":created_at"), timestamp);
    query.bindValue(QStringLiteral(":pile_id"), pileId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("record cancellation pile event failed: %1").arg(queryError(query))); return false; }
    if (!bumpPileStationSnapshot(pileId, &query, error, kind)) { rollback(); return false; }
    query.prepare(QStringLiteral("SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at FROM charging_orders WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), orderId);
    if (!query.exec() || !query.next() || !readOrder(query, order, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read cancelled order failed")); return false; }
    query.prepare(QStringLiteral("SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, status, total_charge_count, total_charge_seconds, restart_count, last_restart_at, simulated, status_source, status_updated_at FROM charging_piles WHERE id = :id"));
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
    query.prepare(QStringLiteral("UPDATE charging_piles SET status = 'charging', status_source = 'business', status_updated_at = :updated_at, updated_at = :updated_at WHERE id = :id AND status = :expected_status"));
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
    query.prepare(QStringLiteral(
        "INSERT INTO pile_status_events(pile_id, station_id, from_status, to_status, source, reason, created_at) "
        "SELECT id, station_id, :from_status, 'charging', 'business', 'charging_started', :created_at "
        "FROM charging_piles WHERE id = :pile_id"));
    query.bindValue(QStringLiteral(":from_status"), expectedPileStatus);
    query.bindValue(QStringLiteral(":created_at"), timestamp);
    query.bindValue(QStringLiteral(":pile_id"), selectedPile);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("record charging pile event failed: %1").arg(queryError(query))); return false; }
    if (!bumpPileStationSnapshot(selectedPile, &query, error, kind)) { rollback(); return false; }
    if (orderId > 0) {
        query.prepare(QStringLiteral("UPDATE charging_orders SET status = 'charging', started_at = :started_at, updated_at = :updated_at WHERE id = :id AND status = 'reserved'"));
        query.bindValue(QStringLiteral(":started_at"), timestamp); query.bindValue(QStringLiteral(":updated_at"), timestamp); query.bindValue(QStringLiteral(":id"), selectedOrder);
        if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("order changed concurrently")); return false; }
    }
    query.prepare(QStringLiteral("SELECT id, order_no, user_id, pile_id, status, reserved_at, started_at, ended_at, energy_wh, unit_price_cents_per_kwh, service_fee_cents, total_amount_cents, settled_at, created_at, updated_at FROM charging_orders WHERE id = :id")); query.bindValue(QStringLiteral(":id"), selectedOrder);
    if (!query.exec() || !query.next() || !readOrder(query, order, error)) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read started order failed")); return false; }
    query.prepare(QStringLiteral("SELECT id, station_id, pile_code, pile_type, power_kw, unit_price_cents_per_kwh, status, total_charge_count, total_charge_seconds, restart_count, last_restart_at, simulated, status_source, status_updated_at FROM charging_piles WHERE id = :id")); query.bindValue(QStringLiteral(":id"), selectedPile);
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
    pileQuery.prepare(QStringLiteral("UPDATE charging_piles SET status='idle', status_source='business', status_updated_at=:updated_at, updated_at=:updated_at WHERE id=:id AND status='charging'"));
    pileQuery.bindValue(QStringLiteral(":updated_at"), finish);
    pileQuery.bindValue(QStringLiteral(":id"), pileId);
    if (!pileQuery.exec() || pileQuery.numRowsAffected() != 1) {
        rollback();
        setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("charging pile is not active"));
        return false;
    }
    pileQuery.prepare(QStringLiteral(
        "INSERT INTO pile_status_events(pile_id, station_id, from_status, to_status, source, reason, created_at) "
        "SELECT id, station_id, 'charging', 'idle', 'business', 'charging_stopped', :created_at "
        "FROM charging_piles WHERE id = :pile_id"));
    pileQuery.bindValue(QStringLiteral(":created_at"), finish);
    pileQuery.bindValue(QStringLiteral(":pile_id"), pileId);
    if (!pileQuery.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("record stop pile event failed: %1").arg(queryError(pileQuery))); return false; }
    if (!bumpPileStationSnapshot(pileId, &pileQuery, error, kind)) { rollback(); return false; }
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

namespace {

QJsonValue nullableText(const QVariant &value)
{
    return value.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(value.toString());
}

QString mapAuditStatus(const QString &dataSource, const QJsonObject &warning)
{
    if (dataSource == QStringLiteral("server_mock")) return QStringLiteral("mock");
    if (dataSource == QStringLiteral("tencent_stale") || !warning.isEmpty())
        return QStringLiteral("degraded");
    return QStringLiteral("success");
}

bool isSafeMapOnly(const QJsonValue &value)
{
    static const QSet<QString> forbidden = {
        QStringLiteral("status"), QStringLiteral("pile_total"),
        QStringLiteral("pile_idle"), QStringLiteral("pile_reserved"),
        QStringLiteral("pile_charging"), QStringLiteral("pile_fault"),
        QStringLiteral("pile_offline"), QStringLiteral("pile_snapshot_version"),
        QStringLiteral("price_cents"), QStringLiteral("power_kw")};
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
            if (forbidden.contains(iterator.key()) || !isSafeMapOnly(iterator.value())) return false;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            if (!isSafeMapOnly(item)) return false;
        }
    }
    return true;
}

} // namespace

bool Database::importMapStations(const QString &requestId, qint64 userId,
                                 const QJsonObject &normalizedQuery,
                                 const QJsonObject &resolvedOrigin,
                                 const QVector<MapPoi> &pois,
                                 const QString &dataSource,
                                 const QJsonObject &warning,
                                 bool hasMore, const QString &nextPageToken,
                                 QJsonObject *response, QString *error,
                                 ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!response || requestId.isEmpty() || userId <= 0 || resolvedOrigin.isEmpty()
        || (dataSource != QStringLiteral("tencent_live")
            && dataSource != QStringLiteral("tencent_cache")
            && dataSource != QStringLiteral("tencent_stale")
            && dataSource != QStringLiteral("server_mock"))) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("map import arguments are invalid"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    if (!begin(error, kind)) return false;
    const QString fingerprint = jsonFingerprint(normalizedQuery);
    QJsonObject replay;
    bool found = false;
    if (!loadRequest(requestId, QStringLiteral("map.station.search"), fingerprint,
                     &replay, &found, error, kind)) {
        rollback();
        return false;
    }
    if (found) {
        *response = replay;
        return commit(error, kind);
    }
    const QString provider = normalizedQuery.value(QStringLiteral("provider"))
        .toString(QStringLiteral("tencent"));
    const QString timestamp = utcNow();
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "INSERT INTO map_request_logs(request_id, operation, user_id, normalized_query_json, "
        "cache_key, result_status, data_source, warning_code, created_at, completed_at) "
        "VALUES (:request_id, 'map.station.search', :user_id, :query, :cache_key, :result_status, "
        ":data_source, :warning_code, :created_at, :completed_at)"));
    query.bindValue(QStringLiteral(":request_id"), requestId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":query"), QString::fromUtf8(
        QJsonDocument(normalizedQuery).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":cache_key"), normalizedQuery.value(QStringLiteral("cache_key")).toString());
    query.bindValue(QStringLiteral(":result_status"), mapAuditStatus(dataSource, warning));
    query.bindValue(QStringLiteral(":data_source"), dataSource);
    query.bindValue(QStringLiteral(":warning_code"), warning.value(QStringLiteral("code")).toInt());
    query.bindValue(QStringLiteral(":created_at"), timestamp);
    query.bindValue(QStringLiteral(":completed_at"), timestamp);
    if (!query.exec()) {
        rollback();
        setFailure(error, kind, ErrorKind::Database,
                   QStringLiteral("write map request log failed: %1").arg(queryError(query)));
        return false;
    }
    const qint64 logId = query.lastInsertId().toLongLong();

    QHash<QString, qint64> stationIds;
    for (const MapPoi &poi : pois) {
        if (poi.providerPoiId.trimmed().isEmpty() || poi.name.trimmed().isEmpty()
            || poi.address.trimmed().isEmpty() || !std::isfinite(poi.latitude)
            || !std::isfinite(poi.longitude) || poi.latitude < -90.0 || poi.latitude > 90.0
            || poi.longitude < -180.0 || poi.longitude > 180.0 || poi.distanceMeters < 0) {
            rollback();
            setFailure(error, kind, ErrorKind::InvalidArgument,
                       QStringLiteral("map provider returned an invalid POI"));
            return false;
        }
        const QString poiKey = provider + QLatin1Char('\0') + poi.providerPoiId;
        if (stationIds.contains(poiKey)) continue;
        query.prepare(QStringLiteral(
            "SELECT id FROM stations WHERE provider = :provider AND provider_poi_id = :poi_id"));
        query.bindValue(QStringLiteral(":provider"), provider);
        query.bindValue(QStringLiteral(":poi_id"), poi.providerPoiId);
        if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("lookup map station failed: %1").arg(queryError(query))); return false; }
        qint64 stationId = 0;
        const bool exists = query.next();
        if (exists) stationId = query.value(0).toLongLong();
        const QByteArray content = QCryptographicHash::hash(
            (poi.name + QLatin1Char('\0') + poi.address).toUtf8(), QCryptographicHash::Sha256).toHex();
        if (exists) {
            query.prepare(QStringLiteral(
                "UPDATE stations SET name = :name, address = :address, latitude = :latitude, "
                "longitude = :longitude, map_synced_at = :synced_at, map_content_hash = :hash, "
                "updated_at = :updated_at WHERE id = :id"));
            query.bindValue(QStringLiteral(":id"), stationId);
        } else {
            query.prepare(QStringLiteral(
                "INSERT INTO stations(name, address, latitude, longitude, status, created_at, "
                "updated_at, provider, provider_poi_id, map_synced_at, map_content_hash) "
                "VALUES (:name, :address, :latitude, :longitude, 'active', :created_at, "
                ":updated_at, :provider, :poi_id, :synced_at, :hash)"));
        }
        query.bindValue(QStringLiteral(":name"), poi.name.normalized(QString::NormalizationForm_C).trimmed());
        query.bindValue(QStringLiteral(":address"), poi.address.normalized(QString::NormalizationForm_C).trimmed());
        query.bindValue(QStringLiteral(":latitude"), poi.latitude);
        query.bindValue(QStringLiteral(":longitude"), poi.longitude);
        query.bindValue(QStringLiteral(":synced_at"), timestamp);
        query.bindValue(QStringLiteral(":hash"), QString::fromLatin1(content));
        query.bindValue(QStringLiteral(":updated_at"), timestamp);
        if (!exists) {
            query.bindValue(QStringLiteral(":created_at"), timestamp);
            query.bindValue(QStringLiteral(":provider"), provider);
            query.bindValue(QStringLiteral(":poi_id"), poi.providerPoiId);
        }
        if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("upsert map station failed: %1").arg(queryError(query))); return false; }
        if (!exists) stationId = query.lastInsertId().toLongLong();
        stationIds.insert(poiKey, stationId);

        query.prepare(QStringLiteral("SELECT COUNT(*) FROM charging_piles WHERE station_id = :station_id"));
        query.bindValue(QStringLiteral(":station_id"), stationId);
        if (!query.exec() || !query.next()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("inspect imported piles failed")); return false; }
        if (query.value(0).toLongLong() == 0) {
            const QString generationSeed = qEnvironmentVariable(
                "EV_PILE_SIMULATION_SEED", QStringLiteral("demo-2026-09"));
            const QVector<GeneratedPile> generated = PileGenerator::generate(
                generationSeed, provider, poi.providerPoiId, true);
            int sequence = 1;
            for (const GeneratedPile &pile : generated) {
                query.prepare(QStringLiteral(
                    "INSERT INTO charging_piles(station_id, pile_code, pile_type, power_kw, "
                    "unit_price_cents_per_kwh, status, simulated, status_source, status_updated_at, "
                    "created_at, updated_at) VALUES (:station_id, :code, :type, :power, :price, "
                    ":status, 1, 'seed', :status_updated_at, :created_at, :updated_at)"));
                query.bindValue(QStringLiteral(":station_id"), stationId);
                query.bindValue(QStringLiteral(":code"), QStringLiteral("M%1-%2").arg(stationId).arg(sequence++, 2, 10, QLatin1Char('0')));
                query.bindValue(QStringLiteral(":type"), pile.type);
                query.bindValue(QStringLiteral(":power"), pile.powerKw);
                query.bindValue(QStringLiteral(":price"), pile.priceFenPerKwh);
                query.bindValue(QStringLiteral(":status"), pile.status);
                query.bindValue(QStringLiteral(":status_updated_at"), timestamp);
                query.bindValue(QStringLiteral(":created_at"), timestamp);
                query.bindValue(QStringLiteral(":updated_at"), timestamp);
                if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("generate imported piles failed: %1").arg(queryError(query))); return false; }
            }
            query.prepare(QStringLiteral(
                "INSERT INTO simulation_state(station_id, seed_id, snapshot_version) "
                "VALUES (:station_id, :seed_id, 0)"));
            query.bindValue(QStringLiteral(":station_id"), stationId);
            query.bindValue(QStringLiteral(":seed_id"), generationSeed);
            if (!query.exec()) {
                rollback();
                setFailure(error, kind, ErrorKind::Database,
                           QStringLiteral("initialize simulation snapshot failed: %1")
                               .arg(queryError(query)));
                return false;
            }
        }
    }

    QJsonArray stations;
    for (const MapPoi &poi : pois) {
        const qint64 stationId = stationIds.value(provider + QLatin1Char('\0') + poi.providerPoiId);
        if (stationId <= 0) continue;
        query.prepare(QStringLiteral(
            "SELECT s.id, s.name, s.address, s.latitude, s.longitude, s.status, s.provider, "
            "s.provider_poi_id, s.map_synced_at, COUNT(p.id), "
            "COALESCE(SUM(p.status = 'idle'), 0), COALESCE(SUM(p.status = 'reserved'), 0), "
            "COALESCE(SUM(p.status = 'charging'), 0), COALESCE(SUM(p.status = 'fault'), 0), "
            "COALESCE(SUM(p.status = 'offline'), 0), "
            "COALESCE((SELECT snapshot_version FROM simulation_state ss WHERE ss.station_id = s.id), 0) "
            "FROM stations s LEFT JOIN charging_piles p ON p.station_id = s.id "
            "WHERE s.id = :id GROUP BY s.id"));
        query.bindValue(QStringLiteral(":id"), stationId);
        if (!query.exec() || !query.next()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("aggregate imported station failed")); return false; }
        stations.append(QJsonObject{
            {QStringLiteral("id"), query.value(0).toLongLong()},
            {QStringLiteral("provider"), query.value(6).toString()},
            {QStringLiteral("provider_poi_id"), query.value(7).toString()},
            {QStringLiteral("name"), query.value(1).toString()},
            {QStringLiteral("address"), query.value(2).toString()},
            {QStringLiteral("latitude"), query.value(3).toDouble()},
            {QStringLiteral("longitude"), query.value(4).toDouble()},
            {QStringLiteral("distance_meters"), poi.distanceMeters},
            {QStringLiteral("status"), query.value(5).toString()},
            {QStringLiteral("pile_total"), query.value(9).toLongLong()},
            {QStringLiteral("pile_idle"), query.value(10).toLongLong()},
            {QStringLiteral("pile_reserved"), query.value(11).toLongLong()},
            {QStringLiteral("pile_charging"), query.value(12).toLongLong()},
            {QStringLiteral("pile_fault"), query.value(13).toLongLong()},
            {QStringLiteral("pile_offline"), query.value(14).toLongLong()},
            {QStringLiteral("map_synced_at"), nullableText(query.value(8))},
            {QStringLiteral("pile_snapshot_version"), query.value(15).toLongLong()}});
    }
    const QDateTime fetchedAt = QDateTime::fromString(timestamp, Qt::ISODate);
    const QString expires = fetchedAt.addSecs(86400).toString(Qt::ISODate);
    QJsonObject mapPayload{{QStringLiteral("provider"), provider},
                           {QStringLiteral("data_source"), dataSource},
                           {QStringLiteral("resolved_origin"), resolvedOrigin},
                           {QStringLiteral("stations"), stations},
                           {QStringLiteral("has_more"), hasMore},
                           {QStringLiteral("next_page_token"), hasMore ? QJsonValue(nextPageToken) : QJsonValue(QJsonValue::Null)},
                           {QStringLiteral("fetched_at"), timestamp},
                           {QStringLiteral("expires_at"), expires},
                           {QStringLiteral("map_request_log_id"), logId},
                           {QStringLiteral("warning"), warning.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(warning)}};
    *response = mapPayload;
    if (!saveRequest(requestId, QStringLiteral("map.station.search"), fingerprint,
                     mapPayload, error, kind)) { rollback(); return false; }
    return commit(error, kind);
}

bool Database::readMapCache(const QString &cacheKey, QJsonObject *mapOnlyPayload,
                            bool *fresh, bool *stale, QString *error,
                            ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!mapOnlyPayload || !fresh || !stale || cacheKey.isEmpty()) {
        setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("map cache arguments are invalid"));
        return false;
    }
    *mapOnlyPayload = QJsonObject(); *fresh = false; *stale = false;
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT payload_json, expires_at, stale_until FROM map_cache_entries WHERE cache_key = :cache_key"));
    query.bindValue(QStringLiteral(":cache_key"), cacheKey);
    if (!query.exec()) { setFailure(error, kind, ErrorKind::Database, QStringLiteral("read map cache failed: %1").arg(queryError(query))); return false; }
    if (!query.next()) return true;
    QJsonParseError parseError;
    const QJsonDocument payload = QJsonDocument::fromJson(query.value(0).toString().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !payload.isObject()
        || !isSafeMapOnly(payload.object())) {
        setFailure(error, kind, ErrorKind::Database, QStringLiteral("map cache contains invalid or business state data"));
        return false;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime expires = QDateTime::fromString(query.value(1).toString(), Qt::ISODate);
    const QDateTime staleUntil = QDateTime::fromString(query.value(2).toString(), Qt::ISODate);
    if (now <= expires) { *fresh = true; *mapOnlyPayload = payload.object(); }
    else if (now <= staleUntil) { *stale = true; *mapOnlyPayload = payload.object(); }
    return true;
}

bool Database::writeMapCache(const QString &cacheKey, const QString &operation,
                             const QJsonObject &mapOnlyPayload,
                             const QString &providerCursor,
                             const QDateTime &fetchedAt, const QDateTime &expiresAt,
                             const QDateTime &staleUntil, QString *error,
                             ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (cacheKey.isEmpty() || cacheKey.size() > 512
        || (operation != QStringLiteral("map.station.search")
            && operation != QStringLiteral("map.route.plan"))
        || mapOnlyPayload.isEmpty() || !isSafeMapOnly(mapOnlyPayload)
        || !fetchedAt.isValid() || !expiresAt.isValid() || !staleUntil.isValid()
        || staleUntil < expiresAt) {
        setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("map cache entry is invalid"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "INSERT INTO map_cache_entries(cache_key, operation, payload_json, provider_cursor, "
        "fetched_at, expires_at, stale_until, updated_at) VALUES (:key, :operation, :payload, "
        ":cursor, :fetched, :expires, :stale, :updated) "
        "ON CONFLICT(cache_key) DO UPDATE SET operation=excluded.operation, "
        "payload_json=excluded.payload_json, provider_cursor=excluded.provider_cursor, "
        "fetched_at=excluded.fetched_at, expires_at=excluded.expires_at, "
        "stale_until=excluded.stale_until, updated_at=excluded.updated_at"));
    query.bindValue(QStringLiteral(":key"), cacheKey);
    query.bindValue(QStringLiteral(":operation"), operation);
    query.bindValue(QStringLiteral(":payload"), QString::fromUtf8(QJsonDocument(mapOnlyPayload).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":cursor"), providerCursor);
    query.bindValue(QStringLiteral(":fetched"), fetchedAt.toUTC().toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":expires"), expiresAt.toUTC().toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":stale"), staleUntil.toUTC().toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":updated"), utcNow());
    if (!query.exec()) { setFailure(error, kind, ErrorKind::Database, QStringLiteral("write map cache failed: %1").arg(queryError(query))); return false; }
    return true;
}

bool Database::getStationMapDestination(qint64 stationId, QJsonObject *destination,
                                        QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!destination || stationId <= 0) { setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("station_id must be positive")); return false; }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("SELECT id, name, address, latitude, longitude, status, provider, provider_poi_id FROM stations WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), stationId);
    if (!query.exec()) { setFailure(error, kind, ErrorKind::Database, QStringLiteral("read route destination failed: %1").arg(queryError(query))); return false; }
    if (!query.next()) { setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("station not found")); return false; }
    *destination = QJsonObject{{QStringLiteral("id"), query.value(0).toLongLong()},
                               {QStringLiteral("name"), query.value(1).toString()},
                               {QStringLiteral("address"), query.value(2).toString()},
                               {QStringLiteral("latitude"), query.value(3).toDouble()},
                               {QStringLiteral("longitude"), query.value(4).toDouble()},
                               {QStringLiteral("status"), query.value(5).toString()},
                               {QStringLiteral("provider"), query.value(6).toString()},
                               {QStringLiteral("provider_poi_id"), query.value(7).toString()}};
    return true;
}

bool Database::recordMapRequest(const QString &requestId, const QString &operation,
                                qint64 userId, const QJsonObject &normalizedQuery,
                                const QString &cacheKey, const QString &resultStatus,
                                const QString &dataSource, int errorCode, int warningCode,
                                qint64 *logId, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!logId || requestId.isEmpty() || (operation != QStringLiteral("map.station.search")
        && operation != QStringLiteral("map.route.plan")) || normalizedQuery.isEmpty()
        || (resultStatus != QStringLiteral("success") && resultStatus != QStringLiteral("error")
            && resultStatus != QStringLiteral("degraded") && resultStatus != QStringLiteral("mock"))) {
        setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("map audit arguments are invalid"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "INSERT INTO map_request_logs(request_id, operation, user_id, normalized_query_json, cache_key, "
        "result_status, data_source, error_code, warning_code, created_at, completed_at) "
        "VALUES (:request_id, :operation, :user_id, :query, :cache_key, :status, :source, :error, :warning, :created_at, :completed_at)"));
    query.bindValue(QStringLiteral(":request_id"), requestId);
    query.bindValue(QStringLiteral(":operation"), operation);
    if (userId > 0) query.bindValue(QStringLiteral(":user_id"), userId); else query.bindValue(QStringLiteral(":user_id"), QVariant());
    query.bindValue(QStringLiteral(":query"), QString::fromUtf8(QJsonDocument(normalizedQuery).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":cache_key"), cacheKey);
    query.bindValue(QStringLiteral(":status"), resultStatus);
    if (dataSource.isEmpty()) query.bindValue(QStringLiteral(":source"), QVariant()); else query.bindValue(QStringLiteral(":source"), dataSource);
    if (errorCode) query.bindValue(QStringLiteral(":error"), errorCode); else query.bindValue(QStringLiteral(":error"), QVariant());
    if (warningCode) query.bindValue(QStringLiteral(":warning"), warningCode); else query.bindValue(QStringLiteral(":warning"), QVariant());
    query.bindValue(QStringLiteral(":created_at"), utcNow());
    query.bindValue(QStringLiteral(":completed_at"), utcNow());
    if (!query.exec()) { setFailure(error, kind, ErrorKind::Database, QStringLiteral("write map audit failed: %1").arg(queryError(query))); return false; }
    *logId = query.lastInsertId().toLongLong();
    return true;
}

bool Database::recordMapUpstreamCall(qint64 mapRequestLogId, const QString &callType,
                                     const QString &resultStatus, int httpStatus,
                                     qint64 latencyMs, int errorCode, QString *error,
                                     ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (mapRequestLogId <= 0 || (callType != QStringLiteral("geocode")
        && callType != QStringLiteral("poi_search") && callType != QStringLiteral("poi_detail")
        && callType != QStringLiteral("driving") && callType != QStringLiteral("walking"))
        || (resultStatus != QStringLiteral("success") && resultStatus != QStringLiteral("error"))
        || latencyMs < 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("upstream audit arguments are invalid"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral("INSERT INTO map_upstream_call_logs(map_request_log_id, call_type, result_status, http_status, latency_ms, error_code, created_at) VALUES (:log_id, :type, :status, :http, :latency, :error, :created_at)"));
    query.bindValue(QStringLiteral(":log_id"), mapRequestLogId);
    query.bindValue(QStringLiteral(":type"), callType);
    query.bindValue(QStringLiteral(":status"), resultStatus);
    if (httpStatus > 0) query.bindValue(QStringLiteral(":http"), httpStatus); else query.bindValue(QStringLiteral(":http"), QVariant());
    query.bindValue(QStringLiteral(":latency"), latencyMs);
    if (errorCode) query.bindValue(QStringLiteral(":error"), errorCode); else query.bindValue(QStringLiteral(":error"), QVariant());
    query.bindValue(QStringLiteral(":created_at"), utcNow());
    if (!query.exec()) { setFailure(error, kind, ErrorKind::Database, QStringLiteral("write upstream audit failed: %1").arg(queryError(query))); return false; }
    return true;
}

bool Database::listMapAudit(const QString &operation, const QString &resultStatus,
                            const QString &beforeCreatedAt, qint64 beforeId,
                            qint64 limit, QJsonArray *records,
                            bool *hasMore, QString *error, ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!records || !hasMore || beforeId < 0 || limit <= 0 || limit > 100
        || (!operation.isEmpty() && operation != QStringLiteral("map.station.search")
            && operation != QStringLiteral("map.route.plan"))
        || (!resultStatus.isEmpty() && resultStatus != QStringLiteral("success")
            && resultStatus != QStringLiteral("error") && resultStatus != QStringLiteral("degraded")
            && resultStatus != QStringLiteral("mock"))) {
        setFailure(error, kind, ErrorKind::InvalidArgument, QStringLiteral("map audit page arguments are invalid"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    QSqlQuery query(connection_);
    QString sql = QStringLiteral(
        "SELECT id, request_id, operation, user_id, result_status, data_source, error_code, warning_code, created_at, completed_at "
        "FROM map_request_logs WHERE 1=1 ");
    if (beforeId > 0)
        sql += QStringLiteral("AND (created_at < :before_created_at OR (created_at = :before_created_at AND id < :before_id)) ");
    if (!operation.isEmpty()) sql += QStringLiteral("AND operation = :operation ");
    if (!resultStatus.isEmpty()) sql += QStringLiteral("AND result_status = :result_status ");
    sql += QStringLiteral("ORDER BY created_at DESC, id DESC LIMIT :limit");
    query.prepare(sql);
    if (beforeId > 0) {
        query.bindValue(QStringLiteral(":before_created_at"), beforeCreatedAt);
        query.bindValue(QStringLiteral(":before_id"), beforeId);
    }
    if (!operation.isEmpty()) query.bindValue(QStringLiteral(":operation"), operation);
    if (!resultStatus.isEmpty())
        query.bindValue(QStringLiteral(":result_status"), resultStatus);
    query.bindValue(QStringLiteral(":limit"), limit + 1);
    if (!query.exec()) { setFailure(error, kind, ErrorKind::Database, QStringLiteral("list map audit failed: %1").arg(queryError(query))); return false; }
    *records = QJsonArray(); *hasMore = false;
    while (query.next()) {
        if (records->size() == limit) { *hasMore = true; break; }
        QJsonObject row{{QStringLiteral("id"), query.value(0).toLongLong()},
                        {QStringLiteral("request_id"), query.value(1).toString()},
                        {QStringLiteral("operation"), query.value(2).toString()},
                        {QStringLiteral("user_id"), query.value(3).isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(3).toLongLong())},
                        {QStringLiteral("result_status"), query.value(4).toString()},
                        {QStringLiteral("data_source"), nullableText(query.value(5))},
                        {QStringLiteral("error_code"), query.value(6).isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(6).toInt())},
                        {QStringLiteral("warning_code"), query.value(7).isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(7).toInt())},
                        {QStringLiteral("created_at"), query.value(8).toString()},
                        {QStringLiteral("completed_at"), nullableText(query.value(9))}};
        records->append(row);
    }
    return true;
}

bool Database::applySimulationProposal(const QString &simulatorId, const QString &seedId,
                                       qint64 tickId,
                                       const QHash<qint64, qint64> &expectedVersions,
                                       const QVector<SimulationChange> &changes,
                                       QJsonObject *result, QString *error,
                                       ErrorKind *kind)
{
    if (kind) *kind = ErrorKind::None;
    if (!result || simulatorId.trimmed().isEmpty() || seedId.trimmed().isEmpty()
        || tickId < 0) {
        setFailure(error, kind, ErrorKind::InvalidArgument,
                   QStringLiteral("simulation proposal is invalid"));
        return false;
    }
    if (!open(error)) { if (kind) *kind = ErrorKind::Database; return false; }
    if (!begin(error, kind)) return false;

    QJsonArray expected;
    QList<qint64> expectedStationIds = expectedVersions.keys();
    std::sort(expectedStationIds.begin(), expectedStationIds.end());
    for (const qint64 stationId : expectedStationIds) {
        expected.append(QJsonObject{{QStringLiteral("station_id"), stationId},
                                    {QStringLiteral("version"), expectedVersions.value(stationId)}});
    }
    QJsonArray requestedChanges;
    for (const SimulationChange &change : changes) {
        requestedChanges.append(QJsonObject{{QStringLiteral("pile_id"), change.pileId},
                                            {QStringLiteral("from"), change.fromStatus},
                                            {QStringLiteral("to"), change.toStatus},
                                            {QStringLiteral("reason"), change.reason}});
    }
    const QJsonObject fingerprintObject{{QStringLiteral("simulator_id"), simulatorId},
                                        {QStringLiteral("seed_id"), seedId},
                                        {QStringLiteral("tick_id"), tickId},
                                        {QStringLiteral("expected_versions"), expected},
                                        {QStringLiteral("changes"), requestedChanges}};
    const QString fingerprint = jsonFingerprint(fingerprintObject);
    QSqlQuery query(connection_);
    query.prepare(QStringLiteral(
        "SELECT fingerprint, result_status, response_json FROM simulation_tick_records "
        "WHERE simulator_id = :simulator_id AND tick_id = :tick_id"));
    query.bindValue(QStringLiteral(":simulator_id"), simulatorId);
    query.bindValue(QStringLiteral(":tick_id"), tickId);
    if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("read simulation tick failed: %1").arg(queryError(query))); return false; }
    if (query.next()) {
        if (query.value(0).toString() != fingerprint) {
            rollback();
            setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("tick id was already used with different proposal"));
            return false;
        }
        const QString replayStatus = query.value(1).toString();
        QJsonParseError parseError;
        const QJsonDocument replay = QJsonDocument::fromJson(query.value(2).toString().toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !replay.isObject()) {
            rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("simulation tick result is invalid")); return false;
        }
        *result = replay.object();
        if (!commit(error, kind)) return false;
        if (replayStatus == QStringLiteral("accepted")) return true;
        setFailure(error, kind, ErrorKind::Conflict,
                   replayStatus == QStringLiteral("conflict")
                       ? QStringLiteral("stale or conflicting simulation proposal")
                       : QStringLiteral("simulation proposal was rejected"));
        return false;
    }

    auto recordOutcome = [&](const QString &status, const QJsonObject &outcome,
                             ErrorKind outcomeKind, const QString &message) -> bool {
        QSqlQuery record(connection_);
        record.prepare(QStringLiteral(
            "INSERT INTO simulation_tick_records(simulator_id, tick_id, fingerprint, result_status, response_json, created_at) "
            "VALUES (:simulator_id, :tick_id, :fingerprint, :status, :response_json, :created_at)"));
        record.bindValue(QStringLiteral(":simulator_id"), simulatorId);
        record.bindValue(QStringLiteral(":tick_id"), tickId);
        record.bindValue(QStringLiteral(":fingerprint"), fingerprint);
        record.bindValue(QStringLiteral(":status"), status);
        record.bindValue(QStringLiteral(":response_json"), QString::fromUtf8(QJsonDocument(outcome).toJson(QJsonDocument::Compact)));
        record.bindValue(QStringLiteral(":created_at"), utcNow());
        if (!record.exec()) {
            rollback();
            setFailure(error, kind, ErrorKind::Database, QStringLiteral("record simulation outcome failed: %1").arg(queryError(record)));
            return false;
        }
        *result = outcome;
        if (!commit(error, kind)) return false;
        if (outcomeKind != ErrorKind::None)
            setFailure(error, kind, outcomeKind, message);
        return outcomeKind == ErrorKind::None;
    };

    QHash<qint64, qint64> validatedVersions;
    for (auto iterator = expectedVersions.constBegin();
         iterator != expectedVersions.constEnd(); ++iterator) {
        query.prepare(QStringLiteral(
            "SELECT s.status, ss.seed_id, COALESCE(ss.snapshot_version, 0) "
            "FROM stations s LEFT JOIN simulation_state ss ON ss.station_id = s.id "
            "WHERE s.id = :station_id"));
        query.bindValue(QStringLiteral(":station_id"), iterator.key());
        if (!query.exec()) {
            rollback();
            setFailure(error, kind, ErrorKind::Database,
                       QStringLiteral("read simulation snapshot failed: %1").arg(queryError(query)));
            return false;
        }
        if (!query.next() || query.value(0).toString() != QStringLiteral("active")) {
            return recordOutcome(QStringLiteral("rejected"),
                                 QJsonObject{{QStringLiteral("accepted"), false},
                                             {QStringLiteral("reason"), QStringLiteral("station not found or inactive")},
                                             {QStringLiteral("station_id"), iterator.key()}},
                                 ErrorKind::NotFound, QStringLiteral("station not found or inactive"));
        }
        const QString currentSeed = query.value(1).isNull() ? seedId : query.value(1).toString();
        const qint64 currentVersion = query.value(2).toLongLong();
        if (currentSeed != seedId || iterator.value() != currentVersion) {
            return recordOutcome(QStringLiteral("conflict"),
                                 QJsonObject{{QStringLiteral("accepted"), false},
                                             {QStringLiteral("reason"), QStringLiteral("stale station snapshot")},
                                             {QStringLiteral("station_id"), iterator.key()},
                                             {QStringLiteral("current_version"), currentVersion}},
                                 ErrorKind::Conflict, QStringLiteral("stale station snapshot"));
        }
        validatedVersions.insert(iterator.key(), currentVersion);
    }

    if (changes.isEmpty()) {
        QJsonObject versions;
        for (auto iterator = validatedVersions.constBegin();
             iterator != validatedVersions.constEnd(); ++iterator)
            versions.insert(QString::number(iterator.key()), iterator.value());
        return recordOutcome(QStringLiteral("accepted"),
                             QJsonObject{{QStringLiteral("accepted"), true},
                                         {QStringLiteral("simulator_id"), simulatorId},
                                         {QStringLiteral("tick_id"), tickId},
                                         {QStringLiteral("changes"), QJsonArray()},
                                         {QStringLiteral("snapshot_versions"), versions}},
                             ErrorKind::None, {});
    }

    QHash<qint64, qint64> stationVersions;
    QHash<qint64, QHash<qint64, QString>> proposed;
    QHash<qint64, QString> originalStatuses;
    QHash<qint64, QHash<QString, int>> stateCounts;
    const QString timestamp = utcNow();
    for (const SimulationChange &change : changes) {
        if (change.pileId <= 0 || (change.fromStatus != QStringLiteral("idle")
            && change.fromStatus != QStringLiteral("fault")
            && change.fromStatus != QStringLiteral("offline"))
            || (change.toStatus != QStringLiteral("idle")
                && change.toStatus != QStringLiteral("fault")
                && change.toStatus != QStringLiteral("offline"))
            || change.fromStatus == change.toStatus) {
            return recordOutcome(QStringLiteral("rejected"),
                                 QJsonObject{{QStringLiteral("accepted"), false},
                                             {QStringLiteral("reason"), QStringLiteral("invalid simulation transition")}},
                                 ErrorKind::Conflict, QStringLiteral("invalid simulation transition"));
        }
        query.prepare(QStringLiteral(
            "SELECT p.station_id, p.status, p.simulated, "
            "EXISTS(SELECT 1 FROM charging_orders o WHERE o.pile_id = p.id AND o.status IN "
            "('pending_reservation','reserved','charging','pending_settlement')) "
            "FROM charging_piles p WHERE p.id = :pile_id"));
        query.bindValue(QStringLiteral(":pile_id"), change.pileId);
        if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("inspect simulated pile failed: %1").arg(queryError(query))); return false; }
        if (!query.next()) {
            return recordOutcome(QStringLiteral("rejected"),
                                 QJsonObject{{QStringLiteral("accepted"), false},
                                             {QStringLiteral("reason"), QStringLiteral("pile not found")}},
                                 ErrorKind::NotFound, QStringLiteral("pile not found"));
        }
        const qint64 stationId = query.value(0).toLongLong();
        const QString current = query.value(1).toString();
        if (query.value(2).toInt() == 0 || query.value(3).toInt() != 0
            || current != change.fromStatus) {
            return recordOutcome(QStringLiteral("rejected"),
                                 QJsonObject{{QStringLiteral("accepted"), false},
                                             {QStringLiteral("reason"), QStringLiteral("pile state is not eligible")}},
                                 ErrorKind::Conflict, QStringLiteral("pile state is not eligible"));
        }
        if (proposed[stationId].contains(change.pileId)) {
            return recordOutcome(QStringLiteral("rejected"),
                                 QJsonObject{{QStringLiteral("accepted"), false},
                                             {QStringLiteral("reason"), QStringLiteral("pile appears more than once")}},
                                 ErrorKind::Conflict, QStringLiteral("pile appears more than once"));
        }
        proposed[stationId].insert(change.pileId, change.toStatus);
        originalStatuses.insert(change.pileId, change.fromStatus);
    }

    for (auto stationIterator = proposed.constBegin(); stationIterator != proposed.constEnd(); ++stationIterator) {
        const qint64 stationId = stationIterator.key();
        if (!validatedVersions.contains(stationId)) {
            return recordOutcome(QStringLiteral("conflict"),
                                 QJsonObject{{QStringLiteral("accepted"), false},
                                             {QStringLiteral("reason"), QStringLiteral("stale station snapshot")},
                                             {QStringLiteral("station_id"), stationId}},
                                 ErrorKind::Conflict, QStringLiteral("stale station snapshot"));
        }
        const qint64 currentVersion = validatedVersions.value(stationId);
        stationVersions.insert(stationId, currentVersion);
        query.prepare(QStringLiteral(
            "SELECT status, COUNT(*) FROM charging_piles WHERE station_id = :station_id GROUP BY status"));
        query.bindValue(QStringLiteral(":station_id"), stationId);
        if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("count simulation station state failed: %1").arg(queryError(query))); return false; }
        while (query.next()) stateCounts[stationId].insert(query.value(0).toString(), query.value(1).toInt());
        for (auto changeIterator = stationIterator.value().constBegin(); changeIterator != stationIterator.value().constEnd(); ++changeIterator) {
            stateCounts[stationId][originalStatuses.value(changeIterator.key())] -= 1;
            stateCounts[stationId][changeIterator.value()] += 1;
        }
        query.prepare(QStringLiteral("SELECT status FROM stations WHERE id = :station_id AND status = 'active'"));
        query.bindValue(QStringLiteral(":station_id"), stationId);
        if (!query.exec() || !query.next()) { rollback(); setFailure(error, kind, ErrorKind::NotFound, QStringLiteral("station not found or inactive")); return false; }
        bool minIdleOk = false;
        const int configuredMinIdle = qEnvironmentVariableIntValue(
            "EV_PILE_SIMULATION_MIN_IDLE", &minIdleOk);
        const int minIdle = qMax(0, minIdleOk ? configuredMinIdle : 1);
        if (stateCounts[stationId].value(QStringLiteral("idle"), 0) < minIdle) {
            return recordOutcome(QStringLiteral("rejected"),
                                 QJsonObject{{QStringLiteral("accepted"), false},
                                             {QStringLiteral("reason"), QStringLiteral("minimum idle invariant would be violated")},
                                             {QStringLiteral("station_id"), stationId}},
                                 ErrorKind::Conflict, QStringLiteral("minimum idle invariant would be violated"));
        }
    }

    QJsonArray applied;
    for (const SimulationChange &change : changes) {
        query.prepare(QStringLiteral(
            "UPDATE charging_piles SET status = :status, status_source = 'simulation', "
            "status_updated_at = :updated_at, updated_at = :updated_at WHERE id = :id AND status = :from"));
        query.bindValue(QStringLiteral(":status"), change.toStatus);
        query.bindValue(QStringLiteral(":updated_at"), timestamp);
        query.bindValue(QStringLiteral(":id"), change.pileId);
        query.bindValue(QStringLiteral(":from"), change.fromStatus);
        if (!query.exec() || query.numRowsAffected() != 1) { rollback(); setFailure(error, kind, ErrorKind::Conflict, QStringLiteral("pile changed while applying simulation")); return false; }
        query.prepare(QStringLiteral(
            "INSERT INTO pile_status_events(pile_id, station_id, from_status, to_status, source, reason, tick_id, created_at) "
            "SELECT id, station_id, :from, :to, 'simulation', :reason, :tick_id, :created_at FROM charging_piles WHERE id = :pile_id"));
        query.bindValue(QStringLiteral(":from"), change.fromStatus);
        query.bindValue(QStringLiteral(":to"), change.toStatus);
        query.bindValue(QStringLiteral(":reason"), change.reason.isEmpty() ? QStringLiteral("simulation") : change.reason);
        query.bindValue(QStringLiteral(":tick_id"), tickId);
        query.bindValue(QStringLiteral(":created_at"), timestamp);
        query.bindValue(QStringLiteral(":pile_id"), change.pileId);
        if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("write simulation event failed: %1").arg(queryError(query))); return false; }
        applied.append(QJsonObject{{QStringLiteral("pile_id"), change.pileId},
                                   {QStringLiteral("from"), change.fromStatus},
                                   {QStringLiteral("to"), change.toStatus}});
    }
    QJsonObject versions;
    for (auto iterator = stationVersions.constBegin(); iterator != stationVersions.constEnd(); ++iterator) {
        const qint64 stationId = iterator.key();
        const qint64 nextVersion = iterator.value() + 1;
        query.prepare(QStringLiteral(
            "INSERT INTO simulation_state(station_id, seed_id, last_tick_id, last_run_at, snapshot_version) "
            "VALUES (:station_id, :seed_id, :tick_id, :last_run_at, :version) "
            "ON CONFLICT(station_id) DO UPDATE SET seed_id=excluded.seed_id, "
            "last_tick_id=excluded.last_tick_id, last_run_at=excluded.last_run_at, snapshot_version=excluded.snapshot_version"));
        query.bindValue(QStringLiteral(":station_id"), stationId);
        query.bindValue(QStringLiteral(":seed_id"), seedId);
        query.bindValue(QStringLiteral(":tick_id"), tickId);
        query.bindValue(QStringLiteral(":last_run_at"), timestamp);
        query.bindValue(QStringLiteral(":version"), nextVersion);
        if (!query.exec()) { rollback(); setFailure(error, kind, ErrorKind::Database, QStringLiteral("update simulation snapshot failed: %1").arg(queryError(query))); return false; }
        versions.insert(QString::number(stationId), nextVersion);
    }
    const QJsonObject accepted{{QStringLiteral("accepted"), true},
                               {QStringLiteral("simulator_id"), simulatorId},
                               {QStringLiteral("tick_id"), tickId},
                               {QStringLiteral("changes"), applied},
                               {QStringLiteral("snapshot_versions"), versions}};
    return recordOutcome(QStringLiteral("accepted"), accepted, ErrorKind::None, {});
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
