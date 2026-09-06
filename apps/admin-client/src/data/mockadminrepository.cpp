#include "mockadminrepository.h"

#include <QHash>
#include <QTimer>

namespace {
constexpr int kCodeOk = 0;
constexpr int kCodeUnauthorized = 1100;
constexpr int kCodeInvalidRequest = 1002;
constexpr int kCodeNotFound = 1200;
constexpr int kCodeConflict = 1201;
// 模拟真实网络往返延迟：让"登录中…"/Loading 状态肉眼可见、演示更真实；
// 测试用 QTRY 等待不受影响（9/6 Socket 接入后为真实网络延迟）
constexpr int kMockNetworkDelayMs = 500;
} // namespace

MockAdminRepository::MockAdminRepository(LoginMode mode)
    : m_mode(mode)
{
    // 业务快照取 mockdata 初始快照作为可变基础（模拟持久化层）；
    // mockdata::*() 纯函数保持无状态，供演示口径与测试直接断言。
    m_pileRows = ev::mockdata::piles(ev::mockdata::DataMode::Normal).items;
    m_userRows = ev::mockdata::users(ev::mockdata::DataMode::Normal).items;
    m_overviewStats = ev::mockdata::overview(ev::mockdata::DataMode::Normal).stats;
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
    ev::OverviewResult result = ev::mockdata::overview(m_overviewMode);
    if (m_overviewMode == ev::mockdata::DataMode::Normal) {
        // 概览统计 = 固定指标 + 桩五态计数从业务快照派生（重启等动作后
        // 概览计数与桩页状态保持同源，不出现"概览 1 故障 / 桩页已空闲"撕裂）
        result.stats = m_overviewStats;
        result.hasData = true;
        int idle = 0, reserved = 0, charging = 0, fault = 0, offline = 0;
        for (const ev::PileInfo &pile : m_pileRows) {
            switch (pile.status) {
            case ev::PileStatus::Idle: ++idle; break;
            case ev::PileStatus::Reserved: ++reserved; break;
            case ev::PileStatus::Charging: ++charging; break;
            case ev::PileStatus::Fault: ++fault; break;
            case ev::PileStatus::Offline: ++offline; break;
            case ev::PileStatus::Unknown: break;
            }
        }
        result.stats.pileIdle = idle;
        result.stats.pileReserved = reserved;
        result.stats.pileCharging = charging;
        result.stats.pileFault = fault;
        result.stats.pileOffline = offline;
    }

    // 与 login 同构：模拟网络往返后异步投递，context 销毁后不再回调
    QTimer::singleShot(kMockNetworkDelayMs, context, [result, callback = std::move(callback)] {
        if (callback)
            callback(result);
    });
}

void MockAdminRepository::fetchPiles(
    QObject *context, std::function<void(const ev::ListResult<ev::PileInfo> &)> callback)
{
    ev::ListResult<ev::PileInfo> result = ev::mockdata::piles(m_overviewMode);
    if (m_overviewMode == ev::mockdata::DataMode::Normal)
        result.items = m_pileRows; // 业务快照：动作（重启）后的状态在此可见

    // 与 fetchOverview 同构：同一演示模式、模拟网络往返
    QTimer::singleShot(kMockNetworkDelayMs, context, [result, callback = std::move(callback)] {
        if (callback)
            callback(result);
    });
}

void MockAdminRepository::fetchStations(
    QObject *context, std::function<void(const ev::ListResult<ev::StationInfo> &)> callback)
{
    ev::ListResult<ev::StationInfo> result = ev::mockdata::stations(m_overviewMode);
    if (m_overviewMode == ev::mockdata::DataMode::Normal) {
        // 桩数/在线率与业务快照同源（schema station_pile_status view 语义）
        QHash<int, int> totalByStation;
        QHash<int, int> onlineByStation;
        for (const ev::PileInfo &pile : m_pileRows) {
            totalByStation[pile.stationId] += 1;
            if (pile.status != ev::PileStatus::Fault && pile.status != ev::PileStatus::Offline)
                onlineByStation[pile.stationId] += 1;
        }
        for (ev::StationInfo &station : result.items) {
            station.pileCount = totalByStation.value(station.id);
            station.onlinePileCount = onlineByStation.value(station.id);
        }
    }

    // 与 fetchOverview 同构：同一演示模式、模拟网络往返
    QTimer::singleShot(kMockNetworkDelayMs, context, [result, callback = std::move(callback)] {
        if (callback)
            callback(result);
    });
}

