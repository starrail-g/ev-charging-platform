#include "mockadminrepository.h"

#include <QTimer>

namespace {
constexpr int kCodeOk = 0;
constexpr int kCodeUnauthorized = 1100;
} // namespace

MockAdminRepository::MockAdminRepository(LoginMode mode)
    : m_mode(mode)
{
}

void MockAdminRepository::login(const QString &username, const QString &password,
                                QObject *context,
                                std::function<void(const ev::LoginResult &)> callback)
{
    ++m_loginCallCount;
    const ev::LoginResult result = doLogin(username, password);

    // 异步投递：singleShot(0) 模拟一次网络往返；context 销毁后 Qt 自动不再调用（防悬垂回调）。
    QTimer::singleShot(0, context, [result, callback = std::move(callback)] {
        if (callback)
            callback(result);
    });
}

ev::LoginResult MockAdminRepository::doLogin(const QString &username, const QString &password) const
{
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
