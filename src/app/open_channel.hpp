#pragma once

// One window opens the files the desktop hands Blokkily
// (features/open_in_running_window.feature). The first window to start
// listens on a local socket; a later `blokkily song.blok` finds it, hands
// over the file's absolute path, and exits without opening a window of its
// own. A lock beside the socket says a live window owns it, so a socket a
// crashed window left behind is taken over rather than written to.

#include "mcp_protocol.hpp"

#include <QLocalServer>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace open_channel {

// $BLOKKILY_OPEN_SOCKET, else blokkily-open.sock in the runtime folder.
[[nodiscard]] QString socketPath();

// The absolute local path a launch argument names: a path relative to the
// working folder, or a file:// URL.
[[nodiscard]] QString absoluteFile(const QString& argument);

// Hands `file` to the window that owns `socket`, waiting at most
// `timeout_ms` for one that is still starting. True when that window took
// it; false when no window owns the socket (this launch opens its own).
[[nodiscard]] bool forward(const QString& socket, const QString& file, int timeout_ms = 5000);

// The owning window's end. Each file a later launch hands over arrives as
// fileRequested(path), on the thread that owns the server.
class Server final : public QObject {
    Q_OBJECT
public:
    explicit Server(QObject* parent = nullptr);
    ~Server() override;
    // Claims `socket` unless a live window owns it. False when it does, or
    // when listening fails (`error` says why).
    bool listen(const QString& socket, QString* error = nullptr);
    [[nodiscard]] bool listening() const { return server_.isListening(); }

signals:
    void fileRequested(const QString& path);

private:
    QLocalServer server_;
    std::optional<blokkily::mcp::SocketLock> lock_;
};

} // namespace open_channel
