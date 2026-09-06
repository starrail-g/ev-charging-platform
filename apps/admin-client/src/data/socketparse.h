#ifndef SOCKETPARSE_H
#define SOCKETPARSE_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "adminrepository.h" // AdminInfo / LoginResult
#include "models/adminmodels.h"

// wire(snake_case JSON) → C 模型 解析单点(设计稿 D4)。
//
// 职责:
//  1. 协议响应 payload(成功 .result 的 payload / error 信封) → ev 模型;
//  2. 容错: 缺字段/类型错 → 该字段默认值(不崩), 结构错(数组键缺失/非对象) → 返回失败标记;
//     列表项解析失败 → 跳过该项不废整页(同 C-S1-002"坏帧保留好帧"哲学);
//  3. 告警限流: 解析问题经 issues(QStringList) 收集, 每响应最多记 3 条(防刷屏),
//     顶层函数统一 qWarning 一次;
//  4. 纯函数、无 QObject 依赖, 可独立单测。
//
// 字段映射冻结状态(2026-09-05): B 在 PR #10(f04f428→45627d5 四个 commit:
// 1f157de revenue_daily / 11702ae+4eb0bad 利用率 / 45627d5 restart 语义)冻结并落地,
// 下表键名已按 B 分支 libs/database/src/database.cpp 实际构造点逐字段核对
// (loginAdministrator/readPile/listAdminUsers/listAdminStations/getStatistics), 全部命中;
// statistics 响应结构另见 docs/api/README.md「管理端统计响应」样例(2026-09-05 B 版)。
//
// ┌──────────────────────────┬──────────────────────────┬──────────────────────────────┐
// │ wire JSON 键(snake)      │ C 模型字段               │ 依据                         │
// ├──────────────────────────┼──────────────────────────┼──────────────────────────────┤
// │ pile: id                 │ PileInfo.id              │ database.cpp readPile SELECT │
// │      station_id          │ stationId                │   (charging_piles 列)        │
// │      pile_code           │ pileCode                 │                              │
// │      pile_type           │ pileType                 │                              │
// │      power_kw            │ powerKw                  │                              │
// │      unit_price_cents_per_kwh │ unitPriceCentsPerKwh│ 金额整数分(protocol.md)      │
// │      status              │ status(parsePileStatus)  │ 五态, 未知→Unknown           │
// │      total_charge_count  │ totalChargeCount         │ A-04 桩表格                  │
// │      total_charge_seconds│ totalChargeSeconds       │ A-04 桩表格                  │
// │      (restart_count/last_restart_at 忽略: 模型无字段)│                            │
// ├──────────────────────────┼──────────────────────────┼──────────────────────────────┤
// │ station: id/name/address │ StationInfo 同名         │ database.cpp listAdminStations│
// │      latitude/longitude/status │ 同名               │   (stations 列)              │
// │      pile_total          │ pileCount                │ listStations 聚合列/视图     │
// │      在线桩数: pile_total−pile_fault−pile_offline  │ station_pile_status 视图     │
// │        (无 fault/offline 时退化为 pile_idle+        │ (在线=非故障/离线,A-06 口径) │
// │         pile_reserved+pile_charging, B listStations │ 缺 fault/offline 聚合列)    │
// │      (utilization/utilization_range 忽略: 站页展示 │ B 2026-09-05 新增(11702ae),  │
// │       在线率自算, 与 7d 利用率是不同概念)          │ C 模型无字段                 │
// ├──────────────────────────┼──────────────────────────┼──────────────────────────────┤
// │ user: id/phone/nickname/ │ UserInfo 同名            │ database.cpp listAdminUsers  │
// │      balance_cents       │ balanceCents             │ 金额整数分                   │
// │      status              │ status(active|frozen)    │                              │
// │      created_at          │ createdAt(UTC 原串)      │ schema users.created_at(A-07)│
// │      (avatar_path/active_order_status 忽略)        │ 模型无字段                   │
// ├──────────────────────────┼──────────────────────────┼──────────────────────────────┤
// │ statistics: revenue_cents│ OverviewStats.revenueCents │ B getStatistics: 聚合值 =  │
// │      revenue_daily[{     │ (缺省时对数组 revenue_cents│ 同 revenue_daily 之和;     │
// │       date,              │  求和兜底)               │ 固定长度 7/30 条、UTC 日历日 │
// │       revenue_cents,     │                          │ 升序、无单日补 0(冻结:      │
// │       completed_order_count, energy_wh}]            │ 1f157de + B 文档样例)        │
// │      (无独立 30d 合计键) │ revenue30dCents          │ fetchOverview 双请求(7d+30d):│
// │                          │                          │ 30d 响应 revenue_cents 填入   │
// │                          │                          │ (2026-09-05 冻结, 原自拟键    │
// │                          │                          │  revenue30d_cents 已废弃)    │
// │      pile_idle/reserved/ │ 同名五态计数             │ station_pile_status 视图列   │
// │      charging/fault/offline │                       │                              │
// │      avg_station_utilization │ avgStationUtilization│ 冻结(11702ae/4eb0bad): 最近  │
// │                          │                          │ 7 个 UTC 自然日时间加权占用率 │
// │                          │                          │ (订单时长∩窗口/桩可用时长),  │
// │                          │                          │ 站均=简单平均, 与 range 无关  │
// │      updated_at          │ updatedAt(UTC 原串)      │ 冻结: 快照整秒(与站列表同    │
// │                          │                          │ 口径, 4eb0bad)               │
// │      (range: 7d|30d 回声, 仅诊断不参与解析)         │ B 响应含 range 回声          │
// ├──────────────────────────┼──────────────────────────┼──────────────────────────────┤
// │ admin: id/username/role/ │ AdminInfo 同名           │ loginAdministrator 构造点    │
// │      status              │                          │ (role: operator|super_admin, │
// │                          │                          │  status: active|disabled)    │
// │ admin.login.result:      │ LoginResult.token        │ Q6 冻结(2026-09-06): 8h 会话 │
// │      token               │                          │ token, 非空字符串必需        │
// │ admin.* 请求(除 login)   │ payload.token            │ Q6 冻结(2026-09-06): PR #12; │
// │                          │ mutation 另带            │ 8h 进程内会话 token 每请求携带;│
// │                          │ payload.administrator_id │ mutation 三类绑定 token 主体  │
// │                          │                          │ buildPayload 单点附加         │
// └──────────────────────────┴──────────────────────────┴──────────────────────────────┘
namespace ev {
namespace socketparse {

// ── 单条对象解析(缺字段/类型错 → 默认值 + issues 限流记录; 不抛不崩) ─────────────
PileInfo parsePile(const QJsonObject &obj, QStringList *issues = nullptr);
StationInfo parseStation(const QJsonObject &obj, QStringList *issues = nullptr);
UserInfo parseUser(const QJsonObject &obj, QStringList *issues = nullptr);
OverviewStats parseStatistics(const QJsonObject &statisticsObject,
                              QStringList *issues = nullptr);
void parseAdmin(const QJsonObject &obj, AdminInfo *admin,
                QStringList *issues = nullptr);

// ── 顶层响应 payload 解析(结构错 → 返回 false + reason; 列表坏项跳过不废整页) ─────
// payload["piles"]: 数组; 非对象元素/无法识别项跳过(计入 issues)
bool parsePilesPayload(const QJsonObject &payload, QList<PileInfo> *piles,
                       QStringList *issues, QString *reason);
// payload["stations"]: 数组
bool parseStationsPayload(const QJsonObject &payload, QList<StationInfo> *stations,
                          QStringList *issues, QString *reason);
// payload["users"]: 数组
bool parseUsersPayload(const QJsonObject &payload, QList<UserInfo> *users,
                       QStringList *issues, QString *reason);
// payload["statistics"]: 必须是对象(整体结构错 → false)
bool parseStatisticsPayload(const QJsonObject &payload, OverviewStats *stats,
                            QStringList *issues, QString *reason);
// payload["admin"]: 必须是对象; admin.login.result 专用
bool parseAdminLoginPayload(const QJsonObject &payload, LoginResult *out,
                            QStringList *issues, QString *reason);

// error 信封 payload 解析(D3): {code:int, name, message}; code 缺失/类型错 → 1002
// (INVALID_REQUEST, 服务端坏信封按请求类错误处理, 不冒充传输层错误)
int parseErrorCode(const QJsonObject &errorPayload, QStringList *issues = nullptr);

} // namespace socketparse
} // namespace ev

#endif // SOCKETPARSE_H
