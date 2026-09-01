#include "ev_protocol/frame_codec.h"

#include <QJsonDocument>

namespace ev::protocol {

QByteArray encodeFrame(const Message &message)
{
    const QByteArray payload = QJsonDocument(message.toJson()).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.reserve(4 + payload.size());
    const quint32 size = static_cast<quint32>(payload.size());
    frame.append(char((size >> 24) & 0xff));
    frame.append(char((size >> 16) & 0xff));
    frame.append(char((size >> 8) & 0xff));
    frame.append(char(size & 0xff));
    frame.append(payload);
    return frame;
}

QList<Message> FrameDecoder::feed(const QByteArray &bytes, QString *error,
                                  ErrorCode *errorCode)
{
    QList<Message> messages;
    if (error) error->clear();
    if (errorCode) *errorCode = ErrorCode::Ok;
    buffer_.append(bytes);

    while (buffer_.size() >= 4) {
        const auto *raw = reinterpret_cast<const uchar *>(buffer_.constData());
        const quint32 size = (quint32(raw[0]) << 24) | (quint32(raw[1]) << 16)
                             | (quint32(raw[2]) << 8) | quint32(raw[3]);
        if (size == 0 || size > kMaxPayloadBytes) {
            if (error) *error = QStringLiteral("invalid payload length: %1").arg(size);
            if (errorCode) *errorCode = ErrorCode::InvalidFrame;
            reset();
            return messages;
        }
        if (buffer_.size() < 4 + qint64(size)) return messages;

        const QByteArray payload = buffer_.mid(4, size);
        buffer_.remove(0, 4 + size);
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        Message message;
        QString messageError;
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
            || !Message::fromJson(document.object(), &message, &messageError)) {
            if (error) *error = messageError.isEmpty() ? parseError.errorString() : messageError;
            if (errorCode) {
                if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                    *errorCode = ErrorCode::InvalidJson;
                } else if (messageError == QStringLiteral("unsupported protocol version")) {
                    *errorCode = ErrorCode::UnsupportedVersion;
                } else {
                    *errorCode = ErrorCode::InvalidRequest;
                }
            }
            reset();
            return messages;
        }
        messages.append(message);
    }
    return messages;
}

void FrameDecoder::reset()
{
    buffer_.clear();
}

} // namespace ev::protocol
