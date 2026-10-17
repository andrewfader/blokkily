#include "mcp_bridge.hpp"

#include "mcp_protocol.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>

#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using blokkily::mcp::Session;

// How long a window the bridge started is waited for before the call that
// needed it is answered with an error.
constexpr int launch_wait_ms = 60000;
constexpr int retry_ms = 250;
// How long a bridge whose window went away waits for another open window to
// take the socket over (McpServer::serveWhenFree) before starting a new one.
constexpr int takeover_wait_ms = 2000;

void writeOut(const QByteArray& line) {
    std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

class Bridge final : public QObject {
public:
    Bridge()
        : local_([](const QString&, const QJsonObject&) {
              return blokkily::mcp::textResult(QStringLiteral("unreachable"), true);
          }),
          path_(blokkily::mcp::socketPath()),
          may_launch_(qgetenv("BLOKKILY_MCP_NO_LAUNCH") != "1") {
        QObject::connect(&socket_, &QLocalSocket::readyRead, this, [this] {
            while (socket_.canReadLine()) {
                QByteArray line = socket_.readLine();
                line.chop(line.endsWith('\n') ? 1 : 0);
                const auto id = QJsonDocument::fromJson(line).object().value("id");
                outstanding_.erase(id.toVariant().toString());
                writeOut(line);
            }
            finishIfDone();
        });
        QObject::connect(&socket_, &QLocalSocket::disconnected, this, [this] {
            // The window went away with calls unanswered: each is told so,
            // and the next call looks for a window again.
            for (const auto& id : outstanding_ids_)
                if (outstanding_.count(id.toVariant().toString()) != 0)
                    answerError(id, QStringLiteral("Blokkily closed before it answered."));
            outstanding_.clear();
            outstanding_ids_.clear();
            finishIfDone();
        });
        retry_.setInterval(retry_ms);
        QObject::connect(&retry_, &QTimer::timeout, this, &Bridge::tryConnect);
    }

    // A connected socket says it disconnected as it is destroyed, after the
    // members its handlers use are gone; it is let go of first.
    ~Bridge() override {
        QObject::disconnect(&socket_, nullptr, this, nullptr);
        socket_.abort();
    }

    // One line from the agent.
    void receive(const QByteArray& line) {
        const auto message = QJsonDocument::fromJson(line).object();
        if (message.value("method").toString() != "tools/call") {
            if (const auto reply = local_.handleLine(line)) writeOut(*reply);
            return;
        }
        waiting_.push_back(line);
        if (socket_.state() == QLocalSocket::ConnectedState) flush();
        else if (!retry_.isActive()) startConnecting();
    }

    void endOfInput() {
        input_ended_ = true;
        finishIfDone();
    }

private:
    void startConnecting() {
        clock_.restart();
        tryConnect();
        if (socket_.state() != QLocalSocket::ConnectedState) retry_.start();
    }

    void tryConnect() {
        socket_.abort();
#ifndef Q_OS_WIN
        // A Unix socket's path has a fixed limit; past it nothing can listen.
        if (path_.toLocal8Bit().size() > 107) {
            failWaiting(QStringLiteral("The socket path %1 is longer than a Unix socket allows; "
                                       "set BLOKKILY_MCP_SOCKET to a shorter one.").arg(path_));
            return;
        }
#endif
        socket_.connectToServer(path_);
        if (socket_.waitForConnected(200)) {
            retry_.stop();
            // A window that goes away after this may be started again.
            launched_ = false;
            had_window_ = true;
            flush();
            return;
        }
        // A window that holds the socket's lock is starting up, or too busy
        // to answer yet: it is waited for, and no second window is started
        // beside it.
        if (!launched_ && blokkily::mcp::SocketLock::taken(path_)) {
            if (clock_.elapsed() < launch_wait_ms) return;
            failWaiting(QStringLiteral("Blokkily is running but has not answered at %1 for a "
                                       "minute.").arg(path_));
            return;
        }
        // The window that served may have closed beside others, one of which
        // takes the socket over within moments: that is waited for first.
        if (had_window_ && !launched_ && clock_.elapsed() < takeover_wait_ms) return;
        // A window is started at most once per absence: one that was started
        // and never listened is not started again, call after call.
        if (!launched_ && may_launch_ && !launch_failed_) {
            launched_ = true;
            // The window's own output never reaches the agent: stdout is the
            // protocol.
            QProcess window;
            window.setProgram(QCoreApplication::applicationFilePath());
            window.setStandardInputFile(QProcess::nullDevice());
            window.setStandardOutputFile(QProcess::nullDevice());
            window.setStandardErrorFile(QProcess::nullDevice());
            if (!window.startDetached()) may_launch_ = false;
        }
        const bool waiting_for_launch =
            launched_ && !launch_failed_ && clock_.elapsed() < launch_wait_ms;
        if (waiting_for_launch) return;
        if (launched_) {
            launch_failed_ = true;
            failWaiting(QStringLiteral("Blokkily was started but did not start listening at %1 "
                                       "(is BLOKKILY_MCP=0 set?).").arg(path_));
        } else {
            failWaiting(QStringLiteral("Blokkily is not running (nothing is listening at %1). "
                                       "Start it, then try again.").arg(path_));
        }
    }

    // Every call still waiting for a window is answered with `why`.
    void failWaiting(const QString& why) {
        retry_.stop();
        for (const auto& line : waiting_)
            answerError(QJsonDocument::fromJson(line).object().value("id"), why);
        waiting_.clear();
        finishIfDone();
    }

    void flush() {
        for (const auto& line : waiting_) {
            const auto id = QJsonDocument::fromJson(line).object().value("id");
            outstanding_.insert(id.toVariant().toString());
            outstanding_ids_.push_back(id);
            socket_.write(line);
            socket_.write("\n");
        }
        waiting_.clear();
        socket_.flush();
    }

    void answerError(const QJsonValue& id, const QString& text) {
        writeOut(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                           {"id", id},
                                           {"result", blokkily::mcp::textResult(text, true)}})
                     .toJson(QJsonDocument::Compact));
    }

    void finishIfDone() {
        if (input_ended_ && waiting_.empty() && outstanding_.empty()) QCoreApplication::quit();
    }

    Session local_;
    QString path_;
    bool may_launch_;
    bool launched_ = false;
    // Whether this bridge has reached a window before.
    bool had_window_ = false;
    bool launch_failed_ = false;
    bool input_ended_ = false;
    QLocalSocket socket_;
    QTimer retry_;
    QElapsedTimer clock_;
    std::vector<QByteArray> waiting_;
    std::set<QString> outstanding_;
    std::vector<QJsonValue> outstanding_ids_;
};

}  // namespace

int runMcpBridge(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    Bridge bridge;
    // Standard input is read on a thread of its own, which works the same on
    // every platform; each line is handed to the event loop.
    std::thread reader([&bridge] {
        std::string line;
        while (std::getline(std::cin, line)) {
            QByteArray bytes = QByteArray::fromStdString(line);
            if (bytes.endsWith('\r')) bytes.chop(1);
            QMetaObject::invokeMethod(&bridge, [&bridge, bytes] { bridge.receive(bytes); },
                                      Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(&bridge, [&bridge] { bridge.endOfInput(); }, Qt::QueuedConnection);
    });
    reader.detach();
    return app.exec();
}
