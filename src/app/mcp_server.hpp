#pragma once

// The running window's end of MCP (features/mcp.feature): a local socket that
// answers one JSON-RPC message per line with blokkily::mcp::Session, running
// each tool on the GUI thread through McpTools. The socket is the user's own
// (a runtime-directory path, or a named pipe on Windows), not a network port.

#include "mcp_protocol.hpp"
#include "mcp_tools.hpp"

#include <QLocalServer>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <optional>

class McpServer final : public QObject {
    Q_OBJECT

public:
    explicit McpServer(McpTools& tools, QObject* parent = nullptr);

    // Listens at `path`, which this window claimed with `lock`. Without the
    // lock another Blokkily has the socket, and false is returned. With it, a
    // socket left behind by a Blokkily that has gone is taken over; one an
    // older Blokkily (from before the lock) still answers on is left to it.
    bool listen(const QString& path, const blokkily::mcp::SocketLock& lock);
    // For a window that is not the one serving `path`
    // (features/multiple_windows.feature): claims the socket's lock as soon
    // as the serving window lets it go, every `retry_ms`, and listens then,
    // so closing the first window never ends MCP for the others. True when
    // it serves at once.
    bool serveWhenFree(const QString& path, int retry_ms = 500);
    bool listening() const { return server_.isListening(); }
    QString path() const { return server_.fullServerName(); }
    QString error() const { return error_; }
    // While held, requests wait in their sockets unread; released, they are
    // answered in the order they came, after whatever held them has returned.
    // A load the window is showing holds them (load_progress.hpp), so an
    // agent never edits a song half way through opening.
    void setHeld(bool held);
    // Whether the agent whose request is being answered has hung up: a
    // render it asked for is then abandoned (AppController::setRenderCancel).
    bool abandoned() const { return current_ != nullptr && hung_up_.contains(current_); }

signals:
    // Listening began at `path` after waiting for another window to let it go.
    void tookOver(const QString& path);

private:
    void accept();
    void serve(class QLocalSocket* socket);

    McpTools& tools_;
    blokkily::mcp::Session session_;
    // Sockets whose requests are being answered, and those of them whose
    // agent hung up meanwhile. Declared before the server, whose sockets
    // still say they disconnected while it is destroyed.
    QSet<class QLocalSocket*> serving_;
    QSet<class QLocalSocket*> hung_up_;
    // The socket whose request is being answered now.
    class QLocalSocket* current_ = nullptr;
    QLocalServer server_;
    QString error_;
    bool held_ = false;
    // The lock a window that took the socket over holds, and the retry that
    // waits for it.
    std::optional<blokkily::mcp::SocketLock> takeover_lock_;
    QTimer takeover_;
};
