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
#include <optional>

namespace blokkily::mcp {

// The protocol revisions this server can speak, newest first. A client asking
// for one of them gets it; any other request gets the newest.
inline constexpr const char* protocol_versions[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

// Where the running window listens: BLOKKILY_MCP_SOCKET when it is set, else
// blokkily-mcp.sock in the user's runtime directory (a named pipe called
// blokkily-mcp on Windows).
QString socketPath();

// Every tool the server offers, as tools/list returns them.
QJsonArray toolDefinitions();

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
