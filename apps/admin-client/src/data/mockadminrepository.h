#ifndef MOCKADMINREPOSITORY_H
#define MOCKADMINREPOSITORY_H

#include "adminrepository.h"

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

    ev::LoginResult login(const QString &username, const QString &password) override;

    LoginMode mode() const { return m_mode; }
    void setMode(LoginMode mode) { m_mode = mode; }

    // 登录调用计数（测试用：断言空输入/校验失败场景未触达 Repository）
    int loginCallCount() const { return m_loginCallCount; }

private:
    LoginMode m_mode = LoginMode::Ok;
    int m_loginCallCount = 0;
};

#endif // MOCKADMINREPOSITORY_H
