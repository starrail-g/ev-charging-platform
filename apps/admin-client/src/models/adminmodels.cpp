#include "adminmodels.h"

namespace ev {

PileStatus parsePileStatus(const QString &status)
{
    if (status == QStringLiteral("idle")) return PileStatus::Idle;
    if (status == QStringLiteral("reserved")) return PileStatus::Reserved;
    if (status == QStringLiteral("charging")) return PileStatus::Charging;
    if (status == QStringLiteral("fault")) return PileStatus::Fault;
    if (status == QStringLiteral("offline")) return PileStatus::Offline;
    return PileStatus::Unknown;
}

QString pileStatusToDisplay(PileStatus status)
{
    switch (status) {
    case PileStatus::Idle: return QStringLiteral("空闲");
    case PileStatus::Reserved: return QStringLiteral("已预约");
    case PileStatus::Charging: return QStringLiteral("充电中");
    case PileStatus::Fault: return QStringLiteral("故障");
    case PileStatus::Offline: return QStringLiteral("离线");
    case PileStatus::Unknown: return QStringLiteral("未知");
    }
    return QStringLiteral("未知");
}

QString pileStatusToProtocol(PileStatus status)
{
    switch (status) {
    case PileStatus::Idle: return QStringLiteral("idle");
    case PileStatus::Reserved: return QStringLiteral("reserved");
    case PileStatus::Charging: return QStringLiteral("charging");
    case PileStatus::Fault: return QStringLiteral("fault");
    case PileStatus::Offline: return QStringLiteral("offline");
    case PileStatus::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

} // namespace ev
