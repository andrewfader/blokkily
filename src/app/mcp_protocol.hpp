#pragma once

// The Model Context Protocol as Blokkily speaks it (features/mcp.feature): an
// agent drives the running application through tools, the way the window's
// own controls drive it. Messages are JSON-RPC 2.0, one per line, which is
// MCP's stdio framing; the running window serves them on a local socket
// (McpServer) and `blokkily --mcp` carries them between an agent's stdio and
// that socket (mcp_bridge.cpp).
//
// Qt Core only, so the bridge can answer the handshake and list the tools
// without a window, a display or the audio engine.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QLockFile;

namespace blokkily::mcp {

// The protocol revisions this server can speak, newest first. A client asking
// for one of them gets it; any other request gets the newest.
inline constexpr const char* protocol_versions[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

// Where the running window listens: BLOKKILY_MCP_SOCKET when it is set, else
// blokkily-mcp.sock in the user's runtime directory (a named pipe called
// blokkily-mcp on Windows).
QString socketPath();

// Which window serves a socket: the one holding the lock file beside it.
// Taking a lock is one atomic step, where knocking on the socket and then
// replacing it is two, so two windows starting together cannot both decide
// the socket is free, and a window too busy to answer still keeps it. A lock
// left by a window that has gone is taken over (its process is checked).
class SocketLock {
public:
    explicit SocketLock(const QString& socket);
    ~SocketLock();
    SocketLock(const SocketLock&) = delete;
    SocketLock& operator=(const SocketLock&) = delete;

    bool held() const { return held_; }
    // Whether a running process holds the lock for `socket`: a window that is
    // serving it, or starting up and about to.
    static bool taken(const QString& socket);

private:
    std::unique_ptr<QLockFile> lock_;
    bool held_ = false;
};

// Every tool the server offers, as tools/list returns them.
QJsonArray toolDefinitions();

// Why `arguments` do not fit a tool's input `schema`, or empty when they do:
// each argument the schema types must have that type (an integer a whole
// number), an enum's value must be one of its choices, and the objects in an
// array whose items are objects are checked the same way. `where` prefixes
// the names in the message.
QString argumentProblem(const QJsonObject& schema, const QJsonObject& arguments,
                        const QString& where = {});

// What the server tells an agent about driving it, sent with initialize.
QString instructions();

// A tool's result: what tools/call returns as its `result`.
QJsonObject textResult(const QString& text, bool error = false);
QJsonObject jsonResult(const QJsonValue& value);

// Runs one tool; returns the tools/call result. Errors a tool reports (a
// bad argument, a refused edit) are results with isError set, as MCP asks.
using ToolRunner = std::function<QJsonObject(const QString& name, const QJsonObject& arguments)>;

// One side of a connection: answers each message it is handed. Requests get
// a response; notifications (no id) get none.
class Session {
public:
    explicit Session(ToolRunner run_tool) : run_tool_(std::move(run_tool)) {}

    // `line` is one framed message; the reply is one line without its newline,
    // or nothing for a notification.
    std::optional<QByteArray> handleLine(const QByteArray& line);
    std::optional<QJsonObject> handle(const QJsonObject& message);

private:
    ToolRunner run_tool_;
};

// The JSON-RPC error object for a request that could not be answered.
QJsonObject errorResponse(const QJsonValue& id, int code, const QString& message);

}  // namespace blokkily::mcp
