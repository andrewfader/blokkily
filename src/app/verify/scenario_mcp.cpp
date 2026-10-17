// features/mcp.feature, run on its own by the bdd_mcp gate (`--scenario mcp`).
// The gate is the running window, serving MCP on the socket main() opened for
// --mcp-socket. Each scenario talks to it the way an agent does: through the
// real `blokkily --mcp` bridge, started as a child process, one JSON-RPC line
// at a time on the bridge's stdin and stdout. While a reply is awaited the
// window's event loop keeps turning, because it is the window that answers.

#include "verify/harness.hpp"
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
                                 "describe", "call", "get_property", "analyze_render"})
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
    const float set_to_zero = play_bar();
    check(set_to_zero == 0.0F);
    check(!isError(agent.tool("set_parameter", {{"track", 0}, {"parameter", 0}, {"value", 0.25}})));
    const float set_up = play_bar();
    check(set_up > 0.001F);
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
        check(isError(agent.tool("place_section", {{"section", added}, {"bar", 8}})));
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
        const auto hang_up = QObject::connect(controller.progress(), &LoadProgress::frameDrawn, [&] {
            if (hung_up) return;
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
        // The window still answers the next agent.
        Agent next(socket, false);
        check(next.started());
        check(next.request("initialize", {{"protocolVersion", "2025-06-18"}}).has_value());
        check(!isError(next.tool("get_session")));
        check(next.close());
    }

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
