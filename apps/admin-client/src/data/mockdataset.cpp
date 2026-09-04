#include "mockdataset.h"

#include <QHash>

namespace ev {
namespace mockdata {

namespace {
const QString kSimulatedError = QStringLiteral("mock: simulated interface error");
}

MockResult<PileInfo> piles(DataMode mode)
{
    MockResult<PileInfo> result;
    if (mode == DataMode::Error) {
        result.ok = false;
        result.error = kSimulatedError;
        return result;
    }
    if (mode != DataMode::Normal)
        return result; // Empty：ok=true，空列表

    // 累计次数/时长与演示口径自洽：seconds ≈ count × 单次均时
    //（P-101-* 均约 40min、P-101-C 45min、P-202-A 35min、P-202-C 30min）
    result.items.append(PileInfo{1, 1, QStringLiteral("P-101-A"), QStringLiteral("fast"),
                                 120.0, 120, PileStatus::Charging, 132, 316800});
    result.items.append(PileInfo{2, 1, QStringLiteral("P-101-B"), QStringLiteral("fast"),
                                 120.0, 120, PileStatus::Idle, 96, 230400});
    result.items.append(PileInfo{3, 1, QStringLiteral("P-101-C"), QStringLiteral("slow"),
                                 60.0, 90, PileStatus::Fault, 45, 121500});
    result.items.append(PileInfo{4, 2, QStringLiteral("P-202-A"), QStringLiteral("fast"),
                                 150.0, 150, PileStatus::Charging, 187, 392700});
    result.items.append(PileInfo{5, 2, QStringLiteral("P-202-B"), QStringLiteral("fast"),
                                 150.0, 150, PileStatus::Reserved, 64, 153600});
    result.items.append(PileInfo{6, 2, QStringLiteral("P-202-C"), QStringLiteral("slow"),
                                 60.0, 90, PileStatus::Offline, 28, 50400});
    return result;
}

MockResult<StationInfo> stations(DataMode mode)
{
    MockResult<StationInfo> result;
    if (mode == DataMode::Error) {
        result.ok = false;
        result.error = kSimulatedError;
        return result;
    }
    if (mode != DataMode::Normal)
        return result;

    // 桩数/在线数从同一演示快照聚合（schema station_pile_status view 语义：
    // pile_total / 在线 = 非 故障+离线），与全局可用率口径同源，不另造数值。
    const auto pileRows = piles(mode);
    QHash<int, int> totalByStation;
    QHash<int, int> onlineByStation;
    for (const PileInfo &pile : pileRows.items) {
        totalByStation[pile.stationId] += 1;
        if (pile.status != PileStatus::Fault && pile.status != PileStatus::Offline)
            onlineByStation[pile.stationId] += 1;
    }

    result.items.append(StationInfo{1, QStringLiteral("东软园区充电站"), QStringLiteral("沈阳市浑南区东软软件园"),
                                    41.7331, 123.4395, QStringLiteral("active"),
                                    totalByStation.value(1), onlineByStation.value(1)});
    result.items.append(StationInfo{2, QStringLiteral("沈阳站前充电站"), QStringLiteral("沈阳市和平区胜利南街"),
                                    41.7923, 123.3942, QStringLiteral("active"),
                                    totalByStation.value(2), onlineByStation.value(2)});
    return result;
}

MockResult<UserInfo> users(DataMode mode)
{
    MockResult<UserInfo> result;
    if (mode == DataMode::Error) {
        result.ok = false;
        result.error = kSimulatedError;
        return result;
    }
    if (mode != DataMode::Normal)
        return result;

    // 注册时间（UTC ISO-8601，schema users.created_at 同口径；均早于演示快照 2026-09-01）
    result.items.append(UserInfo{1, QStringLiteral("13800138000"), QStringLiteral("用户8000"), 16950,
                                 QStringLiteral("active"), QStringLiteral("2026-08-15T03:24:00Z")});
    result.items.append(UserInfo{2, QStringLiteral("13900139000"), QStringLiteral("用户9000"), 3200,
                                 QStringLiteral("frozen"), QStringLiteral("2026-08-02T11:40:00Z")});
    result.items.append(UserInfo{3, QStringLiteral("13700137000"), QStringLiteral("用户7000"), 0,
                                 QStringLiteral("active"), QStringLiteral("2026-08-28T06:05:00Z")});
    return result;
}

OverviewResult overview(DataMode mode)
{
    OverviewResult result;
    if (mode == DataMode::Error) {
        result.ok = false;
        result.error = kSimulatedError;
        return result;
    }
    if (mode != DataMode::Normal)
        return result; // Empty：ok=true，指标全零

    result.stats.revenueCents = 286540;             // 2865.40 元（近 7 日）
    result.stats.pileIdle = 1;
    result.stats.pileReserved = 1;
    result.stats.pileCharging = 2;
    result.stats.pileFault = 1;
    result.stats.pileOffline = 1;
    result.stats.avgStationUtilization = 0.42;      // 42%
    result.stats.updatedAt = QStringLiteral("2026-09-01T10:15:00Z");
    result.hasData = true;
    return result;
}

} // namespace mockdata
} // namespace ev
