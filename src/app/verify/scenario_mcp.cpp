// features/mcp.feature, run on its own by the bdd_mcp gate (`--scenario mcp`).
// The gate is the running window, serving MCP on the socket main() opened for
// --mcp-socket. Each scenario talks to it the way an agent does: through the
// real `blokkily --mcp` bridge, started as a child process, one JSON-RPC line
// at a time on the bridge's stdin and stdout. While a reply is awaited the
// window's event loop keeps turning, because it is the window that answers.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>

#include <algorithm>
#include <iostream>
#include <optional>
#include <source_location>
#include <sstream>
#include <cmath>

namespace blokkily::verify {
namespace {

// One agent's end of the protocol: a bridge process and its replies.
class Agent {
public:
    Agent(const QString& socket, bool may_launch) {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("BLOKKILY_MCP_SOCKET", socket);
        if (!may_launch) environment.insert("BLOKKILY_MCP_NO_LAUNCH", "1");
        bridge_.setProcessEnvironment(environment);
        bridge_.setProgram(QCoreApplication::applicationFilePath());
        bridge_.setArguments({"--mcp"});
        bridge_.setProcessChannelMode(QProcess::SeparateChannels);
        bridge_.start();
        started_ = bridge_.waitForStarted(5000);
    }
    ~Agent() {
        if (bridge_.state() != QProcess::NotRunning) {
            bridge_.kill();
            bridge_.waitForFinished(2000);
        }
    }

    bool started() const { return started_; }

    // Sends a request and waits, bounded, for the reply with its id.
    std::optional<QJsonObject> request(const QString& method, const QJsonObject& params = {}) {
        const int id = ++next_id_;
        send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            while (bridge_.canReadLine()) {
                const auto line = bridge_.readLine();
                const auto reply = QJsonDocument::fromJson(line).object();
                if (reply.value("id").toInt() == id) return reply;
                stray_ = true;
            }
            if (bridge_.state() == QProcess::NotRunning) break;
            bridge_.waitForReadyRead(5);
        }
        return std::nullopt;
    }
    void notify(const QString& method) { send({{"jsonrpc", "2.0"}, {"method", method}}); }

    // A tool's result, or an empty object when the call itself failed.
    QJsonObject tool(const QString& name, const QJsonObject& arguments = {}) {
        const auto reply = request("tools/call", {{"name", name}, {"arguments", arguments}});
        return reply ? reply->value("result").toObject() : QJsonObject{};
    }

    // Ends the agent's side; true when the bridge then exits by itself, cleanly.
    bool close() {
        bridge_.closeWriteChannel();
        QElapsedTimer clock;
        clock.start();
        while (bridge_.state() != QProcess::NotRunning && clock.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            bridge_.waitForFinished(10);
        }
        return bridge_.state() == QProcess::NotRunning &&
               bridge_.exitStatus() == QProcess::NormalExit && bridge_.exitCode() == 0;
    }

    // Whether anything but a reply was written to the protocol channel.
    bool stray() const { return stray_; }

private:
    void send(const QJsonObject& message) {
        bridge_.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + "\n");
        bridge_.waitForBytesWritten(1000);
    }

    QProcess bridge_;
    bool started_ = false;
    bool stray_ = false;
    int next_id_ = 0;
};

QString textOf(const QJsonObject& result) {
    return result.value("content").toArray().at(0).toObject().value("text").toString();
}

bool isError(const QJsonObject& result) { return result.value("isError").toBool(); }

