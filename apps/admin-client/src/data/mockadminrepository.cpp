#include "mockadminrepository.h"

#include <QTimer>

namespace {
constexpr int kCodeOk = 0;
constexpr int kCodeUnauthorized = 1100;
// 模拟真实网络往返延迟：让"登录中…"/Loading 状态肉眼可见、演示更真实；
// 测试用 QTRY 等待不受影响（9/6 Socket 接入后为真实网络延迟）
constexpr int kMockNetworkDelayMs = 500;
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

    // 异步投递：模拟一次网络往返（500ms 延迟）；context 销毁后 Qt 自动不再调用（防悬垂回调）。
    QTimer::singleShot(kMockNetworkDelayMs, context, [result, callback = std::move(callback)] {
        if (callback)
            callback(result);
    });
}

void MockAdminRepository::fetchOverview(QObject *context,
                                        std::function<void(const ev::OverviewResult &)> callback)
{
    const ev::OverviewResult result = ev::mockdata::overview(m_overviewMode);

    // 与 login 同构：模拟网络往返后异步投递，context 销毁后不再回调
    QTimer::singleShot(kMockNetworkDelayMs, context, [result, callback = std::move(callback)] {
        if (callback)
            callback(result);
    });
}

void MockAdminRepository::fetchPiles(
    QObject *context, std::function<void(const ev::ListResult<ev::PileInfo> &)> callback)
{
    const ev::ListResult<ev::PileInfo> result = ev::mockdata::piles(m_overviewMode);

    // 与 fetchOverview 同构：同一演示模式、模拟网络往返
    QTimer::singleShot(kMockNetworkDelayMs, context, [result, callback = std::move(callback)] {
        if (callback)
            callback(result);
    });
}

void MockAdminRepository::fetchStations(
    QObject *context, std::function<void(const ev::ListResult<ev::StationInfo> &)> callback)
{
    const ev::ListResult<ev::StationInfo> result = ev::mockdata::stations(m_overviewMode);

    // 与 fetchOverview 同构：同一演示模式、模拟网络往返
    QTimer::singleShot(kMockNetworkDelayMs, context, [result, callback = std::move(callback)] {
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
