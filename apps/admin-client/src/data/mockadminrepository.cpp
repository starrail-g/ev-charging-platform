#include "mockadminrepository.h"

namespace {
constexpr int kCodeOk = 0;
constexpr int kCodeUnauthorized = 1100;
} // namespace

MockAdminRepository::MockAdminRepository(LoginMode mode)
    : m_mode(mode)
{
}

ev::LoginResult MockAdminRepository::login(const QString &username, const QString &password)
{
    ++m_loginCallCount;
    ev::LoginResult result;

    if (m_mode == LoginMode::ServiceUnavailable) {
        result.networkError = true;
        result.message = QStringLiteral("service unavailable (mock)");
        return result;
    }

    if (username.trimmed() != QStringLiteral("admin")
        || password != QStringLiteral("123456")) {
        result.errorCode = kCodeUnauthorized;
        result.message = QStringLiteral("invalid username or password");
        return result;
    }

    result.ok = true;
    result.errorCode = kCodeOk;
    result.admin = ev::AdminInfo{
        1,
        QStringLiteral("admin"),
        QStringLiteral("super_admin"),
        QStringLiteral("active"),
    };
    return result;
}
