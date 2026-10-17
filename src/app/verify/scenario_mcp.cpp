// features/mcp.feature, run on its own by the bdd_mcp gate (`--scenario mcp`).
// The gate is the running window, serving MCP on the socket main() opened for
// --mcp-socket. Each scenario talks to it the way an agent does: through the
// real `blokkily --mcp` bridge, started as a child process, one JSON-RPC line
// at a time on the bridge's stdin and stdout. While a reply is awaited the
// window's event loop keeps turning, because it is the window that answers.

#include "verify/harness.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/model/timebase.hpp"
#include "load_progress.hpp"
#include "mcp_server.hpp"
#include "mcp_tools.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <iostream>
#include <memory>
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
    // Asks for a tool and does not wait for the answer.
    void post(const QString& name, const QJsonObject& arguments = {}) {
        send({{"jsonrpc", "2.0"}, {"id", ++next_id_}, {"method", "tools/call"},
              {"params", QJsonObject{{"name", name}, {"arguments", arguments}}}});
    }
    // The agent gives up and kills its bridge, as a client that times out does.
    void hangUp() {
        bridge_.kill();
        bridge_.waitForFinished(2000);
    }

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
    for (const char* expected : {"get_session", "get_pattern", "write_notes", "set_section",
                                 "place_section", "transport",
                                 "list_plugins", "set_instrument", "list_parameters",
                                 "set_parameter", "set_soundfont_preset", "screenshot",
                                 "describe", "call", "get_property", "analyze_render",
                                 "get_score", "set_clef", "set_key_signature",
                                 "set_time_signature", "export_musicxml", "import_musicxml",
                                 "guide_events", "guide_lesson", "guide_ask", "set_meter"})
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
    // An argument of the wrong type is refused before anything happens: a
    // list, a string or a fraction given as the track is never read as the
    // selected track.
    const QJsonArray one_note{QJsonObject{{"step", 0}, {"key", 60}}};
    const auto listed_track = agent.tool("write_notes", {{"track", QJsonArray{1, 2}}, {"notes", one_note}});
    check(isError(listed_track) && textOf(listed_track).contains("track must be a whole number"));
    check(isError(agent.tool("write_notes", {{"track", "0"}, {"notes", one_note}})));
    check(isError(agent.tool("write_notes", {{"track", 0.5}, {"notes", one_note}})));
    const auto worded_step = agent.tool(
        "write_notes", {{"notes", QJsonArray{QJsonObject{{"step", "0"}, {"key", 60}}}}});
    check(isError(worded_step) && textOf(worded_step).contains("notes[0].step"));
    check(isError(agent.tool("transport", {{"action", "dance"}})));
    check(ctx.pattern.rowCount() == 0);
    const auto missing = agent.tool("call", {{"object", "songModel"}, {"method", "noSuchMethod"}});
    check(isError(missing) && textOf(missing).contains("describe"));
    check(isError(agent.tool("call", {{"object", "songModel"},
                                      {"method", "setTrackGain"},
                                      {"args", QJsonArray{"loud"}}})));
    const auto unknown = agent.request("tools/call", {{"name", "no_such_tool"}});
    check(unknown && unknown->contains("error"));
    // A call that lands while someone else is stamped (here the guide, as
    // during a lesson) gives the stamp back as it found it, not as the user's.
    ctx.controller.setEventSource(blokkily::guide::EventSource::guide);
    ctx.pattern.setEventSource(blokkily::guide::EventSource::guide);
    check(!isError(agent.tool("get_session")));
    check(ctx.controller.eventSource() == blokkily::guide::EventSource::guide &&
          ctx.pattern.eventSource() == blokkily::guide::EventSource::guide);
    ctx.controller.setEventSource(blokkily::guide::EventSource::user);
    ctx.pattern.setEventSource(blokkily::guide::EventSource::user);

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
    // Its folder is made here: run first in a fresh build, no other gate has.
    if (ctx.parser.isSet("screenshot")) {
        QDir{}.mkpath(QFileInfo(ctx.parser.value("screenshot")).absolutePath());
        check(image.save(ctx.parser.value("screenshot")));
    }
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
    // The lane names the parameter, as the processor does, not by its number.
    lay_out();
    const auto lane_rows = song.automationLanes();
    check(!lane_rows.isEmpty() && lane_rows.at(0).toMap().value("name").toString() == "INST · LEVEL");
    auto* lane_label = ctx.named("automationLaneName");
    check(lane_label != nullptr && lane_label->property("text").toString() == "INST · LEVEL");
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
    ctx.reached("mcp: the agent lists a sound's parameters and sets one by name");
    const auto listed_parameters =
        agent.tool("list_parameters", {{"track", 0}}).value("structuredContent").toObject();
    bool has_level = false;
    for (const auto& entry : listed_parameters.value("parameters").toArray()) {
        const auto parameter = entry.toObject();
        if (parameter.value("name").toString() == "Level" && parameter.contains("minimum") &&
            parameter.contains("maximum"))
            has_level = true;
    }
    check(has_level);
    check(!isError(agent.tool("set_parameter", {{"track", 0}, {"parameter", "level"}, {"value", 0.0}})));
    // Each parameter says what it is set to now.
    const auto level_now = [&agent] {
        for (const auto& entry : agent.tool("list_parameters", {{"track", 0}})
                                     .value("structuredContent").toObject().value("parameters").toArray())
            if (entry.toObject().value("name").toString() == "Level") return entry.toObject().value("value");
        return QJsonValue{};
    };
    const float set_to_zero = play_bar();
    check(set_to_zero == 0.0F);
    check(level_now().isDouble() && level_now().toDouble() == 0.0);
    check(!isError(agent.tool("set_parameter", {{"track", 0}, {"parameter", 0}, {"value", 0.25}})));
    const float set_up = play_bar();
    check(set_up > 0.001F);
    check(level_now().isDouble() && std::abs(level_now().toDouble() - 0.25) < 1e-6);
    check(isError(agent.tool("set_parameter", {{"track", 0}, {"parameter", "level"}, {"value", 99}})));
    check(isError(agent.tool("set_parameter", {{"track", 0}, {"parameter", "no such knob"}, {"value", 0}})));

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent locks a parameter on one step, by its address");
    // The lane the agent drew would hold the level over the whole song; the
    // locks are to be heard on their own.
    check(textOf(agent.tool("call", {{"object", "songModel"},
                                     {"method", "removeAutomationLane"},
                                     {"args", QJsonArray{0, 0}}})) == "true");
    check(song.song().tracks.at(0).automation.empty());
    const auto call_controller = [&agent](const char* method, const QJsonArray& args) {
        return textOf(agent.tool("call", {{"object", "appController"},
                                          {"method", QLatin1String(method)},
                                          {"args", args}}));
    };
    // Level (parameter 0 of the instrument, slot -1) down on step 5's note,
    // up again on step 9's.
    check(call_controller("setStepLock", {0, 0, 4, -1, 0, 0.0, false}) == "true");
    check(call_controller("setStepLock", {0, 0, 8, -1, 0, 0.25, false}) == "true");
    const auto locks_on = [&agent](int step) {
        for (const auto& entry : agent.tool("get_pattern", {{"track", 0}, {"section", 0}})
                                     .value("structuredContent").toObject()
                                     .value("notes").toArray()) {
            const auto note = entry.toObject();
            if (std::lround(note.value("step").toDouble()) == step)
                return note.value("locks").toArray();
        }
        return QJsonArray{};
    };
    {
        const auto down = locks_on(4);
        check(down.size() == 1);
        const auto lock = down.at(0).toObject();
        check(lock.value("slot").toInt() == -1 && lock.value("parameter").toInt() == 0 &&
              lock.value("name").toString() == "Level" && lock.value("value").toDouble() == 0.0 &&
              !lock.value("modulation").toBool());
        check(locks_on(8).size() == 1 && locks_on(8).at(0).toObject().value("value").toDouble() == 0.25);
        check(locks_on(0).isEmpty());
    }
    // The loudest sample of each block of the first bar, read from the
    // production callback, so each step's window can be heard on its own.
    const double blocks_per_step = 60.0 / ctx.transport.bpm() / 4.0 *
                                   controller.engine()->sample_rate() / 512.0;
    const auto play_steps = [&] {
        check(!isError(agent.tool("transport", {{"action", "seek"}, {"bar", 0}})));
        check(!isError(agent.tool("transport", {{"action", "play"}})));
        std::vector<float> peaks;
        for (int block = 0; block < static_cast<int>(blocks_per_step * 12.0); ++block)
            peaks.push_back(ctx.pump());
        check(!isError(agent.tool("transport", {{"action", "stop"}})));
        return peaks;
    };
    // The loudest block wholly inside steps [from, to).
    const auto within = [&](const std::vector<float>& peaks, double from, double to) {
        float loudest = 0.0F;
        const auto first = static_cast<std::size_t>(std::ceil(from * blocks_per_step)) + 1;
        const auto last = static_cast<std::size_t>(std::floor(to * blocks_per_step)) - 1;
        for (auto block = first; block < last && block < peaks.size(); ++block)
            loudest = std::max(loudest, peaks[block]);
        return loudest;
    };
    {
        const auto peaks = play_steps();
        check(within(peaks, 0, 4) > 0.001F);
        check(within(peaks, 4, 8) == 0.0F);
        check(within(peaks, 8, 12) > 0.001F);
    }
    check(call_controller("clearStepLocks", {0, 0, 4}) == "true");
    check(locks_on(4).isEmpty());
    {
        const auto peaks = play_steps();
        check(within(peaks, 4, 8) > 0.001F);
    }
    check(textOf(agent.tool("call", {{"object", "songModel"}, {"method", "undo"}})) == "true");
    check(locks_on(4).size() == 1);
    {
        const auto peaks = play_steps();
        check(within(peaks, 0, 4) > 0.001F);
        check(within(peaks, 4, 8) == 0.0F);
    }
    // Refusals leave every lock as it was.
    check(call_controller("setStepLock", {0, 0, 2, -1, 0, 0.5, false}) == "false");   // no note
    check(call_controller("setStepLock", {0, 0, 4, -1, 999, 0.5, false}) == "false"); // no parameter
    check(call_controller("setStepLock", {0, 0, 4, -1, 0, 99.0, false}) == "false");  // out of range
    check(call_controller("setStepLock", {0, 0, 4, 3, 0, 0.5, false}) == "false");    // no insert
    check(call_controller("setStepLock", {7, 0, 4, -1, 0, 0.5, false}) == "false");   // no track
    check(call_controller("setStepLock", {0, 5, 4, -1, 0, 0.5, false}) == "false");   // no pattern
    check(call_controller("clearStepLocks", {0, 0, 2}) == "false");
    check(locks_on(4).size() == 1 && locks_on(4).at(0).toObject().value("value").toDouble() == 0.0);

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent chooses a SoundFont's preset, and its note is heard");
    check(isError(agent.tool("set_soundfont_preset", {{"track", 0}, {"bank", 0}, {"program", 0}})));
    check(!isError(agent.tool("call", {{"object", "songModel"}, {"method", "addTrack"}})));
    // The gate's plugin list holds what it scanned: the CLAP fixture, and now
    // the test SoundFont beside it.
    controller.scanPluginPaths({ctx.parser.value("clap-fixture").toStdString()}, {},
                               {ctx.parser.value("soundfont-fixture").toStdString()});
    const QString bank_name = QFileInfo(ctx.parser.value("soundfont-fixture")).completeBaseName();
    int bank_index = -1;
    for (const auto& entry : agent.tool("list_plugins").value("structuredContent").toObject().value("plugins").toArray())
        if (entry.toObject().value("name").toString() == bank_name) bank_index = entry.toObject().value("index").toInt();
    check(bank_index >= 0);
    check(!isError(agent.tool("set_instrument", {{"track", 1}, {"plugin", bank_index}})));
    check(isError(agent.tool("set_soundfont_preset", {{"track", 1}, {"bank", 127}, {"program", 127}})));
    check(!isError(agent.tool("set_soundfont_preset", {{"track", 1}, {"bank", 0}, {"program", 0}})));
    {
        const auto& state = song.song().tracks.at(1).instrument.state;
        const std::string text(reinterpret_cast<const char*>(state.data()), state.size());
        check(text.size() > 4 && text.substr(text.size() - 4) == "\n0\n0");
    }
    check(!isError(agent.tool("write_notes", {{"track", 1}, {"notes", QJsonArray{QJsonObject{{"step", 0}, {"key", 60}, {"length", 8}}}}})));
    check(!isError(agent.tool("call", {{"object", "songModel"}, {"method", "toggleSolo"}, {"args", QJsonArray{1}}})));
    const float soundfont_heard = play_bar();
    check(soundfont_heard > 0.001F);
    check(!isError(agent.tool("call", {{"object", "songModel"}, {"method", "toggleSolo"}, {"args", QJsonArray{1}}})));

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent measures what it made");
    // The fixture's Tone plays a sine at each note's pitch: C4 and G4 here
    // (E4's level is locked to 0), both in the low mids.
    check(!isError(agent.tool("set_parameter", {{"track", 0}, {"parameter", "tone"}, {"value", 1}})));
    const auto analyze = [&agent](const QJsonObject& arguments) {
        return agent.tool("analyze_render", arguments).value("structuredContent").toObject();
    };
    const auto share_of = [](const QJsonObject& analysis, const QString& name) {
        for (const auto& entry : analysis.value("bands").toArray())
            if (entry.toObject().value("name").toString() == name)
                return entry.toObject().value("share").toDouble();
        return -1.0;
    };
    const auto mix = analyze({});
    const double mix_lufs = mix.value("integratedLufs").toDouble(1.0);
    check(mix.value("source").toString() == "master");
    check(mix.value("integratedLufs").isDouble() && mix_lufs > -40.0 && mix_lufs < 0.0);
    check(mix.value("truePeakDbtp").toDouble() >= mix.value("samplePeakDbfs").toDouble() - 0.01);
    double shares = 0.0;
    for (const auto& entry : mix.value("bands").toArray()) shares += entry.toObject().value("share").toDouble();
    check(mix.value("bands").toArray().size() == 7 && std::abs(shares - 1.0) < 0.01);
    // The song's length and the export's half second of tail.
    const double song_seconds =
        static_cast<double>(controller.engine()->song_samples()) / controller.engine()->sample_rate();
    check(std::abs(mix.value("seconds").toDouble() - (song_seconds + 0.5)) < 0.01);
    const auto lead = analyze({{"track", 0}});
    const double lead_lufs = lead.value("integratedLufs").toDouble(1.0);
    const double lead_low_mid = share_of(lead, "lowMid");
    check(lead.value("source").toString() == "track 0" && lead.value("integratedLufs").isDouble());
    check(lead_low_mid > 0.8);
    check(lead.value("spectralCentroidHz").toDouble() > 200.0 &&
          lead.value("spectralCentroidHz").toDouble() < 700.0);
    // A file the agent exported reads as the render it measured: one engine.
    const QTemporaryDir exports;
    const QString exported = exports.filePath("measured.wav");
    check(textOf(agent.tool("call", {{"object", "appController"},
                                     {"method", "exportAudioFile"},
                                     {"args", QJsonArray{exported}}})) == "true");
    const auto from_file = analyze({{"file", exported}});
    check(from_file.value("integratedLufs").isDouble() &&
          std::abs(from_file.value("integratedLufs").toDouble() - mix_lufs) < 0.011);
    check(std::abs(from_file.value("truePeakDbtp").toDouble() - mix.value("truePeakDbtp").toDouble()) < 0.011);
    // The fader is heard in the measurement: 6 dB down is 6 LU quieter.
    check(!isError(agent.tool("call", {{"object", "songModel"},
                                       {"method", "setTrackGain"},
                                       {"args", QJsonArray{0, -6}}})));
    const auto quieter = analyze({{"track", 0}});
    const double fader_drop = lead_lufs - quieter.value("integratedLufs").toDouble(1.0);
    check(std::abs(fader_drop - 6.0) < 0.1);
    check(std::abs(lead.value("truePeakDbtp").toDouble() - quieter.value("truePeakDbtp").toDouble() - 6.0) < 0.1);
    check(textOf(agent.tool("call", {{"object", "songModel"}, {"method", "undo"}})) == "true");
    check(std::abs(song.song().tracks.at(0).mix.gain_db) < 1e-9);
    // Measuring changes nothing: the transport stays where it was.
    check(!ctx.transport.playing());
    // Refusals.
    check(isError(agent.tool("analyze_render", {{"track", 9}})));
    check(isError(agent.tool("analyze_render",
                             {{"return", static_cast<int>(song.song().returns.size())}})));
    check(isError(agent.tool("analyze_render", {{"track", 0}, {"file", exported}})));
    check(isError(agent.tool("analyze_render", {{"file", exports.filePath("missing.wav")}})));

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent measures some bars, and every bus from one render");
    {
        // The song's one clip runs four bars: bar 1 alone is a stretch
        // shorter than the song.
        const int song_bars = agent.tool("get_session").value("structuredContent").toObject().value("lengthBars").toInt();
        check(song_bars == 4);
        controller.flushRecompile();
        const auto rate = controller.engine()->sample_rate();
        const auto bar_seconds = [&](int bar) {
            const auto& meter = song.song().meter;
            const auto& clock = controller.engine()->published_clock();
            return static_cast<double>(
                       blokkily::sample_for_tick(clock, static_cast<double>(meter.bar_start(bar + 1))) -
                       blokkily::sample_for_tick(clock, static_cast<double>(meter.bar_start(bar)))) / rate;
        };
        const auto second_bar = analyze({{"track", 0}, {"fromBar", 1}, {"toBar", 2}});
        check(second_bar.value("source").toString() == "track 0");
        check(std::abs(second_bar.value("seconds").toDouble() - (bar_seconds(1) + 0.5)) < 0.01);
        // Bar 1 plays what every bar of the clip plays: it measures as the
        // whole song did.
        check(std::abs(second_bar.value("integratedLufs").toDouble() - lead_lufs) < 0.5);
        const auto whole_lead = analyze({{"track", 0}});
        const auto every = analyze({{"all", true}}).value("buses").toArray();
        const auto bus_count = 1 + song.song().tracks.size() + song.song().returns.size();
        check(static_cast<std::size_t>(every.size()) == bus_count);
        check(!every.isEmpty() && every.at(0).toObject().value("source").toString() == "master");
        check(every.size() > 1 && every.at(1).toObject().value("source").toString() == "track 0" &&
              every.at(1).toObject().value("name").toString() == "LEAD");
        // One render measures each bus as its own render does.
        check(every.size() > 1 &&
              std::abs(every.at(1).toObject().value("integratedLufs").toDouble() -
                       whole_lead.value("integratedLufs").toDouble()) < 0.011);
        check(!every.isEmpty() &&
              std::abs(every.at(0).toObject().value("integratedLufs").toDouble() -
                       analyze({}).value("integratedLufs").toDouble()) < 0.011);
        // Exports of some bars: the file and every stem are that stretch.
        const QString stretch = exports.filePath("bar-two.wav");
        check(textOf(agent.tool("call", {{"object", "appController"}, {"method", "exportAudioRange"},
                                         {"args", QJsonArray{stretch, 1, 2}}})) == "true");
        check(std::abs(analyze({{"file", stretch}}).value("seconds").toDouble() - (bar_seconds(1) + 0.5)) < 0.01);
        const QString stems = exports.filePath("stems");
        QDir().mkpath(stems);
        check(textOf(agent.tool("call", {{"object", "appController"}, {"method", "exportTrackFilesRange"},
                                         {"args", QJsonArray{stems, 1, 2, "WAV", "FLOAT32"}}})) == "true");
        const auto stem_files = QDir(stems).entryList({"*.wav"}, QDir::Files, QDir::Name);
        check(static_cast<std::size_t>(stem_files.size()) == bus_count - 1);
        for (const auto& name : stem_files)
            check(std::abs(analyze({{"file", QDir(stems).filePath(name)}}).value("seconds").toDouble() -
                           (bar_seconds(1) + 0.5)) < 0.01);
        // Bars outside the song, an empty stretch, or a range on a file are refused.
        check(isError(agent.tool("analyze_render", {{"fromBar", song_bars + 1}, {"toBar", song_bars + 5}})));
        check(isError(agent.tool("analyze_render", {{"fromBar", 1}, {"toBar", 1}})));
        check(isError(agent.tool("analyze_render", {{"file", stretch}, {"fromBar", 1}})));
        check(isError(agent.tool("analyze_render", {{"all", true}, {"track", 0}})));
        check(textOf(agent.tool("call", {{"object", "appController"}, {"method", "exportAudioRange"},
                                         {"args", QJsonArray{stretch, song_bars, song_bars + 4}}})) == "false");
        check(!ctx.transport.playing());
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent writes a note between keys, and it is heard there");
    double cents_ratio = 0.0;
    {
        // One long A4 on the fixture's sine, then the same note a quarter
        // tone up: its pitch, read off the render, rises by 50 cents.
        const auto a4 = [&](double cents) {
            return agent.tool("write_notes",
                              {{"track", 0}, {"section", 0}, {"replace", true},
                               {"notes", QJsonArray{QJsonObject{{"step", 0}, {"key", "A4"},
                                                                {"length", 16}, {"cents", cents}}}}});
        };
        check(!isError(a4(0.0)));
        const double plain = analyze({{"track", 0}}).value("spectralCentroidHz").toDouble();
        check(!isError(a4(50.0)));
        const auto read = agent.tool("get_pattern", {{"track", 0}, {"section", 0}})
                              .value("structuredContent").toObject().value("notes").toArray();
        check(read.size() == 1 && read.at(0).toObject().value("cents").toDouble() == 50.0);
        const double raised = analyze({{"track", 0}}).value("spectralCentroidHz").toDouble();
        cents_ratio = raised / plain;
        check(std::abs(plain - 440.0) < 5.0);
        check(std::abs(cents_ratio - std::pow(2.0, 50.0 / 1200.0)) < 0.004);
        // Out of range is refused, and the part keeps its note.
        check(isError(agent.tool("write_notes",
                                 {{"track", 0}, {"section", 0},
                                  {"notes", QJsonArray{QJsonObject{{"step", 1}, {"key", 60}, {"cents", 150}}}}})));
        check(agent.tool("get_pattern", {{"track", 0}, {"section", 0}})
                  .value("structuredContent").toObject().value("notes").toArray().size() == 1);
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent pushes a note off its step, and it is heard there");
    {
        // A short C4 on step 4, then the same note 40 ticks late: the soloed
        // track's export starts sounding 40 ticks' worth of samples later.
        const QTemporaryDir offsets;
        const auto onset = [&](int offset) -> std::optional<double> {
            if (isError(agent.tool("write_notes",
                                   {{"track", 0}, {"section", 0}, {"replace", true},
                                    {"notes", QJsonArray{QJsonObject{{"step", 4}, {"key", "C4"},
                                                                     {"length", 2}, {"offset", offset}}}}})))
                return std::nullopt;
            const QString file = offsets.filePath(QStringLiteral("offset-%1.wav").arg(offset));
            agent.tool("call", {{"object", "appController"}, {"method", "exportAudioFile"}, {"args", QJsonArray{file}}});
            std::string error;
            const auto wave = blokkily::read_wave(file.toStdString(), &error);
            if (!wave) return std::nullopt;
            for (std::size_t frame = 0; frame < static_cast<std::size_t>(wave->frames); ++frame)
                if (std::abs(wave->interleaved[frame * wave->channels]) > 0.001F)
                    return static_cast<double>(frame);
            return std::nullopt;
        };
        check(!isError(agent.tool("call", {{"object", "songModel"}, {"method", "toggleSolo"}, {"args", QJsonArray{0}}})));
        const auto on_step = onset(0);
        const auto late = onset(40);
        check(!isError(agent.tool("call", {{"object", "songModel"}, {"method", "toggleSolo"}, {"args", QJsonArray{0}}})));
        const auto read = agent.tool("get_pattern", {{"track", 0}, {"section", 0}})
                              .value("structuredContent").toObject().value("notes").toArray();
        check(read.size() == 1 && read.at(0).toObject().value("offset").toInt() == 40 &&
              read.at(0).toObject().value("step").toDouble() == 4.0);
        const auto& clock = controller.engine()->published_clock();
        const double forty = static_cast<double>(blokkily::sample_for_tick(clock, 4 * 120 + 40) -
                                                 blokkily::sample_for_tick(clock, 4 * 120));
        check(on_step && late && std::abs((*late - *on_step) - forty) <= 2.0);
        // Half a step or more, or a fraction of a tick, is refused, and the part keeps its note.
        check(isError(agent.tool("write_notes", {{"track", 0}, {"section", 0},
                                                 {"notes", QJsonArray{QJsonObject{{"step", 1}, {"key", 60}, {"offset", 60}}}}})));
        check(isError(agent.tool("write_notes", {{"track", 0}, {"section", 0},
                                                 {"notes", QJsonArray{QJsonObject{{"step", 1}, {"key", 60}, {"offset", 2.5}}}}})));
        check(agent.tool("get_pattern", {{"track", 0}, {"section", 0}})
                  .value("structuredContent").toObject().value("notes").toArray().size() == 1);
        // The long A4 a quarter tone up is put back for what follows.
        check(!isError(agent.tool("write_notes",
                                  {{"track", 0}, {"section", 0}, {"replace", true},
                                   {"notes", QJsonArray{QJsonObject{{"step", 0}, {"key", "A4"},
                                                                    {"length", 16}, {"cents", 50}}}}})));
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent sets an effect on a return and on the master by name");
    double master_drop = 0.0, return_drop = 0.0;
    {
        int saturation = -1;
        for (const auto& entry : agent.tool("list_plugins").value("structuredContent").toObject().value("plugins").toArray())
            if (entry.toObject().value("name").toString() == "Saturation" &&
                entry.toObject().value("kind").toString() == "effect")
                saturation = entry.toObject().value("index").toInt();
        check(saturation >= 0);
        const auto call = [&agent](const char* object, const char* method, const QJsonArray& args) {
            return agent.tool("call", {{"object", QLatin1String(object)}, {"method", QLatin1String(method)}, {"args", args}});
        };
        // Saturation on the master, and on a return that track 0 sends to.
        check(!isError(call("songModel", "selectRack", {"master", 0})));
        check(textOf(call("appController", "addEffect", {saturation})) == "true");
        const int bus = textOf(call("songModel", "addReturn", {})).toInt();
        check(bus >= 0 && bus < static_cast<int>(song.song().returns.size()));
        check(!isError(call("songModel", "setSendLevel", {0, bus, 0.0})));
        check(!isError(call("songModel", "selectRack", {"return", bus})));
        check(textOf(call("appController", "addEffect", {saturation})) == "true");
        // Each is listed and set by name, as a track's is.
        bool has_output = false;
        for (const auto& entry : agent.tool("list_parameters", {{"master", true}, {"slot", 0}})
                                     .value("structuredContent").toObject().value("parameters").toArray())
            has_output = has_output || entry.toObject().value("name").toString() == "Output";
        check(has_output);
        check(!isError(agent.tool("list_parameters", {{"return", bus}})));
        const auto set = [&agent](const QJsonObject& where, double value) {
            QJsonObject arguments = where;
            arguments.insert("parameter", "output");
            arguments.insert("value", value);
            return agent.tool("set_parameter", arguments);
        };
        const int lanes_before = static_cast<int>(song.song().tracks.at(0).automation.size());
        song.selectTrack(0);
        check(!isError(set({{"master", true}}, 0.0)));
        const double master_open = analyze({}).value("integratedLufs").toDouble(1.0);
        const auto lowered = set({{"master", true}}, -12.0).value("structuredContent").toObject();
        check(lowered.value("laneTrack").toInt() == 0 && lowered.value("master").toBool());
        // The second value moves the one lane rather than adding another.
        check(static_cast<int>(song.song().tracks.at(0).automation.size()) == lanes_before + 1);
        master_drop = master_open - analyze({}).value("integratedLufs").toDouble(1.0);
        check(std::abs(master_drop - 12.0) < 0.1);
        check(!isError(set({{"return", bus}}, 0.0)));
        const double return_open = analyze({{"return", bus}}).value("integratedLufs").toDouble(1.0);
        check(!isError(set({{"return", bus}}, -12.0)));
        return_drop = return_open - analyze({{"return", bus}}).value("integratedLufs").toDouble(1.0);
        check(std::abs(return_drop - 12.0) < 0.1);
        // A lane already on another track is the one that moves.
        song.selectTrack(1);
        check(set({{"master", true}}, -6.0).value("structuredContent").toObject().value("laneTrack").toInt() == 0);
        song.selectTrack(0);
        // Refusals.
        check(isError(set({{"return", bus + 1}}, 0.0)));
        check(isError(set({{"master", true}, {"track", 0}}, 0.0)));
        check(isError(set({{"master", true}}, 99.0)));
        check(isError(agent.tool("list_parameters", {{"master", true}, {"slot", -1}})));
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the session shows each bus's effects and sends");
    {
        const auto view = agent.tool("get_session").value("structuredContent").toObject();
        const auto named_saturation = [](const QJsonArray& effects) {
            return effects.size() == 1 &&
                   effects.at(0).toObject().value("name").toString().contains("saturation", Qt::CaseInsensitive) &&
                   effects.at(0).toObject().value("slot").toInt() == 0;
        };
        check(named_saturation(view.value("masterEffects").toArray()));
        const auto returns = view.value("returns").toArray();
        check(returns.size() == static_cast<int>(song.song().returns.size()) && !returns.isEmpty());
        check(named_saturation(returns.last().toObject().value("effects").toArray()));
        const auto lead = view.value("tracks").toArray().at(0).toObject();
        check(lead.value("effects").toArray().isEmpty());
        const auto sends = lead.value("sends").toArray();
        check(sends.size() == 1 && sends.at(0).toObject().value("return").toInt() == returns.size() - 1 &&
              sends.at(0).toObject().value("levelDb").toDouble() == 0.0);
        check(view.value("patterns").toArray().at(0).toObject().value("steps").toInt() == 16);
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent shapes and places a pattern while the song plays");
    {
        const auto call = [&agent](const char* object, const char* method, const QJsonArray& args) {
            return agent.tool("call", {{"object", QLatin1String(object)}, {"method", QLatin1String(method)}, {"args", args}});
        };
        check(!isError(call("songModel", "addPattern", {})));
        const int added = static_cast<int>(song.song().sections.size()) - 1;
        check(added >= 1);
        // The playhead keeps the section it plays open, as the window does.
        check(!isError(call("appController", "setFollowPlayback", {true})));
        check(!isError(agent.tool("transport", {{"action", "seek"}, {"bar", 0}})));
        check(!isError(agent.tool("transport", {{"action", "play"}})));
        (void)heard(8);
        check(song.currentSection() == 0);
        const auto shaped = agent.tool("set_section", {{"section", added}, {"steps", 32}, {"swing", 25}, {"name", "BRIDGE"}});
        check(!isError(shaped));
        const auto section_length = [&](int section) {
            blokkily::Tick length = -1;
            for (const auto& slot : song.song().patterns)
                if (static_cast<int>(slot.section) == section) {
                    if (length >= 0 && slot.pattern.length() != length) return blokkily::Tick{-2};
                    length = slot.pattern.length();
                }
            return length;
        };
        check(section_length(added) == 32 * 120);
        check(section_length(0) == 16 * 120);
        check(song.song().patterns.at(static_cast<std::size_t>(song.partOf(added, 0))).pattern.swing() == 25);
        check(song.sections().at(added).toMap().value("name").toString() == "BRIDGE");
        const auto placed = agent.tool("place_section", {{"section", added}, {"bar", 8}});
        check(!isError(placed) &&
              placed.value("structuredContent").toObject().value("placed").toInt() ==
                  static_cast<int>(song.song().tracks.size()));
        bool at_bar_eight = false;
        for (const auto& entry : agent.tool("get_session").value("structuredContent").toObject().value("clips").toArray())
            at_bar_eight = at_bar_eight || (entry.toObject().value("bar").toInt() == 8 &&
                                            entry.toObject().value("name").toString() == "BRIDGE" &&
                                            entry.toObject().value("bars").toInt() == 2);
        check(at_bar_eight);
        const auto one = agent.tool("place_section", {{"section", added}, {"bar", 12}, {"track", 0}});
        check(!isError(one) && one.value("structuredContent").toObject().value("placed").toInt() == 1);
        // Refusals change nothing.
        const auto clips = song.song().clips.size();
        // Placing it where it already is asks for what the song already holds.
        const auto again = agent.tool("place_section", {{"section", added}, {"bar", 8}}).value("structuredContent").toObject();
        check(again.value("alreadyPlaced").toBool() && again.value("placed").toInt() == 0);
        check(song.song().clips.size() == clips);
        // A stretch that is taken is refused, naming what is in the way.
        const auto blocked = agent.tool("place_section", {{"section", 0}, {"bar", 9}});
        check(isError(blocked) && textOf(blocked).contains("BRIDGE from bar 8"));
        check(isError(agent.tool("place_section", {{"section", 99}, {"bar", 16}})));
        check(isError(agent.tool("place_section", {{"section", added}, {"bar", 16}, {"track", 9}})));
        check(song.song().clips.size() == clips);
        check(isError(agent.tool("set_section", {{"section", 99}, {"steps", 16}})));
        check(isError(agent.tool("set_section", {{"section", added}, {"steps", 0}})));
        check(isError(agent.tool("set_section", {{"section", added}, {"swing", 150}})));
        check(isError(agent.tool("set_section", {{"section", added}})));
        check(section_length(added) == 32 * 120);
        check(!isError(agent.tool("transport", {{"action", "stop"}})));
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent sets the song's meter, and its bars follow it");
    {
        const auto meters_before = song.song().meter;
        const auto set = agent.tool("set_meter", {{"bar", 20}, {"numerator", 7}, {"denominator", 8}});
        check(!isError(set));
        const auto meters = agent.tool("get_session").value("structuredContent").toObject().value("meters").toArray();
        check(meters.size() == 2 && meters.at(1).toObject().value("bar").toInt() == 20 &&
              meters.at(1).toObject().value("numerator").toInt() == 7 &&
              meters.at(1).toObject().value("denominator").toInt() == 8);
        check(song.song().meter.bar_length(20) == 1680 && song.song().meter.bar_length(19) == 1920);
        // A 14-step section fills a bar of 7/8: placed in bars 20 and 21, the
        // second starts one 7/8 bar after the first.
        check(!isError(agent.tool("call", {{"object", "songModel"}, {"method", "addPattern"}})));
        const int seven = static_cast<int>(song.song().sections.size()) - 1;
        check(!isError(agent.tool("set_section", {{"section", seven}, {"steps", 14}, {"name", "SEVEN"}})));
        check(!isError(agent.tool("place_section", {{"section", seven}, {"bar", 20}, {"track", 0}})));
        check(!isError(agent.tool("place_section", {{"section", seven}, {"bar", 21}, {"track", 0}})));
        std::vector<blokkily::Tick> starts;
        const auto part = static_cast<std::size_t>(song.partOf(seven, 0));
        for (const auto& clip : song.song().clips)
            if (clip.track == 0 && clip.pattern == part) starts.push_back(clip.start);
        std::sort(starts.begin(), starts.end());
        check(starts.size() == 2 && starts.at(0) == song.song().meter.bar_start(20) &&
              starts.at(1) - starts.at(0) == 1680);
        check(agent.tool("get_session").value("structuredContent").toObject().value("lengthBars").toInt() == 22);
        // A unit that is not a power of two, no beats, or a bar before the
        // first is refused, changing nothing.
        const auto meter_now = song.song().meter;
        check(isError(agent.tool("set_meter", {{"bar", 4}, {"numerator", 7}, {"denominator", 6}})));
        check(isError(agent.tool("set_meter", {{"bar", 4}, {"numerator", 0}, {"denominator", 4}})));
        check(isError(agent.tool("set_meter", {{"bar", -1}, {"numerator", 3}, {"denominator", 4}})));
        check(song.song().meter == meter_now);
        // UNDO takes it all back, the meter last.
        for (int step = 0; step < 8 && !(song.song().meter == meters_before); ++step)
            check(textOf(agent.tool("call", {{"object", "songModel"}, {"method", "undo"}})) == "true");
        check(song.song().meter == meters_before);
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: an agent that hangs up during a long call leaves the window running");
    {
        // The render draws frames while it works; the agent hangs up on the
        // first, so the window learns of it with the answer still owed.
        Agent impatient(socket, false);
        check(impatient.started());
        check(impatient.request("initialize", {{"protocolVersion", "2025-06-18"}}).has_value());
        // Every percent is drawn at once, so the render pumps the event loop
        // from its start.
        controller.progress()->setTiming(0, 0);
        bool hung_up = false;
        int frames_after = 0;
        const auto hang_up = QObject::connect(controller.progress(), &LoadProgress::frameDrawn, [&] {
            if (hung_up) {
                ++frames_after;
                return;
            }
            hung_up = true;
            impatient.hangUp();
        });
        impatient.post("analyze_render");
        QElapsedTimer clock;
        clock.start();
        while (!hung_up && clock.elapsed() < 15000) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        // Whatever the render still owes is answered into the closed line.
        for (int turn = 0; turn < 50; ++turn) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QObject::disconnect(hang_up);
        controller.progress()->setTiming(LoadProgress::default_reveal_ms, LoadProgress::default_frame_ms);
        check(hung_up);
        // The render nobody is waiting for is abandoned: it draws no more
        // than the frame or two it was in, where a finished one draws one per
        // percent.
        check(frames_after < 10);
        // The window still answers the next agent.
        Agent next(socket, false);
        check(next.started());
        check(next.request("initialize", {{"protocolVersion", "2025-06-18"}}).has_value());
        check(!isError(next.tool("get_session")));
        check(next.close());
    }
    ctx.reached("mcp: the agent reads a part as staff notation");
    // A known part: a quarter-note C4, a quarter's rest, an E4-G4 dyad, then
    // silence - replacing whatever the earlier scenarios left behind.
    check(!isError(agent.tool("write_notes",
                              {{"track", 0},
                               {"section", 0},
                               {"replace", true},
                               {"notes", QJsonArray{QJsonObject{{"step", 0}, {"key", "C4"}, {"length", 4}},
                                                    QJsonObject{{"step", 8}, {"key", "E4"}, {"length", 4}},
                                                    QJsonObject{{"step", 8}, {"key", "G4"}, {"length", 4}}}}})));
    const auto score_result = agent.tool("get_score", {{"track", 0}, {"section", 0}});
    check(!isError(score_result));
    const auto score = score_result.value("structuredContent").toObject();
    check(score.value("clef").toString() == "Treble");
    check(score.value("key").toObject().value("fifths").toInt() == 0);
    check(score.value("time").toString() == "4/4");
    QStringList read_notes;
    for (const auto& entry : score.value("notes").toArray()) {
        const auto note = entry.toObject();
        read_notes << QStringLiteral("%1@%2:%3%4")
                          .arg(note.value("spelled").toString())
                          .arg(note.value("step").toDouble())
                          .arg(note.value("value").toString(), note.value("chord").toBool() ? "+" : "");
    }
    // Where each note falls, the dyad as one chord.
    check(read_notes == QStringList({"C4@0:quarter", "E4@8:quarter", "G4@8:quarter+"}));
    QStringList read_rests;
    for (const auto& entry : score.value("rests").toArray())
        read_rests << QStringLiteral("%1:%2").arg(entry.toObject().value("step").toDouble()).arg(entry.toObject().value("value").toString());
    check(read_rests == QStringList({"4:quarter", "12:quarter"}));
    const QString score_xml = score.value("musicxml").toString();
    check(score_xml.contains("<score-partwise") && score_xml.contains("<chord") && score_xml.contains("<rest"));

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent dresses the score in another clef, key and meter");
    check(!isError(agent.tool("set_clef", {{"track", 0}, {"section", 0}, {"clef", "Bass"}})));
    check(!isError(agent.tool("set_key_signature", {{"section", 0}, {"fifths", -2}})));
    check(!isError(agent.tool("set_time_signature", {{"section", 0}, {"beats", 3}, {"unit", 4}})));
    const auto dressed = agent.tool("get_score", {{"track", 0}, {"section", 0}}).value("structuredContent").toObject();
    check(dressed.value("clef").toString() == "Bass");
    check(dressed.value("key").toObject().value("fifths").toInt() == -2);
    check(dressed.value("key").toObject().value("label").toString() == "Bb major");
    check(dressed.value("time").toString() == "3/4");
    // The pitches never move under a new key signature: E4 stays E natural
    // in Bb major, and prints its natural sign.
    QStringList respelled;
    bool e_needs_natural = false;
    for (const auto& note : dressed.value("notes").toArray()) {
        respelled << note.toObject().value("spelled").toString();
        if (note.toObject().value("midi").toInt() == 64) e_needs_natural = note.toObject().value("accidentalShown").toBool();
    }
    check(respelled == QStringList({"C4", "E4", "G4"}));
    check(e_needs_natural);
    // The settings are the song's: CHANT keeps the meter for when it comes
    // off, the song's meter comes back on request, and UNDO takes a change back.
    check(agent.tool("set_time_signature", {{"section", 0}, {"unmetered", true}})
              .value("structuredContent").toObject().value("time").toString() == "unmetered");
    check(agent.tool("set_time_signature", {{"section", 0}, {"unmetered", false}})
              .value("structuredContent").toObject().value("time").toString() == "3/4");
    const auto follows = agent.tool("set_time_signature", {{"section", 0}, {"song", true}}).value("structuredContent").toObject();
    check(follows.value("time").toString() == "4/4" && follows.value("followsSong").toBool());
    check(textOf(agent.tool("call", {{"object", "songModel"}, {"method", "undo"}})) == "true");
    check(agent.tool("get_score", {{"track", 0}, {"section", 0}}).value("structuredContent").toObject().value("time").toString() == "3/4");
    check(song.song().patterns.at(static_cast<std::size_t>(song.partOf(0, 0))).clef == "Bass");
    check(song.song().sections.at(0).key_fifths == -2);
    // Refusals change nothing.
    check(isError(agent.tool("set_clef", {{"clef", "Treble-8vb"}})));
    check(isError(agent.tool("set_key_signature", {{"fifths", 8}})));
    check(isError(agent.tool("set_key_signature", {{"fifths", "2"}})));
    check(isError(agent.tool("set_time_signature", {{"beats", 3}, {"unit", 3}})));
    check(isError(agent.tool("set_time_signature", {{"section", 0}})));
    check(isError(agent.tool("get_score", {{"key", "2"}})));
    const auto still = agent.tool("get_score", {{"track", 0}, {"section", 0}}).value("structuredContent").toObject();
    check(still.value("clef").toString() == "Bass" && still.value("key").toObject().value("fifths").toInt() == -2 &&
          still.value("time").toString() == "3/4");

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent exports MusicXML and reads it back");
    const QString score_file = exports.filePath("mcp-score.musicxml");
    const auto exported_score =
        agent.tool("export_musicxml", {{"section", 0}, {"track", 0}, {"path", score_file}}).value("structuredContent").toObject();
    check(exported_score.value("path").toString() == score_file);
    // The file was read back from disk and plays what the part plays.
    check(exported_score.value("matches").toBool() && exported_score.value("notes").toInt() == 3);
    check(isError(agent.tool("export_musicxml", {{"section", 0}, {"track", 0}, {"path", score_file}})));
    check(!isError(agent.tool("export_musicxml",
                              {{"section", 0}, {"track", 0}, {"path", score_file}, {"overwrite", true}})));
    check(isError(agent.tool("export_musicxml", {{"section", 0}, {"path", "relative.musicxml"}})));
    check(isError(agent.tool("export_musicxml", {{"section", 0}, {"path", exports.filePath("score.wav")}})));
    // Back to treble, C major and the song's meter, so the import can be
    // seen to bring the file's own.
    check(!isError(agent.tool("set_clef", {{"track", 0}, {"section", 0}, {"clef", "Treble"}})));
    check(!isError(agent.tool("set_key_signature", {{"section", 0}, {"fifths", 0}})));
    check(!isError(agent.tool("set_time_signature", {{"section", 0}, {"song", true}})));
    check(!isError(agent.tool("write_notes", {{"track", 0}, {"section", 0}, {"replace", true}, {"notes", QJsonArray{}}})));
    const auto imported = agent.tool("import_musicxml", {{"track", 0}, {"section", 0}, {"replace", true}, {"path", score_file}})
                              .value("structuredContent").toObject();
    check(imported.value("written").toInt() == 3 && imported.value("dropped").toInt() == 0);
    const auto back = agent.tool("get_pattern", {{"track", 0}, {"section", 0}}).value("structuredContent").toObject();
    QStringList back_notes;
    for (const auto& note : back.value("notes").toArray())
        back_notes << QStringLiteral("%1@%2/%3").arg(note.toObject().value("key").toInt())
                          .arg(note.toObject().value("step").toDouble())
                          .arg(note.toObject().value("length").toDouble());
    back_notes.sort();
    check(back_notes == QStringList({"60@0/4", "64@8/4", "67@8/4"}));
    const auto reimported = agent.tool("get_score", {{"track", 0}, {"section", 0}}).value("structuredContent").toObject();
    check(reimported.value("clef").toString() == "Bass" && reimported.value("key").toObject().value("fifths").toInt() == -2 &&
          reimported.value("time").toString() == "3/4");
    // One undo takes back the notes and the clef, key and meter with them.
    check(textOf(agent.tool("call", {{"object", "songModel"}, {"method", "undo"}})) == "true");
    const auto undone = agent.tool("get_pattern", {{"track", 0}, {"section", 0}}).value("structuredContent").toObject();
    check(undone.value("notes").toArray().isEmpty());
    check(agent.tool("get_score", {{"track", 0}, {"section", 0}}).value("structuredContent").toObject()
              .value("clef").toString() == "Treble");
    // A score longer than the pattern, or a bad document, is refused
    // without touching the song.
    const QString long_score = QStringLiteral(
        "<score-partwise version=\"4.0\"><part-list><score-part id=\"P1\"><part-name>L</part-name></score-part></part-list>"
        "<part id=\"P1\"><measure number=\"1\"><attributes><divisions>1</divisions></attributes>"
        "<note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice><type>whole</type></note></measure>"
        "<measure number=\"2\"><note><pitch><step>D</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice><type>whole</type></note></measure>"
        "<measure number=\"3\"><note><pitch><step>E</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice><type>whole</type></note></measure>"
        "</part></score-partwise>");
    const auto too_long = agent.tool("import_musicxml", {{"track", 0}, {"section", 0}, {"xml", long_score}});
    check(isError(too_long) && textOf(too_long).contains("set_section"));
    check(isError(agent.tool("import_musicxml", {{"xml", "this is not xml <"}})));
    check(isError(agent.tool("import_musicxml", {{"path", exports.filePath("missing.musicxml")}})));
    check(isError(agent.tool("import_musicxml", {})));
    check(isError(agent.tool("import_musicxml", {{"xml", long_score}, {"path", score_file}})));
    const auto untouched = agent.tool("get_pattern", {{"track", 0}, {"section", 0}}).value("structuredContent").toObject();
    check(untouched.value("notes").toArray().isEmpty());

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent writes lyrics, in any script");
    {
        const auto written = agent.tool(
            "write_notes", {{"track", 0}, {"section", 0}, {"replace", true},
                            {"notes", QJsonArray{QJsonObject{{"step", 0}, {"key", "C4"}, {"length", 4}, {"lyric", "Hal-"}},
                                                 QJsonObject{{"step", 4}, {"key", "D4"}, {"length", 4}, {"lyric", "le-"}},
                                                 QJsonObject{{"step", 8}, {"key", "E4"}, {"length", 4}}}}});
        check(!isError(written));
        QStringList sung;
        for (const auto& note : agent.tool("get_pattern", {{"track", 0}, {"section", 0}})
                                    .value("structuredContent").toObject().value("notes").toArray())
            sung << note.toObject().value("lyric").toString();
        check(sung == QStringList({"Hal-", "le-", ""}));
        // A line of Hebrew laid on the part, one syllable per note.
        const QString hebrew = QString::fromUtf8("\xD7\x94\xD6\xB7\xD7\x9C-\xD7\x9C\xD7\x95\xD6\xBC-\xD7\x99\xD6\xB8\xD7\x94");
        const auto laid = agent.tool("set_lyrics", {{"track", 0}, {"section", 0}, {"text", hebrew}})
                              .value("structuredContent").toObject();
        check(laid.value("placed").toInt() == 3 && laid.value("syllables").toInt() == 3 &&
              laid.value("lyrics").toString() == hebrew);
        const auto scored = agent.tool("get_score", {{"track", 0}, {"section", 0}}).value("structuredContent").toObject();
        check(scored.value("notes").toArray().at(2).toObject().value("lyric").toString() ==
              QString::fromUtf8("\xD7\x99\xD6\xB8\xD7\x94"));
        check(scored.value("musicxml").toString().contains(QString::fromUtf8("\xD7\x99\xD6\xB8\xD7\x94")));
        // One undo takes the line back.
        check(textOf(agent.tool("call", {{"object", "songModel"}, {"method", "undo"}})) == "true");
        check(ctx.pattern.lyricsText() == "Hal-le-");
        check(isError(agent.tool("set_lyrics", {{"track", 0}, {"section", 0}})));
        check(isError(agent.tool("write_notes", {{"notes", QJsonArray{QJsonObject{{"step", 0}, {"key", 60},
                                                                                   {"lyric", QString(300, 'a')}}}}})));
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the guide narrates what the agent did");
    check(!isError(agent.tool("write_notes",
                              {{"track", 0},
                               {"section", 0},
                               {"notes", QJsonArray{QJsonObject{{"step", 0}, {"key", "C4"}}}}})));
    const auto feed =
        agent.tool("guide_events", {{"count", 5}}).value("structuredContent").toObject();
    const auto events = feed.value("events").toArray();
    check(!events.isEmpty());
    const auto last = events.last().toObject();
    check(last.value("source").toString() == "mcp");
    check(last.value("kind").toString() == "note_added");
    check(!last.value("narration").toString().isEmpty());
    check(last.value("narration").toString().contains("agent", Qt::CaseInsensitive));
    // The producer's own transport move is attributed to the user, not the agent.
    controller.togglePlayback();
    VerifyContext::settle(20);
    const auto feed2 =
        agent.tool("guide_events", {{"count", 5}}).value("structuredContent").toObject();
    const auto events2 = feed2.value("events").toArray();
    check(!events2.isEmpty());
    const auto last2 = events2.last().toObject();
    check(last2.value("source").toString() == "user");
    check(last2.value("kind").toString() == "transport_started");
    controller.togglePlayback();
    VerifyContext::settle(20);

    // ---------------------------------------------------------------------
    ctx.reached("mcp: the agent walks a guide lesson and asks it a question");
    const auto lessons =
        agent.tool("guide_lesson", {{"action", "list"}}).value("structuredContent").toObject();
    QStringList lesson_ids;
    for (const auto& lesson : lessons.value("lessons").toArray())
        lesson_ids << lesson.toObject().value("id").toString();
    check(lesson_ids.contains("first-beat"));
    const auto started = agent.tool("guide_lesson", {{"action", "start"}, {"id", "first-beat"}})
                             .value("structuredContent")
                             .toObject();
    check(started.value("active").toBool());
    check(started.value("step").toInt() == 0);
    check(started.value("steps").toInt() > 1);
    const auto stepped = agent.tool("guide_lesson", {{"action", "next"}})
                             .value("structuredContent")
                             .toObject();
    check(stepped.value("step").toInt() == 1);
    const auto backed = agent.tool("guide_lesson", {{"action", "back"}})
                            .value("structuredContent")
                            .toObject();
    check(backed.value("step").toInt() == 0);
    check(agent.tool("guide_lesson", {{"action", "stop"}})
              .value("structuredContent")
              .toObject()
              .value("active") == QJsonValue(false));
    check(isError(agent.tool("guide_lesson", {{"action", "start"}, {"id", "no-such-lesson"}})));
    check(isError(agent.tool("guide_lesson", {{"action", "dance"}})));
    const auto triad =
        agent.tool("guide_ask", {{"question", "what is a triad"}}).value("structuredContent").toObject();
    check(triad.value("source").toString() == "guide");
    check(triad.value("answer").toString().contains("third", Qt::CaseInsensitive));
    check(isError(agent.tool("guide_ask", {{"question", "   "}})));
    check(isError(agent.tool("guide_ask", {})));

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
              alone_list->value("result").toObject().value("tools").toArray().size() == names.size());
        const auto refused = alone.tool("get_session");
        check(isError(refused) && textOf(refused).contains("not running"));
        check(alone.close());
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: a second Blokkily never takes the socket from a busy one");
    {
        // A Windows pipe is named bare or in full (\\.\pipe\name, what the
        // server reports); a lock claimed under one spelling is found under
        // the other.
        const QString pipe = QStringLiteral("blokkily-probe-%1").arg(QCoreApplication::applicationPid());
        const blokkily::mcp::SocketLock full(QStringLiteral("\\\\.\\pipe\\") + pipe);
        check(full.held());
        check(blokkily::mcp::SocketLock::taken(pipe));
        check(!blokkily::mcp::SocketLock(pipe).held());
    }
    {
        // A window busy on its own thread accepts nothing until it is free,
        // so connections queue up; a full queue makes it look gone to anyone
        // who only knocks. A second window started then must still leave the
        // socket to the one that has it.
        std::vector<std::unique_ptr<QLocalSocket>> queued;
#ifndef Q_OS_WIN
        // A Unix socket's queue fills; a Windows pipe has instances instead,
        // and a connect to a busy one blocks until one frees, so the queue is
        // filled only where it is a queue. The lock decides on both.
        for (int i = 0; i < 400; ++i) {
            queued.push_back(std::make_unique<QLocalSocket>());
            queued.back()->connectToServer(socket);
        }
#endif
        McpTools rival_tools(song, ctx.pattern, controller, ctx.transport, ctx.window);
        {
            const blokkily::mcp::SocketLock claim(socket);
            check(!claim.held());
            check(blokkily::mcp::SocketLock::taken(socket));
            McpServer rival(rival_tools);
            check(!rival.listen(socket, claim));
        }
        queued.clear();
        QCoreApplication::processEvents();
        // A new agent still reaches this window, by the same path.
        Agent later(socket, false);
        check(later.started());
        check(later.request("initialize", {{"protocolVersion", "2025-06-18"}}).has_value());
        const auto reached = later.tool("get_session");
        check(!isError(reached) &&
              reached.value("structuredContent").toObject().value("tracks").toArray().size() ==
                  static_cast<int>(song.song().tracks.size()));
        check(later.close());
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: a bridge waits for a window that is still starting");
    {
        // The window has claimed the socket but not yet listens on it, as
        // while it loads; the bridge must neither start another window nor
        // say Blokkily is not running, but wait and be answered.
        const QString starting = socket + "-starting";
        const blokkily::mcp::SocketLock claim(starting);
        check(claim.held());
        McpTools late_tools(song, ctx.pattern, controller, ctx.transport, ctx.window);
        McpServer late(late_tools);
        Agent waiting(starting, false);
        check(waiting.started());
        check(waiting.request("initialize", {{"protocolVersion", "2025-06-18"}}).has_value());
        bool listening = false;
        QTimer::singleShot(1500, [&] { listening = late.listen(starting, claim); });
        const auto answered = waiting.tool("get_session");
        check(listening);
        check(!isError(answered));
        check(waiting.close());
    }

    // ---------------------------------------------------------------------
    ctx.reached("mcp: a bridge waits for a window that is still starting");
    {
        // Two windows are open; the first serves the agent. When it closes,
        // the second takes the socket over, and the agent's next call is
        // answered there through the same bridge, which neither gives up nor
        // starts a window of its own.
        const QString shared = socket + "-shared";
        auto first_lock = std::make_unique<blokkily::mcp::SocketLock>(shared);
        check(first_lock->held());
        McpTools first_tools(song, ctx.pattern, controller, ctx.transport, ctx.window);
        auto first = std::make_unique<McpServer>(first_tools);
        check(first->listen(shared, *first_lock));
        McpTools second_tools(song, ctx.pattern, controller, ctx.transport, ctx.window);
        McpServer second(second_tools);
        QString took_over;
        QObject::connect(&second, &McpServer::tookOver, [&](const QString& path) { took_over = path; });
        check(!second.serveWhenFree(shared, 100) && !second.listening());
        Agent agent_of_both(shared, false);
        check(agent_of_both.started());
        check(agent_of_both.request("initialize", {{"protocolVersion", "2025-06-18"}}).has_value());
        check(!isError(agent_of_both.tool("get_session")));
        check(!second.listening());
        // The first window closes.
        first.reset();
        first_lock.reset();
        const auto after_close = agent_of_both.tool("get_session");
        check(!isError(after_close) && second.listening() && !took_over.isEmpty());
        check(agent_of_both.close());
    }
    ctx.reached("mcp: closing the first window hands MCP to another");

    // ---------------------------------------------------------------------
    ctx.reached("mcp: closing the agent's end ends the bridge");
    check(agent.close());

    std::ostringstream details;
    details << " | tools=" << names.size() << " | heard after write " << after
            << " | level lane 0 -> " << held_at_zero << ", 0.25 -> " << held_up
            << " | set_parameter 0 -> " << set_to_zero << ", 0.25 -> " << set_up
            << " | soundfont preset heard " << soundfont_heard << " | mix " << mix_lufs
            << " LUFS, lead " << lead_lufs << " LUFS, lowMid share " << lead_low_mid
            << ", fader -6 dB -> " << fader_drop << " LU | +50 cents -> pitch x" << cents_ratio
            << " | master output -12 dB -> " << master_drop << " LU, return -> " << return_drop << " LU";
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("mcp", run_mcp);

}  // namespace
}  // namespace blokkily::verify
