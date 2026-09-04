#ifndef MOCKADMINREPOSITORY_H
#define MOCKADMINREPOSITORY_H

#include "adminrepository.h"

#include "mockdataset.h"

#include <functional>

class QObject;

// 第一阶段 Mock 数据层（9/6 由 Socket 适配层替换）。
// 固定账号对齐 database/seeds/dev.sql：admin / 123456
// （密码哈希 8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92 = SHA-256("123456")）。
// 登录模式可注入，供 tst_loginflow 覆盖 成功 / 密码错误(1100) / 服务不可用 三场景。
class MockAdminRepository : public ev::AdminRepository
{
public:
    enum class LoginMode {
        Ok,                 // 真实校验：admin/123456 成功，其余 1100
        ServiceUnavailable  // 模拟传输层不可达（networkError）
    };

    explicit MockAdminRepository(LoginMode mode = LoginMode::Ok);

    // 异步实现：singleShot(0) 模拟一次网络往返，不阻塞调用线程；
    // context 销毁（如页面关闭）后回调自动不再投递。
    void login(const QString &username, const QString &password,
               QObject *context,
               std::function<void(const ev::LoginResult &)> callback) override;

    // 异步概览：按当前演示模式返回 mockdata 三态数据（模拟网络往返）
    void fetchOverview(QObject *context,
                       std::function<void(const ev::OverviewResult &)> callback) override;

    // 异步桩明细/站点列表：与 fetchOverview 同构（同一演示模式、模拟网络往返）
    void fetchPiles(QObject *context,
                    std::function<void(const ev::ListResult<ev::PileInfo> &)> callback) override;
    void fetchStations(QObject *context,
                       std::function<void(const ev::ListResult<ev::StationInfo> &)> callback) override;
    void fetchUsers(QObject *context,
                    std::function<void(const ev::ListResult<ev::UserInfo> &)> callback) override;

    QString dataSourceName() const override { return QStringLiteral("Mock 演示"); }

    // 演示数据模式（Mock 特有，不进抽象接口；供概览页下拉驱动，
    // 9/6 Socket 接入后由数据层自动驱动）
    void setOverviewMode(ev::mockdata::DataMode mode) { m_overviewMode = mode; }
    ev::mockdata::DataMode overviewMode() const { return m_overviewMode; }

    LoginMode mode() const { return m_mode; }
    void setMode(LoginMode mode) { m_mode = mode; }

    // 登录调用计数（测试用：断言空输入/校验失败场景未触达 Repository）
    int loginCallCount() const { return m_loginCallCount; }

private:
    ev::LoginResult doLogin(const QString &username, const QString &password) const;

    LoginMode m_mode = LoginMode::Ok;
    int m_loginCallCount = 0;
    ev::mockdata::DataMode m_overviewMode = ev::mockdata::DataMode::Normal;
};

#endif // MOCKADMINREPOSITORY_H
