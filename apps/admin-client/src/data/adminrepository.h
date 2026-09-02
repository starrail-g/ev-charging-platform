#ifndef ADMINREPOSITORY_H
#define ADMINREPOSITORY_H

#include <QString>

#include <functional>

#include "models/adminmodels.h"

class QObject;

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

    // 异步登录：立即返回，结果经 callback 投递（同一线程事件循环内派发）。
    // context 非空时，callback 仅在 context 存活期间被调用（context 销毁后自动取消，防悬垂）；
    // 调用方负责超时保护（LoginPage 内置 10s 超时）。
    virtual void login(const QString &username, const QString &password,
                       QObject *context,
                       std::function<void(const LoginResult &)> callback) = 0;

    // 异步获取概览数据：语义同 login（立即返回、结果经 callback 在事件循环派发、
    // context 销毁后自动不再回调）。
    // 第一阶段 Mock 返回三态演示数据；9/6 Socket 适配层返回真实协议数据，
    // 页面只依赖本接口，不直接触达数据源（P2 review 修复：标签与数据同源）。
    virtual void fetchOverview(QObject *context,
                               std::function<void(const OverviewResult &)> callback) = 0;

    // 异步获取桩明细列表:语义同 fetchOverview。
    // 概览页"需关注"列表与站点态势图需要桩级状态/站点坐标明细,
    // OverviewResult 只承载统计,不含明细(统一 UI Task 5 扩展)。
    virtual void fetchPiles(QObject *context,
                            std::function<void(const ListResult<PileInfo> &)> callback) = 0;

    // 异步获取站点列表:语义同 fetchOverview。
    virtual void fetchStations(QObject *context,
                               std::function<void(const ListResult<StationInfo> &)> callback) = 0;

    // 数据来源标识（状态栏展示用）：Mock 返回 "Mock 演示"，
    // 未来 Socket 适配层返回自身标识；空串表示不展示来源。
    virtual QString dataSourceName() const { return QString(); }
};

} // namespace ev

#endif // ADMINREPOSITORY_H
