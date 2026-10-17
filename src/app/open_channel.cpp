#include "open_channel.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

namespace open_channel {

QString socketPath() {
    const auto chosen = qEnvironmentVariable("BLOKKILY_OPEN_SOCKET");
    if (!chosen.isEmpty()) return chosen;
#ifdef Q_OS_WIN
    return QStringLiteral("blokkily-open");
#else
    QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (directory.isEmpty()) directory = QDir::tempPath();
    return QDir(directory).filePath(QStringLiteral("blokkily-open.sock"));
#endif
}

QString fileSocketPath(const QString& file) {
    QFileInfo info(absoluteFile(file));
    QString key = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    // Short, as a Unix socket's whole path must fit in 107 bytes.
    const auto hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    QString base = socketPath();
    if (base.endsWith(QStringLiteral(".sock"))) base.chop(5);
    return base + QLatin1Char('-') + QString::fromLatin1(hash) +
           (socketPath().endsWith(QStringLiteral(".sock")) ? QStringLiteral(".sock") : QString());
}

QString absoluteFile(const QString& argument) {
    const QUrl url(argument);
    const QString local = url.isLocalFile() ? url.toLocalFile() : argument;
    return QFileInfo(local).absoluteFilePath();
}

bool forward(const QString& socket, const QString& file, int timeout_ms) {
    QElapsedTimer clock;
    clock.start();
    // No live window owns the channel: this launch is the first.
    if (!blokkily::mcp::SocketLock::taken(socket)) return false;
    // One does, though it may still be coming up: try until it answers.
    while (clock.elapsed() < timeout_ms) {
        QLocalSocket client;
        client.connectToServer(socket);
        if (client.waitForConnected(500)) {
            client.write((file + QLatin1Char('\n')).toUtf8());
            if (!client.waitForBytesWritten(2000)) return false;
            // The window answers once it has the path.
            while (!client.canReadLine() && client.waitForReadyRead(2000)) {}
            return client.readLine().trimmed() == "ok";
        }
        if (!blokkily::mcp::SocketLock::taken(socket)) return false;
        QThread::msleep(100);
    }
    return false;
}

Server::Server(QObject* parent) : QObject(parent) {
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (auto* client = server_.nextPendingConnection()) {
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                while (client->canReadLine()) {
                    const auto path = QString::fromUtf8(client->readLine()).trimmed();
                    if (path.isEmpty()) continue;
                    client->write("ok\n");
                    client->flush();
                    emit fileRequested(path);
                }
            });
        }
    });
}

Server::~Server() = default;

void Server::close() {
    server_.close();
    lock_.reset();
}

bool Server::listen(const QString& socket, QString* error) {
    close();
    lock_.emplace(socket);
    if (!lock_->held()) {
        lock_.reset();
        if (error != nullptr) *error = QStringLiteral("another window owns %1").arg(socket);
        return false;
    }
    // Whatever is at the path was left by a window that is gone: the lock
    // is ours.
    QLocalServer::removeServer(socket);
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_.listen(socket)) {
        if (error != nullptr) *error = server_.errorString();
        lock_.reset();
        return false;
    }
    return true;
}

} // namespace open_channel
