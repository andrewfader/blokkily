#include "mcp_server.hpp"

#include <QLocalSocket>
#include <QTimer>

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
        // A long tool (a render) turns the event loop while it works, so an
        // agent can hang up while its request is being answered. Its socket
        // is then still on the stack, in serve() and in the signal that
        // called it, and is deleted only once serve() has returned.
        QObject::connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            if (serving_.contains(socket)) hung_up_.insert(socket);
            else socket->deleteLater();
        });
        // A message may arrive in pieces, or several in one read; each whole
        // line is answered in order.
        QObject::connect(socket, &QLocalSocket::readyRead, this, [this, socket] { serve(socket); });
    }
}

void McpServer::serve(QLocalSocket* socket) {
    if (serving_.contains(socket)) return;
    serving_.insert(socket);
    while (!held_ && !hung_up_.contains(socket) && socket->canReadLine()) {
        const QByteArray line = socket->readLine();
        const auto reply = session_.handleLine(line);
        // An agent that hung up meanwhile is owed nothing.
        if (reply && !hung_up_.contains(socket)) {
            socket->write(*reply);
            socket->write("\n");
        }
    }
    serving_.remove(socket);
    if (hung_up_.remove(socket)) {
        socket->deleteLater();
        return;
    }
    socket->flush();
}

void McpServer::setHeld(bool held) {
    if (held_ == held) return;
    held_ = held;
    if (held) return;
    // Next turn: the request that held them may still be on the stack, and
    // its reply goes first.
    QTimer::singleShot(0, this, [this] {
        for (auto* socket : server_.findChildren<QLocalSocket*>()) serve(socket);
    });
}
