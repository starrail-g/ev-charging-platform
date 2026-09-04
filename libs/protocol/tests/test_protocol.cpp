#include "ev_protocol/frame_codec.h"

#include <QCoreApplication>
#include <QDebug>

using namespace ev::protocol;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Message source{1, QStringLiteral("request-1"), QStringLiteral("echo"),
                   QJsonObject{{QStringLiteral("text"), QStringLiteral("hello")}}};
    const QByteArray frame = encodeFrame(source);
    FrameDecoder decoder;
    QString error;
    ErrorCode errorCode = ErrorCode::Ok;
    QList<Message> result = decoder.feed(frame.left(2), &error);
    if (!result.isEmpty() || !error.isEmpty()) return 1;
    result = decoder.feed(frame.mid(2), &error);
    if (result.size() != 1 || result.first().id != source.id
        || result.first().payload != source.payload || !error.isEmpty()) return 2;
    result = decoder.feed(frame + frame, &error);
    if (result.size() != 2 || !error.isEmpty()) return 3;
    const QByteArray invalid("\x00\x10\x00\x01", 4);
    decoder.feed(invalid, &error, &errorCode);
    if (error.isEmpty() || errorCode != ErrorCode::InvalidFrame) return 4;
    const QByteArray badJson("\x00\x00\x00\x01!", 5);
    decoder.feed(badJson, &error, &errorCode);
    if (error.isEmpty() || errorCode != ErrorCode::InvalidJson) return 5;
    Message unsupported = source;
    unsupported.version = 2;
    decoder.feed(encodeFrame(unsupported), &error, &errorCode);
    if (error.isEmpty() || errorCode != ErrorCode::UnsupportedVersion) return 6;
    Message good{1, QStringLiteral("good-before-bad"), QStringLiteral("echo"),
                 QJsonObject{{QStringLiteral("value"), 42}}};
    const QByteArray badBatch = encodeFrame(good) + QByteArray("\x00\x00\x00\x01!", 5);
    result = decoder.feed(badBatch, &error, &errorCode);
    if (result.size() != 1 || result.first().id != good.id
        || error.isEmpty() || errorCode != ErrorCode::InvalidJson) return 7;
    if (errorCodeName(ErrorCode::AccountFrozen) != QStringLiteral("ACCOUNT_FROZEN")) return 8;
    qInfo() << "protocol tests passed";
    return 0;
}
