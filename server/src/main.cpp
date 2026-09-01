#include "ev_protocol/frame_codec.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QTcpSocket>

#include <limits>

using namespace ev::protocol;

class ClientConnection final : public QObject {
public:
    explicit ClientConnection(QTcpSocket *socket, QObject *parent = nullptr)
        : QObject(parent), socket_(socket)
    {
        socket_->setParent(this);
        connect(socket_, &QTcpSocket::readyRead, this, [this] { readAvailable(); });
        connect(socket_, &QTcpSocket::disconnected, this, &QObject::deleteLater);
        connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            qWarning() << "socket error" << socket_->errorString();
        });
    }

private:
    void readAvailable()
    {
        QString error;
        ErrorCode errorCode = ErrorCode::Ok;
        const QList<Message> requests = decoder_.feed(socket_->readAll(), &error, &errorCode);
        for (const Message &request : requests) handle(request);
        if (!error.isEmpty()) {
            sendError(QString(), errorCode, error);
            socket_->disconnectFromHost();
        }
    }

    void handle(const Message &request)
    {
        qInfo() << "request" << request.id << request.type;
        if (request.type == QStringLiteral("health")) {
            sendResponse(request, QStringLiteral("health.result"),
                         QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")},
                                     {QStringLiteral("service"), QStringLiteral("ev-server")}});
        } else if (request.type == QStringLiteral("echo")) {
            sendResponse(request, QStringLiteral("echo.result"), request.payload);
        } else {
            sendError(request.id, ErrorCode::InvalidRequest,
                      QStringLiteral("unsupported request type: %1").arg(request.type));
        }
    }

    void sendResponse(const Message &request, const QString &type, const QJsonObject &payload)
    {
        socket_->write(encodeFrame(Message{kProtocolVersion, request.id, type, payload}));
    }

    void sendError(const QString &id, ErrorCode code, const QString &message)
    {
        socket_->write(encodeFrame(Message{kProtocolVersion, id.isEmpty() ? QStringLiteral("server") : id,
                                           QStringLiteral("error"), errorPayload(code, message)}));
    }

    QTcpSocket *socket_;
    FrameDecoder decoder_;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ev-server"));
    QTcpServer server;
    const QHostAddress host(qEnvironmentVariable("EV_SERVER_HOST", QStringLiteral("127.0.0.1")));
    bool portOk = false;
    const int configuredPort = qEnvironmentVariableIntValue("EV_SERVER_PORT", &portOk);
    if (qEnvironmentVariableIsSet("EV_SERVER_PORT")
        && (!portOk || configuredPort < 1
            || configuredPort > std::numeric_limits<quint16>::max())) {
        qCritical() << "EV_SERVER_PORT must be an integer between 1 and 65535";
        return 1;
    }
    const quint16 listenPort = portOk ? static_cast<quint16>(configuredPort) : 45454;
    if (!server.listen(host, listenPort)) {
        qCritical() << "listen failed" << host.toString() << listenPort << server.errorString();
        return 1;
    }
    qInfo() << "ev-server listening on" << host.toString() << listenPort;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
        while (server.hasPendingConnections())
            new ClientConnection(server.nextPendingConnection(), &server);
    });
    return app.exec();
}
