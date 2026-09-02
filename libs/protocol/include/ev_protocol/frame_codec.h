#pragma once

#include "ev_protocol/message.h"

#include <QByteArray>

namespace ev::protocol {

QByteArray encodeFrame(const Message &message);

class FrameDecoder {
public:
    // Appends bytes from a TCP read. Complete messages are returned in order.
    QList<Message> feed(const QByteArray &bytes, QString *error = nullptr,
                        ErrorCode *errorCode = nullptr);
    void reset();

private:
    QByteArray buffer_;
};

} // namespace ev::protocol
