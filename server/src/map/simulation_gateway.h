#pragma once

#include "ev_database/database.h"
#include "ev_protocol/message.h"

namespace ev::server::map {

class SimulationGateway final {
public:
    explicit SimulationGateway(ev::database::Database *database);

    // Internal-only boundary. The public client TCP dispatcher must not route
    // arbitrary user messages here; production callers authenticate the
    // simulator service with mTLS/service identity before invoking it.
    bool apply(const QJsonObject &proposal, QJsonObject *response,
               ev::protocol::ErrorCode *code, QString *error);

private:
    ev::database::Database *database_ = nullptr;
};

} // namespace ev::server::map
