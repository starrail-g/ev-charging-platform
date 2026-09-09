#pragma once

#include <QJsonObject>
#include <QString>

namespace ev::protocol {

constexpr int kProtocolVersion = 1;
constexpr quint32 kMaxPayloadBytes = 1024 * 1024;

enum class ErrorCode {
    Ok = 0,
    InvalidFrame = 1000,
    InvalidJson = 1001,
    InvalidRequest = 1002,
    UnsupportedVersion = 1003,
    Unauthorized = 1100,
    AccountFrozen = 1101,
    NotFound = 1200,
    Conflict = 1201,
    InsufficientBalance = 1202,
    DatabaseError = 1300,
    MapDisabled = 1400,
    MapNotConfigured = 1401,
    MapUpstreamTimeout = 1402,
    MapUpstreamUnavailable = 1403,
    MapQuotaExceeded = 1404,
    MapPermissionDenied = 1405,
    MapNoResult = 1406,
    MapResponseInvalid = 1407,
    MapRateLimited = 1408,
    MapResponseTooLarge = 1409,
    MapServerMock = 1410,
    InternalError = 1500
};

struct Message {
    int version = kProtocolVersion;
    QString id;
    QString type;
    QJsonObject payload;

    bool isValid(QString *reason = nullptr) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Message *message,
                         QString *reason = nullptr);
};

QJsonObject errorPayload(ErrorCode code, const QString &message);
QString errorCodeName(ErrorCode code);
bool isRetryable(ErrorCode code);

} // namespace ev::protocol
