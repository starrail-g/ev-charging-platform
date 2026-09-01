#ifndef ADMINREPOSITORY_H
#define ADMINREPOSITORY_H

#include <QString>

namespace ev {

// 管理员对象。
// 字段待 9/4 与 B 对齐（协议文档注明 admin 字段随 database schema 冻结）；
// 当前按 database/schema/schema.sql administrators 表自拟。
struct AdminInfo {
    int id = 0;
    QString username;
    QString role;    // operator | super_admin
    QString status;  // active | disabled
};

// 登录结果。错误分支遵循 docs/architecture/protocol.md §Error Codes：
// 客户端只按 code 分支，不按 message 文本。
struct LoginResult {
    bool ok = false;
    AdminInfo admin;
    int errorCode = 0;         // 协议码：0=OK，1100=UNAUTHORIZED
    bool networkError = false; // 传输层错误（服务不可用），协议码不覆盖，单独标记
    QString message;           // 仅用于日志/兜底展示，不作为分支依据
};

// 数据层抽象：页面不建 Socket 不写 SQL（架构约定），
// 第一阶段用 MockAdminRepository，9/6 Socket 适配层替换。
class AdminRepository
{
public:
    virtual ~AdminRepository() = default;

    // 语义：成功 -> ok=true + admin；失败 -> ok=false + errorCode/networkError
    virtual LoginResult login(const QString &username, const QString &password) = 0;
};

} // namespace ev

#endif // ADMINREPOSITORY_H
