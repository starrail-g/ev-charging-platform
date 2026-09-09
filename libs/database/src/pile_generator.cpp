#include "ev_database/pile_generator.h"

#include <QCryptographicHash>

#include <cmath>

namespace ev::database {
namespace {

class Pcg32 final {
public:
    explicit Pcg32(const QByteArray &seed)
    {
        const QByteArray digest = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
        quint64 initState = 0;
        quint64 initSeq = 0;
        for (int i = 0; i < 8; ++i) {
            initState |= quint64(static_cast<uchar>(digest.at(i))) << (8 * i);
            initSeq |= quint64(static_cast<uchar>(digest.at(8 + i))) << (8 * i);
        }
        state_ = 0;
        increment_ = (initSeq << 1) | 1ULL;
        next32();
        state_ += initState;
        next32();
    }

    quint32 next32()
    {
        const quint64 old = state_;
        state_ = old * 6364136223846793005ULL + increment_;
        const quint32 xorshifted = static_cast<quint32>(((old >> 18U) ^ old) >> 27U);
        const quint32 rotation = static_cast<quint32>(old >> 59U);
        return (xorshifted >> rotation)
            | (xorshifted << ((32U - rotation) & 31U));
    }

    quint32 below(quint32 n)
    {
        if (n == 0) return 0;
        const quint64 threshold = (0x100000000ULL - n) % n;
        quint32 value = 0;
        do {
            value = next32();
        } while (value < threshold);
        return value % n;
    }

private:
    quint64 state_ = 0;
    quint64 increment_ = 1;
};

QByteArray generationInput(const QString &seedId, const QString &provider,
                           const QString &poi)
{
    return seedId.normalized(QString::NormalizationForm_C).toUtf8()
        + QByteArray(1, '\0')
        + provider.normalized(QString::NormalizationForm_C).toUtf8()
        + QByteArray(1, '\0')
        + poi.normalized(QString::NormalizationForm_C).toUtf8();
}

QByteArray transitionInput(const QString &seedId, quint64 tickId, quint64 pileId)
{
    return seedId.normalized(QString::NormalizationForm_C).toUtf8()
        + QByteArray(1, '\0')
        + QByteArray::number(tickId)
        + QByteArray(1, '\0')
        + QByteArray::number(pileId);
}

} // namespace

QByteArray PileGenerator::digest(const QString &seedId, const QString &provider,
                                 const QString &providerPoiId)
{
    return QCryptographicHash::hash(generationInput(seedId, provider, providerPoiId),
                                    QCryptographicHash::Sha256);
}

QVector<GeneratedPile> PileGenerator::generate(const QString &seedId,
                                               const QString &provider,
                                               const QString &providerPoiId,
                                               bool activeStation)
{
    Pcg32 random(generationInput(seedId, provider, providerPoiId));
    const int count = 4 + static_cast<int>(random.below(9));
    const int roundedFast = (count * 60 + 50) / 100;
    const int fastCount = qBound(1, roundedFast, count - 1);
    static constexpr int fastPower[] = {60, 120, 180};
    static constexpr int slowPower[] = {7, 11, 22};
    static constexpr int basePrices[] = {90, 110, 130};

    QVector<GeneratedPile> result;
    result.reserve(count);
    bool hasIdle = false;
    for (int sequence = 1; sequence <= count; ++sequence) {
        const bool fast = sequence <= fastCount;
        const int powerIndex = static_cast<int>(random.below(3));
        const int priceIndex = static_cast<int>(random.below(3));
        const int statusBucket = static_cast<int>(random.below(10));
        QString status;
        if (statusBucket <= 7) status = QStringLiteral("idle");
        else if (statusBucket == 8) status = QStringLiteral("fault");
        else status = QStringLiteral("offline");
        if (status == QStringLiteral("idle")) hasIdle = true;
        result.append(GeneratedPile{fast ? QStringLiteral("fast") : QStringLiteral("slow"),
                                    fast ? fastPower[powerIndex] : slowPower[powerIndex],
                                    basePrices[priceIndex] + (fast ? 20 : 0), status});
    }
    if (activeStation && !hasIdle && !result.isEmpty()) result.last().status = QStringLiteral("idle");
    return result;
}

QString PileGenerator::proposeTransition(const QString &seedId, quint64 tickId,
                                         quint64 pileId, const QString &currentStatus,
                                         quint32 *draw)
{
    Pcg32 random(transitionInput(seedId, tickId, pileId));
    const quint32 value = random.below(100);
    if (draw) *draw = value;
    if (currentStatus == QStringLiteral("idle")) {
        if (value < 90) return QStringLiteral("idle");
        if (value < 95) return QStringLiteral("fault");
        return QStringLiteral("offline");
    }
    if (currentStatus == QStringLiteral("fault")) {
        if (value < 60) return QStringLiteral("fault");
        if (value < 95) return QStringLiteral("idle");
        return QStringLiteral("offline");
    }
    if (currentStatus == QStringLiteral("offline")) {
        if (value < 60) return QStringLiteral("offline");
        if (value < 95) return QStringLiteral("idle");
        return QStringLiteral("fault");
    }
    return currentStatus;
}

} // namespace ev::database