void run_mcp(VerifyContext& ctx) {
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "mcp check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto lay_out = [&ctx] {
        QCoreApplication::processEvents();
        (void)ctx.window->grabWindow();
    };
    // The loudest sample of `blocks` blocks of the production callback.
    const auto heard = [&ctx](int blocks) {
        float loudest = 0.0F;
        for (int block = 0; block < blocks; ++block) loudest = std::max(loudest, ctx.pump());
        return loudest;
    };
    const QString socket = QCoreApplication::instance()->property("blokkilyMcpSocket").toString();
    check(!socket.isEmpty());

    // ---------------------------------------------------------------------
    ctx.reached("mcp: Blokkily is running with one CLAP track");
    check(controller.verifyClap(ctx.parser.value("clap-fixture")));
    const auto clap = song.song().tracks.at(0).instrument;
    check(clap.format == "CLAP");
    {
        Song fresh;
        fresh.patterns.front().name = "PATTERN 1";
        fresh.tracks.front().name = "LEAD";
        fresh.tracks.front().instrument = clap;
        fresh.clips = {{0, 0, 0, 4}};
        song.replace(std::move(fresh));
    }
    VerifyContext::settle(20);
    check(ctx.window->setProperty("view", "ALL"));
    lay_out();
    check(controller.engine() != nullptr && controller.engine()->has_instrument(0));

    Agent agent(socket, false);
    check(agent.started());

    // ---------------------------------------------------------------------
    ctx.reached("mcp: an agent connects and is told what it can do");
    const auto hello = agent.request(
        "initialize", {{"protocolVersion", "2025-06-18"},
                       {"capabilities", QJsonObject{}},
                       {"clientInfo", QJsonObject{{"name", "bdd_mcp"}, {"version", "1"}}}});
    check(hello.has_value());
    const auto server = hello ? hello->value("result").toObject() : QJsonObject{};
    check(server.value("protocolVersion").toString() == "2025-06-18");
    check(server.value("serverInfo").toObject().value("name").toString() == "blokkily");
    check(server.value("capabilities").toObject().contains("tools"));
    check(!server.value("instructions").toString().isEmpty());
    agent.notify("notifications/initialized");
    const auto listed = agent.request("tools/list");
    QStringList names;
    if (listed)
        for (const auto& entry : listed->value("result").toObject().value("tools").toArray())
            names << entry.toObject().value("name").toString();
    for (const char* expected : {"get_session", "get_pattern", "write_notes", "transport",
                                 "list_plugins", "set_instrument", "screenshot", "describe",
                                 "call", "get_property"})
        check(names.contains(QLatin1String(expected)));

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent reads the session");
    const auto session = agent.tool("get_session").value("structuredContent").toObject();
    const auto first_track = session.value("tracks").toArray().at(0).toObject();
    check(first_track.value("name").toString() == "LEAD");
    check(first_track.value("hasInstrument").toBool());
    check(session.value("patterns").toArray().size() == 1);
    check(session.value("transport").toObject().value("playing") == QJsonValue(false));

    // ---------------------------------------------------------------------
    ctx.reached("mcp: notes the agent writes while the song plays are heard");
    const auto playing = agent.tool("transport", {{"action", "play"}});
    check(playing.value("structuredContent").toObject().value("playing").toBool());
    check(ctx.transport.playing());
    check(heard(24) == 0.0F);
    // The cursor waits on step 9, as a producer's would, before the agent
    // writes there.
    ctx.pattern.selectStep(8);
    lay_out();
    const auto inspector_note = [&ctx] {
        auto* label = ctx.named("inspectorNote");
        return label == nullptr ? QStringLiteral("(none)") : label->property("text").toString();
    };
    check(inspector_note().isEmpty());
    const auto written = agent.tool(
        "write_notes",
        {{"track", 0},
         {"notes", QJsonArray{QJsonObject{{"step", 0}, {"key", "C4"}, {"length", 4}},
                              QJsonObject{{"step", 4}, {"key", 64}, {"length", 4}},
                              QJsonObject{{"step", 8}, {"key", 67}, {"length", 4},
                                          {"velocity", 0.5}}}}});
    check(!isError(written) &&
          written.value("structuredContent").toObject().value("written").toInt() == 3);
    lay_out();
    check(ctx.pattern.rowCount() == 3);
    check(ctx.rendered_step(0) && ctx.rendered_step(4) && ctx.rendered_step(8));
    // Regression: the step inspector kept what the step held before an edit
    // made outside the editors (an agent's, an undo, the assistant's).
    if (inspector_note() != "G4")
        std::cerr << "REGRESSION: the inspector reads \"" << inspector_note().toStdString()
                  << "\" for the G4 the agent wrote under the cursor\n";
    check(inspector_note() == "G4");
    const auto read_back = agent.tool("get_pattern", {{"track", 0}});
    QList<int> keys;
    for (const auto& note : read_back.value("structuredContent").toObject().value("notes").toArray())
        keys << note.toObject().value("key").toInt();
    std::sort(keys.begin(), keys.end());
    check(keys == QList<int>({60, 64, 67}));
    // A sixteenth of the song is under twelve blocks; two bars always reach
    // a note that is now in the pattern.
    const float after = heard(96);
    check(after > 0.001F);
    check(agent.tool("transport", {{"action", "stop"}})
              .value("structuredContent").toObject().value("playing") == QJsonValue(false));

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent calls the window's own methods, and UNDO takes them back");
    const auto described = agent.tool("describe", {{"object", "songModel"}});
    check(textOf(described).contains("setTrackGain"));
    check(!isError(agent.tool("call", {{"object", "songModel"},
                                       {"method", "setTrackGain"},
                                       {"args", QJsonArray{0, -6}}})));
    check(std::abs(song.song().tracks.at(0).mix.gain_db + 6.0) < 1e-9);
    check(textOf(agent.tool("get_property", {{"object", "transport"}, {"property", "playing"}})) ==
          "false");
    check(textOf(agent.tool("call", {{"object", "songModel"}, {"method", "undo"}})) == "true");
    check(std::abs(song.song().tracks.at(0).mix.gain_db) < 1e-9);
    check(textOf(agent.tool("call", {{"object", "songModel"}, {"method", "undo"}})) == "true");
    lay_out();
    check(ctx.pattern.rowCount() == 0 && !ctx.rendered_step(0));
    check(inspector_note().isEmpty());

    // ---------------------------------------------------------------------
    ctx.reached("mcp: a bad request is refused without changing the song");
    check(isError(agent.tool("write_notes",
                             {{"notes", QJsonArray{QJsonObject{{"step", 0}, {"key", 60}},
                                                   QJsonObject{{"step", 1}, {"key", 200}}}}})));
    check(ctx.pattern.rowCount() == 0);
    const auto missing = agent.tool("call", {{"object", "songModel"}, {"method", "noSuchMethod"}});
    check(isError(missing) && textOf(missing).contains("describe"));
    check(isError(agent.tool("call", {{"object", "songModel"},
                                      {"method", "setTrackGain"},
                                      {"args", QJsonArray{"loud"}}})));
    const auto unknown = agent.request("tools/call", {{"name", "no_such_tool"}});
    check(unknown && unknown->contains("error"));

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent sees the window");
    // The notes are put back so the picture shows what the agent wrote.
    check(textOf(agent.tool("call", {{"object", "songModel"}, {"method", "redo"}})) == "true");
    lay_out();
    const auto picture = agent.tool("screenshot");
    const auto image_content = picture.value("content").toArray().at(0).toObject();
    check(image_content.value("type").toString() == "image" &&
          image_content.value("mimeType").toString() == "image/png");
    const QImage image = QImage::fromData(
        QByteArray::fromBase64(image_content.value("data").toString().toLatin1()), "PNG");
    check(!image.isNull() && image.size() == ctx.window->size() * ctx.window->devicePixelRatio());
    bool varies = false;
    for (int y = 0; y < image.height() && !varies; y += 37)
        for (int x = 0; x < image.width() && !varies; x += 41)
            varies = image.pixel(x, y) != image.pixel(0, 0);
    check(varies);
    // The agent's own picture is the gate's evidence.
    if (ctx.parser.isSet("screenshot")) check(image.save(ctx.parser.value("screenshot")));
    check(!agent.stray());

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent sets an instrument's parameter for the whole song");
    // Slot -1 is the track's instrument; the fixture's parameter 0 is its level.
    check(textOf(agent.tool("call", {{"object", "appController"},
                                     {"method", "automateInsert"},
                                     {"args", QJsonArray{"track", 0, -1, 0}}})) == "true");
    const auto& lanes = song.song().tracks.at(0).automation;
    check(lanes.size() == 1 && lanes[0].target.kind == AutomationTarget::Kind::parameter &&
          lanes[0].target.processor.slot == -1);
    const auto play_bar = [&] {
        check(!isError(agent.tool("transport", {{"action", "seek"}, {"bar", 0}})));
        check(!isError(agent.tool("transport", {{"action", "play"}})));
        const float loudest = heard(96);
        check(!isError(agent.tool("transport", {{"action", "stop"}})));
        return loudest;
    };
    check(!isError(agent.tool("call", {{"object", "songModel"},
                                       {"method", "moveAutomationPoint"},
                                       {"args", QJsonArray{0, 0, 0, 0, 0.0}}})));
    const float held_at_zero = play_bar();
    check(held_at_zero == 0.0F);
    check(!isError(agent.tool("call", {{"object", "songModel"},
                                       {"method", "moveAutomationPoint"},
                                       {"args", QJsonArray{0, 0, 0, 0, 0.25}}})));
    const float held_up = play_bar();
    check(held_up > 0.001F);

    // ---------------------------------------------------------------------
    ctx.reached("mcp: without a window the bridge still answers, and says why calls fail");
    {
        Agent alone(socket + "-nobody", false);
        check(alone.started());
        const auto alone_hello = alone.request("initialize", {{"protocolVersion", "2024-11-05"}});
        check(alone_hello &&
              alone_hello->value("result").toObject().value("protocolVersion").toString() ==
                  "2024-11-05");
        const auto alone_list = alone.request("tools/list");
        check(alone_list &&
              alone_list->value("result").toObject().value("tools").toArray().size() == 10);
        const auto refused = alone.tool("get_session");
        check(isError(refused) && textOf(refused).contains("not running"));
        check(alone.close());
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: closing the agent's end ends the bridge");
    check(agent.close());

    std::ostringstream details;
    details << " | tools=" << names.size() << " | heard after write " << after
            << " | level lane 0 -> " << held_at_zero << ", 0.25 -> " << held_up;
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("mcp", run_mcp);

}  // namespace
}  // namespace blokkily::verify
