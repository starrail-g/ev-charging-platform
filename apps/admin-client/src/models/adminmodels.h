#ifndef ADMINSMODELS_H
#define ADMINSMODELS_H

#include <QString>

#include <QList>

// 管理端只读展示模型（第一阶段）。
// 字段对齐 database/schema/schema.sql（stations / charging_piles / users），
// 语义对齐 docs/architecture/protocol.md：金额一律整数分（*_cents）、
// 时间 UTC ISO-8601、桩状态协议五态。待 9/4 与 B 评审后最终冻结。
namespace ev {

// 桩状态（协议五态，protocol.md）
enum class PileStatus {
    Idle,      // idle
    Reserved,  // reserved
    Charging,  // charging
    Fault,     // fault
    Offline,   // offline
};

// 解析协议状态字符串；未知值返回 Unknown 语义（不崩溃，见 C-S1-019）
enum class PileStatusParse { Idle, Reserved, Charging, Fault, Offline, Unknown };
PileStatusParse parsePileStatus(const QString &status);
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
};

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

} // namespace ev

#endif // ADMINSMODELS_H
