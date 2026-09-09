#pragma once

#include "ev_database/database.h"
#include "ev_protocol/message.h"
#include "tencent_client.h"

#include <QJsonObject>

namespace ev::server::map {

struct MapFailure {
    ev::protocol::ErrorCode code = ev::protocol::ErrorCode::InternalError;
    QString message;
};

class MapService final {
public:
    explicit MapService(ev::database::Database *database);

    bool stationSearch(const QString &requestId, qint64 userId,
                       const QJsonObject &payload, QJsonObject *response,
                       MapFailure *failure);
    bool routePlan(const QString &requestId, qint64 userId,
                   const QJsonObject &payload, QJsonObject *response,
                   MapFailure *failure);
    bool listAudit(const QString &operation, const QString &resultStatus,
                   const QString &pageToken, qint64 limit,
                   QJsonObject *response, MapFailure *failure);

    static QString encodePageToken(const QString &binding, qint64 position);
    static bool decodePageToken(const QString &token, const QString &binding,
                                qint64 *position);

private:
    static bool parseOrigin(const QJsonObject &payload, QJsonObject *origin,
                            QString *address, QString *error);
    static QString queryBinding(const QJsonObject &query);
    static QString cacheKey(const QString &operation, const QString &binding);
    static bool isMapMockEnabled();
    static void setFailure(MapFailure *failure, ev::protocol::ErrorCode code,
                           const QString &message);
    static QVector<ev::database::MapPoi> parseCachedPois(const QJsonObject &payload);
    static QJsonObject mapOnlyPayload(const QString &provider,
                                      const QString &providerSource,
                                      const QJsonObject &origin,
                                      const QVector<ev::database::MapPoi> &pois,
                                      bool hasMore, const QString &nextToken);
    static QString encodeAuditPageToken(const QString &binding,
                                        const QString &createdAt, qint64 id);
    static bool decodeAuditPageToken(const QString &token, const QString &binding,
                                     QString *createdAt, qint64 *id);

    ev::database::Database *database_ = nullptr;
    DeterministicTencentClient deterministicClient_;
};

} // namespace ev::server::map
