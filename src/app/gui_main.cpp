#include "keyboard_model.hpp"
#include "app_controller.hpp"
#include "llm_model.hpp"
#include "plugin_run_loop_qt.hpp"
#include "mcp_bridge.hpp"
#include "mcp_server.hpp"
#include "mcp_tools.hpp"
#include "verify/harness.hpp"

#include "../llm/scripted_backend.hpp"

#include <cstdio>

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>

#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <clocale>
#include <memory>
#include <optional>
#include <string_view>


int main(int argc, char* argv[]) {
    // `blokkily --mcp` is the agent's end of MCP: it needs no window, no
    // display and no audio, so it runs before any of them exist.
    for (int index = 1; index < argc; ++index)
        if (std::string_view(argv[index]) == "--mcp") return runMcpBridge(argc, argv);

    QGuiApplication app(argc, argv);
    // Qt sets the C locale from the environment on Unix, which would make
    // every printf-family number a plugin or a library parses depend on the
    // machine's decimal mark; the numeric locale stays "C".
    std::setlocale(LC_NUMERIC, "C");

    QGuiApplication::setApplicationName("Blokkily");
    QGuiApplication::setOrganizationName("Blokkily");

    // The controls are restyled throughout, which only a non-native style
    // allows; Fusion is what Linux picks by default, so every platform now
    // draws the same thing rather than macOS's or Windows' native controls.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    // The layout is drawn for the bundled faces; without them it would take
    // the metrics of whatever the system calls sans-serif and could run off
    // the window. Each face is used by the family name it registers under.
    const auto family_of = [](const char* face) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(face));
        const auto families = QFontDatabase::applicationFontFamilies(id);
        if (families.isEmpty()) {
            std::fprintf(stderr, "Blokkily: could not load the bundled font %s\n", face);
            return QString();
        }
        return families.front();
    };
    const QString interface_family = family_of(":/fonts/IBMPlexSans-Regular.ttf");
    (void)family_of(":/fonts/IBMPlexSans-Bold.ttf");
    (void)family_of(":/fonts/JetBrainsMono-Regular.ttf");
    (void)family_of(":/fonts/JetBrainsMono-Bold.ttf");
    QFont interface_font(interface_family);
    interface_font.setPixelSize(12);
    QGuiApplication::setFont(interface_font);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"verify", "Run the whole-app verification scenario and exit."});
    parser.addOption({"screenshot", "Write verification screenshot to path.", "path"});
    parser.addOption({"clap-fixture", "Scan this CLAP module during verification.", "path"});
    parser.addOption({"vst3-fixture", "Scan this VST3 bundle during verification.", "path"});
    parser.addOption({"soundfont-fixture", "Render this SF2/SF3 during verification.", "path"});
    parser.addOption({"clap-effect-fixture", "Scan this CLAP effect during verification.",
                      "path"});
    parser.addOption({"vst3-effect-fixture", "Load this VST3 effect during verification.", "path"});
    parser.addOption({"project", "Save and reload the verification project here.", "path"});
    parser.addOption({"view", "Leave the interface in this editor view.", "name"});
    parser.addOption({"export", "Bounce the verification arrangement here.", "path"});
    parser.addOption({"scan-fixture", "Scan this directory out of process during verification.",
                      "path"});
    parser.addOption({"tuning", "Leave the session in this tuning.", "name"});
    parser.addOption({"scale", "Leave the session in this scale.", "name"});
    parser.addOption({"surface", "Leave the keyboard on this playable surface.", "name"});
    parser.addOption({"layout", "Leave the isomorphic grid on this layout.", "name"});
    parser.addOption({"orientation", "Leave the surface running ACROSS or DOWN.", "name"});
    parser.addOption({"scenario", "Run only this scenario group (midi, chords) and exit.", "name"});
    parser.addOption({"mcp", "Speak MCP on stdin and stdout for an agent, driving the running "
                              "Blokkily (starting it when none is running)."});
    parser.addOption({"mcp-socket", "During verification, serve MCP on this socket.", "path"});
    parser.process(app);

    // Plugins' timers, watched descriptors and JUCE's message queue are served
    // by the application's own event loop (item 2.6). Installed before any
    // plugin exists and removed after the last one is gone.
    QtPluginRunLoop plugin_run_loop;
    const blokkily::ScopedPluginRunLoop plugin_run_loop_installed(plugin_run_loop);

    // Agents drive the window through MCP (features/mcp.feature). A normal
    // launch serves the user's own socket; verification serves only the one
    // its gate names, so a test never answers for, or displaces, a Blokkily
    // the user has open. A gate's socket is named for its process, so two
    // test runs at once never meet on one. The socket is claimed before
    // anything loads: a bridge that finds nobody answering yet sees a window
    // on its way and waits for it rather than starting another.
    const bool serve_mcp = parser.isSet("verify") ? parser.isSet("mcp-socket")
                                                  : qgetenv("BLOKKILY_MCP") != "0";
    const QString mcp_socket = parser.isSet("verify")
        ? QStringLiteral("%1-%2").arg(parser.value("mcp-socket")).arg(QCoreApplication::applicationPid())
        : blokkily::mcp::socketPath();
    std::optional<blokkily::mcp::SocketLock> mcp_lock;
    if (serve_mcp) mcp_lock.emplace(mcp_socket);

    // Held for the life of the application, so rebuilding the audio graph
    // never tears the plugin host runtime down between two instruments.
    SongModel song;
    PatternModel pattern(&song);
    Transport transport;
    auto output = parser.isSet("verify") ? std::make_unique<blokkily::RtAudioOutput>(
        blokkily::RtAudioOutput::Mode::deterministic) : nullptr;
    auto* verification_output = output.get();
    // Verification plays a MIDI keyboard through the same decode, tuning and
    // routing path a port's thread runs, without needing one plugged in.
    auto midi = parser.isSet("verify") ? std::make_unique<blokkily::MidiInput>(
        blokkily::MidiInput::Mode::deterministic) : nullptr;
    AppController controller(&song, &pattern, &transport, nullptr, std::move(output),
                             std::move(midi));
    KeyboardModel keyboard(&song, &pattern, &controller);
    LlmModel llm(&song, &pattern);
    // Verification answers prompts from a script, not a server: the gate
    // drives the same ask → propose → apply path with bytes it controls.
    if (parser.isSet("verify"))
        llm.setBackendForTesting(std::make_unique<blokkily::llm::ScriptedBackend>());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("songModel", &song);
    engine.rootContext()->setContextProperty("patternModel", &pattern);
    engine.rootContext()->setContextProperty("appController", &controller);
    engine.rootContext()->setContextProperty("transport", &transport);
    engine.rootContext()->setContextProperty("keyboardModel", &keyboard);
    engine.rootContext()->setContextProperty("llmModel", &llm);
    engine.loadFromModule("Blokkily", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    McpTools mcp_tools(song, pattern, controller, transport,
                       qobject_cast<QQuickWindow*>(engine.rootObjects().front()));
    McpServer mcp_server(mcp_tools);
    if (serve_mcp) {
        if (mcp_server.listen(mcp_socket, *mcp_lock))
            app.setProperty("blokkilyMcpSocket", mcp_server.path());
        else
            std::fprintf(stderr, "Blokkily: MCP is off: %s\n", qPrintable(mcp_server.error()));
    }

    if (parser.isSet("verify")) {
        controller.scanPluginPaths(
            {parser.value("clap-fixture").toStdString()},
            {parser.value("vst3-fixture").toStdString()},
            {parser.value("soundfont-fixture").toStdString()});
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        if (window == nullptr) return 2;
        // Gates render one canonical frame whatever screen the run has;
        // what the window chose for itself is kept for the gate to judge.
        const QSize opened = window->size();
        window->resize(1280, 1080);
        // The scenario group runs from the event loop and reads this block's
        // objects through the context, so the loop runs while they still exist.
        blokkily::verify::VerifyContext context(app, parser, window, song, pattern, transport,
                                                controller, keyboard, llm,
                                                verification_output);
        context.opened_size = opened;
        blokkily::verify::schedule(context, parser.value("scenario"));
        return app.exec();
    } else {
        // The session opens with something to hear. A workstation that makes no
        // sound until a plugin has been hunted down is not one a producer can
        // sit down at, so every track without an instrument is given the
        // machine's General MIDI bank before anything else happens.
        QTimer::singleShot(0, &controller, [&controller] {
            (void)controller.loadDefaultInstrument();
        });
        // Discovery begins automatically on every normal launch and never
        // delays the window: candidates are described by a helper process, one
        // at a time, so a plugin that hangs or crashes costs one process and
        // the browser keeps filling from the rest.
        QTimer::singleShot(0, &controller, &AppController::scanPlugins);
    }
    return app.exec();
}
