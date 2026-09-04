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

QString formatChargeDuration(int totalSeconds)
{
    if (totalSeconds < 0)
        totalSeconds = 0;
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
}

// 金额格式：整数分 → "¥2,865.40"（千分位、无浮点漂移；口径同 Web formatCents）。
// 评审 P2-01：负值/零值/大额统一走绝对值运算、负号前置（-¥0.01 不丢符号）。
QString formatYuanCents(qint64 cents)
{
    const bool negative = cents < 0;
    const qint64 absCents = qAbs(cents);
    const QString frac = QString::number(absCents % 100).rightJustified(2, QLatin1Char('0'));
    const QString digits = QString::number(absCents / 100);
    QString grouped;
    const int length = digits.size();
    for (int i = 0; i < length; ++i) {
        if (i > 0 && (length - i) % 3 == 0)
            grouped += QLatin1Char(',');
        grouped += digits.at(i);
    }
    // 负号置于货币符号前（-¥0.01），与中文展示惯例一致
    return QStringLiteral("%1¥%2.%3").arg(negative ? QStringLiteral("-") : QString(), grouped, frac);
}

} // namespace ev
