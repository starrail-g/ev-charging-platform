#include "ev_protocol/message.h"

namespace ev::protocol {

bool Message::isValid(QString *reason) const
{
    if (version != kProtocolVersion) {
        if (reason) *reason = QStringLiteral("unsupported protocol version");
        return false;
    }
    if (id.isEmpty() || id.size() > 64) {
        if (reason) *reason = QStringLiteral("id must contain 1..64 characters");
        return false;
    }
    if (type.isEmpty() || type.size() > 64) {
        if (reason) *reason = QStringLiteral("type must contain 1..64 characters");
        return false;
    }
    return true;
}

QJsonObject Message::toJson() const
{
    return QJsonObject{{QStringLiteral("v"), version},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("payload"), payload}};
}

bool Message::fromJson(const QJsonObject &object, Message *message, QString *reason)
{
    if (!message || !object.value(QStringLiteral("v")).isDouble()
        || !object.value(QStringLiteral("id")).isString()
        || !object.value(QStringLiteral("type")).isString()
        || !object.value(QStringLiteral("payload")).isObject()) {
        if (reason) *reason = QStringLiteral("required fields are missing or have the wrong type");
        return false;
    }

    Message parsed;
    parsed.version = object.value(QStringLiteral("v")).toInt();
    parsed.id = object.value(QStringLiteral("id")).toString();
    parsed.type = object.value(QStringLiteral("type")).toString();
    parsed.payload = object.value(QStringLiteral("payload")).toObject();
    if (!parsed.isValid(reason)) return false;
    *message = parsed;
    return true;
}

QJsonObject errorPayload(ErrorCode code, const QString &message)
{
    return QJsonObject{{QStringLiteral("code"), static_cast<int>(code)},
                       {QStringLiteral("name"), errorCodeName(code)},
                       {QStringLiteral("message"), message}};
}

QString errorCodeName(ErrorCode code)
{
    switch (code) {
    case ErrorCode::Ok: return QStringLiteral("OK");
    case ErrorCode::InvalidFrame: return QStringLiteral("INVALID_FRAME");
    case ErrorCode::InvalidJson: return QStringLiteral("INVALID_JSON");
    case ErrorCode::InvalidRequest: return QStringLiteral("INVALID_REQUEST");
    case ErrorCode::UnsupportedVersion: return QStringLiteral("UNSUPPORTED_VERSION");
    case ErrorCode::Unauthorized: return QStringLiteral("UNAUTHORIZED");
    case ErrorCode::AccountFrozen: return QStringLiteral("ACCOUNT_FROZEN");
    case ErrorCode::NotFound: return QStringLiteral("NOT_FOUND");
    case ErrorCode::Conflict: return QStringLiteral("CONFLICT");
    case ErrorCode::InsufficientBalance: return QStringLiteral("INSUFFICIENT_BALANCE");
    case ErrorCode::DatabaseError: return QStringLiteral("DATABASE_ERROR");
    case ErrorCode::InternalError: return QStringLiteral("INTERNAL_ERROR");
    }
    return QStringLiteral("INTERNAL_ERROR");
}

} // namespace ev::protocol