void MockAdminRepository::fetchUsers(
    QObject *context, std::function<void(const ev::ListResult<ev::UserInfo> &)> callback)
{
    ev::ListResult<ev::UserInfo> result = ev::mockdata::users(m_overviewMode);
    if (m_overviewMode == ev::mockdata::DataMode::Normal)
        result.items = m_userRows; // 业务快照：冻结/解冻后的状态在此可见

    // 与 fetchOverview 同构：同一演示模式、模拟网络往返
    QTimer::singleShot(kMockNetworkDelayMs, context, [result, callback = std::move(callback)] {
        if (callback)
            callback(result);
    });
}

void MockAdminRepository::restartPile(
    const QString &pileCode, QObject *context,
    std::function<void(const ev::ActionResult &)> callback)
{
    const ev::ActionResult result = doRestartPile(pileCode);
    // 与 login 同构：模拟网络往返后异步投递，context 销毁后不再回调
    QTimer::singleShot(kMockNetworkDelayMs, context, [result, callback = std::move(callback)] {
        if (callback)
            callback(result);
    });
}

void MockAdminRepository::setUserStatus(
    int userId, const QString &status, QObject *context,
    std::function<void(const ev::ActionResult &)> callback)
{
    const ev::ActionResult result = doSetUserStatus(userId, status);
    // 与 login 同构：模拟网络往返后异步投递，context 销毁后不再回调
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

ev::ActionResult MockAdminRepository::doRestartPile(const QString &pileCode)
{
    ev::ActionResult result;
    for (ev::PileInfo &pile : m_pileRows) {
        if (pile.pileCode != pileCode)
            continue;
        if (pile.status == ev::PileStatus::Fault || pile.status == ev::PileStatus::Offline) {
            // 模拟重启成功 + 自检通过：桩转 idle（远程重启语义可观察）
            pile.status = ev::PileStatus::Idle;
            result.ok = true;
            result.errorCode = kCodeOk;
            result.message =
                QStringLiteral("桩 %1 已重启，状态：空闲（模拟）").arg(pileCode);
        } else {
            // 状态冲突：运行中/空闲桩不允许远程重启（协议 1201 CONFLICT 语义）
            result.errorCode = kCodeConflict;
            result.message = QStringLiteral("仅故障/离线桩可重启，当前状态：%1")
                                 .arg(ev::pileStatusToDisplay(pile.status));
        }
        return result;
    }
    result.errorCode = kCodeNotFound;
    result.message = QStringLiteral("充电桩 %1 不存在").arg(pileCode);
    return result;
}

ev::ActionResult MockAdminRepository::doSetUserStatus(int userId, const QString &status)
{
    ev::ActionResult result;
    if (status != QStringLiteral("active") && status != QStringLiteral("frozen")) {
        result.errorCode = kCodeInvalidRequest;
        result.message = QStringLiteral("不支持的状态值：%1").arg(status);
        return result;
    }
    for (ev::UserInfo &user : m_userRows) {
        if (user.id != userId)
            continue;
        if (user.status == status) {
            // 重复提交相同状态：状态转换不允许（1201 CONFLICT）
            result.errorCode = kCodeConflict;
            result.message = QStringLiteral("用户 %1 已处于该状态").arg(user.phone);
            return result;
        }
        user.status = status;
        result.ok = true;
        result.errorCode = kCodeOk;
        result.message = status == QStringLiteral("frozen")
            ? QStringLiteral("用户 %1 已冻结（模拟）").arg(user.phone)
            : QStringLiteral("用户 %1 已解冻（模拟）").arg(user.phone);
        return result;
    }
    result.errorCode = kCodeNotFound;
    result.message = QStringLiteral("用户（id=%1）不存在").arg(userId);
    return result;
}
