#ifndef ADMINSMODELS_H
#define ADMINSMODELS_H

#include <QString>

#include <QList>

// 管理端只读展示模型（第一阶段）。
// 字段对齐 database/schema/schema.sql（stations / charging_piles / users），
// 语义对齐 docs/architecture/protocol.md：金额一律整数分（*_cents）、
// 时间 UTC ISO-8601、桩状态协议五态。待 9/4 与 B 评审后最终冻结。
namespace ev {

// 桩状态（协议五态 + 未知兜底，protocol.md；解析不出协议值 → Unknown）
enum class PileStatus {
    Idle,      // idle
    Reserved,  // reserved
    Charging,  // charging
    Fault,     // fault
    Offline,   // offline
    Unknown,   // unknown（协议未知值 / 解析失败；可展示、不可参与关注排序）
};

// 解析协议状态字符串；未知值返回 Unknown 语义（不崩溃，见 C-S1-019）
PileStatus parsePileStatus(const QString &status);
QString pileStatusToDisplay(PileStatus status);        // 中文展示
QString pileStatusToProtocol(PileStatus status);       // 协议原串

struct PileInfo {
    int id = 0;
    int stationId = 0;
    QString pileCode;
    QString pileType;              // fast | slow
    double powerKw = 0.0;
    int unitPriceCentsPerKwh = 0;  // 整数分/千瓦时
    PileStatus status = PileStatus::Idle;
    int totalChargeCount = 0;      // 累计充电次数（schema total_charge_count，A-04 桩表格）
    int totalChargeSeconds = 0;    // 累计充电时长（秒，schema total_charge_seconds，A-04 桩表格）
};

// 累计充电时长秒数 → 展示文本（如 316800 → "88h 0m"）。纯函数，tst_ui 锁定格式。
QString formatChargeDuration(int totalSeconds);

// 金额（整数分）→ 展示文本（如 286540 → "¥2,865.40"，千分位、无浮点漂移；
// 口径同 Web formatCents）。纯函数，tst_ui 锁定格式；负值/零值/大额统一处理。
QString formatYuanCents(qint64 cents);

struct StationInfo {
    int id = 0;
    QString name;
    QString address;
    double latitude = 0.0;
    double longitude = 0.0;
    QString status;                // active | inactive
    int pileCount = 0;             // 展示用（由桩聚合）
};

struct UserInfo {
    int id = 0;
    QString phone;
    QString nickname;
    int balanceCents = 0;          // 整数分
    QString status;                // active | frozen
};

// 概览页指标（C-S1-003）。字段待 B 的 statistics 口径确认（9/4），
// 当前按大屏 demo JSON 同口径自拟。
struct OverviewStats {
    qint64 revenueCents = 0;              // 近 7 日营收（分）
    int pileIdle = 0;
    int pileReserved = 0;
    int pileCharging = 0;
    int pileFault = 0;
    int pileOffline = 0;
    double avgStationUtilization = 0.0;   // 0..1
    QString updatedAt;                    // UTC ISO-8601，如 2026-09-01T10:15:00Z
};

// 通用列表结果包装(数据层与页面层共用;原 mockdataset::MockResult 语义):
//   ok=false    —— 接口失败(调用方必须按 ok 分支展示错误态,不能只依赖列表长度)
//   error       —— 失败原因文案(展示/日志用)
//   items       —— 成功时的数据列表
template <typename T>
struct ListResult {
    bool ok = true;
    QString error;
    QList<T> items;
};

// 概览结果包装(数据层与页面层共用,P2 review 修复后上移为通用模型):
//   ok=false     —— 接口失败(调用方按 ok 分支展示错误态)
//   hasData=false —— 空数据,由数据层显式声明;禁止从指标全零反推
//                    （新站点/当日无营收时全零可能是有效数据）
struct OverviewResult {
    bool ok = true;
    QString error;
    OverviewStats stats;
    bool hasData = false;
};

} // namespace ev

#endif // ADMINSMODELS_H
