#include "mockdataset.h"

#include <QDate>
#include <QHash>

namespace ev {
namespace mockdata {

namespace {
const QString kSimulatedError = QStringLiteral("mock: simulated interface error");

// 与 dashboard/data/demo.json revenue30dCents 逐值一致（30 元素，末 7 位 = 7d 数组；
// 和 = 983840 / 286540 分）。运行时不读取 dashboard 目录，双端同口径由测试锁定。
const QList<qint64> kRevenueCents30 = {
    25400, 27100, 28900, 26600, 31200, 33500, 29800, 24300, 26200, 28100,
    30500, 32800, 35200, 31400, 27500, 29600, 31800, 34300, 36900, 33100,
    28800, 30900, 33400, 35600, 41200, 38240, 44700, 46800, 40200, 39800};

RevenueSeries mockRevenue(int count)
{
    RevenueSeries result;
    result.range = count == 7 ? QStringLiteral("7d") : QStringLiteral("30d");
    result.updatedAt = QStringLiteral("2026-09-01T10:15:00Z");
    const QDate end(2026, 9, 1);
    for (int i = 30 - count; i < 30; ++i) {
        result.days.append({end.addDays(i - 29), kRevenueCents30.at(i)});
        result.totalCents += kRevenueCents30.at(i);
    }
    result.available = true;
    return result;
}
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

    // 两份逐日序列来自同一 30 日源（末 7 日派生 7 日序列，同 demo.json）；
    // 卡面合计从 totalCents 派生，避免三份手写合计漂移（tst_ui 锁定一致）。
    result.stats.revenue7dSeries = mockRevenue(7);
    result.stats.revenue30dSeries = mockRevenue(30);
    result.stats.revenueCents = result.stats.revenue7dSeries.totalCents;    // 2865.40 元（近 7 日）
    result.stats.revenue30dCents = result.stats.revenue30dSeries.totalCents; // 9838.40 元（近 30 日）
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
