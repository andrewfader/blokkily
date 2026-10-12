#pragma once

// The running window's end of MCP (features/mcp.feature): a local socket that
// answers one JSON-RPC message per line with blokkily::mcp::Session, running
// each tool on the GUI thread through McpTools. The socket is the user's own
// (a runtime-directory path, or a named pipe on Windows), not a network port.

#include "mcp_protocol.hpp"
#include "mcp_tools.hpp"

#include <QLocalServer>
#include <QObject>
#include <QString>

class McpServer final : public QObject {
    Q_OBJECT

public:
    explicit McpServer(McpTools& tools, QObject* parent = nullptr);

    // Listens at `path`, which this window claimed with `lock`. Without the
    // lock another Blokkily has the socket, and false is returned. With it, a
    // socket left behind by a Blokkily that has gone is taken over; one an
    // older Blokkily (from before the lock) still answers on is left to it.
    bool listen(const QString& path, const blokkily::mcp::SocketLock& lock);
    QString path() const { return server_.fullServerName(); }
    QString error() const { return error_; }

private:
    void accept();

    McpTools& tools_;
    blokkily::mcp::Session session_;
    QLocalServer server_;
    QString error_;
};
