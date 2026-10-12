#include "mcp_server.hpp"

#include <QLocalSocket>

McpServer::McpServer(McpTools& tools, QObject* parent)
    : QObject(parent), tools_(tools),
      session_([this](const QString& name, const QJsonObject& arguments) {
          return tools_.call(name, arguments);
      }) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    QObject::connect(&server_, &QLocalServer::newConnection, this, &McpServer::accept);
}

bool McpServer::listen(const QString& path, const blokkily::mcp::SocketLock& lock) {
    if (!lock.held()) {
        error_ = QStringLiteral("another Blokkily is serving %1").arg(path);
        return false;
    }
    if (server_.listen(path)) return true;
    if (server_.serverError() == QAbstractSocket::AddressInUseError) {
        QLocalSocket probe;
        probe.connectToServer(path);
        if (probe.waitForConnected(300)) {
            error_ = QStringLiteral("another Blokkily is serving %1").arg(path);
            return false;
        }
        QLocalServer::removeServer(path);
        if (server_.listen(path)) return true;
    }
    error_ = server_.errorString();
    return false;
}

void McpServer::accept() {
    while (QLocalSocket* socket = server_.nextPendingConnection()) {
        QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        // A message may arrive in pieces, or several in one read; each whole
        // line is answered in order.
        QObject::connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            while (socket->canReadLine()) {
                const QByteArray line = socket->readLine();
                if (const auto reply = session_.handleLine(line)) {
                    socket->write(*reply);
                    socket->write("\n");
                }
            }
            socket->flush();
        });
    }
}
