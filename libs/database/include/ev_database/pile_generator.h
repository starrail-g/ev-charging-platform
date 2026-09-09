#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace ev::database {

struct GeneratedPile {
    QString type;
    int powerKw = 0;
    int priceFenPerKwh = 0;
    QString status;
};

// The generator is deliberately independent of SQLite and of the Qt random
// APIs. It is shared by the server importer and the cloud simulator tests so
// that another language can reproduce the exact same sequence.
class PileGenerator final {
public:
    static QByteArray digest(const QString &seedId, const QString &provider,
                             const QString &providerPoiId);

    static QVector<GeneratedPile> generate(const QString &seedId,
                                           const QString &provider,
                                           const QString &providerPoiId,
                                           bool activeStation = true);

    // Returns the transition proposed for one eligible pile. The caller is
    // responsible for applying the station minimum-idle invariant to a full
    // batch after all piles have been evaluated.
    static QString proposeTransition(const QString &seedId, quint64 tickId,
                                     quint64 pileId, const QString &currentStatus,
                                     quint32 *draw = nullptr);
};

} // namespace ev::database
