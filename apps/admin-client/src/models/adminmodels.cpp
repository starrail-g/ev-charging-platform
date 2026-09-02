#include "adminmodels.h"

namespace ev {

PileStatusParse parsePileStatus(const QString &status)
{
    if (status == QStringLiteral("idle")) return PileStatusParse::Idle;
    if (status == QStringLiteral("reserved")) return PileStatusParse::Reserved;
    if (status == QStringLiteral("charging")) return PileStatusParse::Charging;
    if (status == QStringLiteral("fault")) return PileStatusParse::Fault;
    if (status == QStringLiteral("offline")) return PileStatusParse::Offline;
    return PileStatusParse::Unknown;
}

QString pileStatusToDisplay(PileStatus status)
{
    switch (status) {
    case PileStatus::Idle: return QStringLiteral("空闲");
    case PileStatus::Reserved: return QStringLiteral("已预约");
    case PileStatus::Charging: return QStringLiteral("充电中");
    case PileStatus::Fault: return QStringLiteral("故障");
    case PileStatus::Offline: return QStringLiteral("离线");
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
    }
    return QString();
}

} // namespace ev
