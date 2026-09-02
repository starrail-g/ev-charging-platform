#include "mockdataset.h"

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

    result.items.append(PileInfo{1, 1, QStringLiteral("P-101-A"), QStringLiteral("fast"),
                                 120.0, 120, PileStatus::Charging});
    result.items.append(PileInfo{2, 1, QStringLiteral("P-101-B"), QStringLiteral("fast"),
                                 120.0, 120, PileStatus::Idle});
    result.items.append(PileInfo{3, 1, QStringLiteral("P-101-C"), QStringLiteral("slow"),
                                 60.0, 90, PileStatus::Fault});
    result.items.append(PileInfo{4, 2, QStringLiteral("P-202-A"), QStringLiteral("fast"),
                                 150.0, 150, PileStatus::Charging});
    result.items.append(PileInfo{5, 2, QStringLiteral("P-202-B"), QStringLiteral("fast"),
                                 150.0, 150, PileStatus::Reserved});
    result.items.append(PileInfo{6, 2, QStringLiteral("P-202-C"), QStringLiteral("slow"),
                                 60.0, 90, PileStatus::Offline});
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

    result.items.append(StationInfo{1, QStringLiteral("东软园区充电站"), QStringLiteral("沈阳市浑南区东软软件园"),
                                    41.7331, 123.4395, QStringLiteral("active"), 3});
    result.items.append(StationInfo{2, QStringLiteral("沈阳站前充电站"), QStringLiteral("沈阳市和平区胜利南街"),
                                    41.7923, 123.3942, QStringLiteral("active"), 3});
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

    result.items.append(UserInfo{1, QStringLiteral("13800138000"), QStringLiteral("用户8000"), 16950,
                                 QStringLiteral("active")});
    result.items.append(UserInfo{2, QStringLiteral("13900139000"), QStringLiteral("用户9000"), 3200,
                                 QStringLiteral("frozen")});
    result.items.append(UserInfo{3, QStringLiteral("13700137000"), QStringLiteral("用户7000"), 0,
                                 QStringLiteral("active")});
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
