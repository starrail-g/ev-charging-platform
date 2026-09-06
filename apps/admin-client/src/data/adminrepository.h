#ifndef ADMINREPOSITORY_H
#define ADMINREPOSITORY_H

#include <QString>

#include <functional>

#include "models/adminmodels.h"

class QObject;

namespace ev {

// 管理员对象。
// 2026-09-05 与 B 对齐实证: admin.login 响应 payload.admin = {id, username, role,
// status}(database.cpp loginAdministrator 构造点), 键名全命中, 语义随 schema 冻结。
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

// 管理动作结果（login 同构：错误分支只按 errorCode 分支，message 仅展示/日志）。
// errorCode 语义同 docs/architecture/protocol.md §Error Codes：
//   0=OK、1002=INVALID_REQUEST（参数非法）、1200=NOT_FOUND、1201=CONFLICT（状态转换不允许）。
struct ActionResult {
    bool ok = false;
    int errorCode = 0;
    bool networkError = false; // 传输层错误（服务不可达），协议码不覆盖
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

    // 异步获取用户列表:语义同 fetchOverview(统一 UI Task 6,用户工作页)。
    virtual void fetchUsers(QObject *context,
                            std::function<void(const ListResult<UserInfo> &)> callback) = 0;

    // 数据来源标识（状态栏展示用）：Mock 返回 "Mock 演示"，
    // 未来 Socket 适配层返回自身标识；空串表示不展示来源。
    virtual QString dataSourceName() const { return QString(); }

    // 异步远程重启充电桩（C-S1-005；第一阶段为服务端确认后的状态模拟）：
    // 仅故障/离线桩允许重启，成功后桩转 idle（模拟自检通过）并可观察；
    // 其余状态返回 1201 CONFLICT。异步语义同 login（context 防悬垂、事件循环派发）。
    virtual void restartPile(const QString &pileCode,
                             QObject *context,
                             std::function<void(const ActionResult &)> callback) = 0;

    // 异步冻结/解冻用户（C-S1-007；第一阶段为模拟确认）：status ∈ active|frozen；
    // 与当前状态相同返回 1201 CONFLICT，用户不存在返回 1200。
    virtual void setUserStatus(int userId,
                               const QString &status,
                               QObject *context,
                               std::function<void(const ActionResult &)> callback) = 0;
};

} // namespace ev

#endif // ADMINREPOSITORY_H
