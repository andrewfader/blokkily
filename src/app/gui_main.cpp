#include "keyboard_model.hpp"
#include "app_controller.hpp"
#include "llm_model.hpp"
#include "score_model.hpp"
#include "../guide/guide_controller.hpp"
#include "plugin_run_loop_qt.hpp"
#include "mcp_bridge.hpp"
#include "mcp_server.hpp"
#include "mcp_tools.hpp"
#include "open_channel.hpp"
#include "language.hpp"
#include "verify/harness.hpp"
#include "blokkily/plugins/sandbox.hpp"

#include "../llm/scripted_backend.hpp"

#include <cstdio>

#include <QFileInfo>
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

namespace {

// How often unsaved work is copied aside: $BLOKKILY_AUTOSAVE_MS, else 30 s.
int autosave_ms() {
    bool ok = false;
    const int configured = qEnvironmentVariableIntValue("BLOKKILY_AUTOSAVE_MS", &ok);
    return ok && configured > 0 ? configured : 30000;
}

} // namespace

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
    // Hebrew lyrics under the staff draw in this face on every machine,
    // whatever fonts the system has.
    (void)family_of(":/fonts/NotoSansHebrew-Regular.ttf");
    QFont interface_font(interface_family);
    interface_font.setPixelSize(12);
    QGuiApplication::setFont(interface_font);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"verify", "Run the whole-app verification scenario and exit."});
    parser.addOption({"screenshot", "Write verification screenshot to path.", "path"});
    parser.addOption({"clap-fixture", "Scan this CLAP module during verification.", "path"});
    parser.addOption({"vst3-fixture", "Scan this VST3 bundle during verification.", "path"});
    parser.addOption({"lv2-fixture", "Scan this LV2 bundle during verification.", "path"});
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
    parser.addOption({"isolate-plugins", "During verification, run each plugin in a helper "
                                         "process, as they ship; otherwise a gate loads them in "
                                         "the window."});
    parser.addOption({"faulty-clap", "During verification, a CLAP that crashes on cue.", "path"});
    parser.addOption({"auto-hide", "During verification, let the side bars auto-hide as they "
                                   "ship; otherwise a gate docks them."});
    // What the desktop hands over when a .blok is opened from the file
    // manager (resources/blokkily.desktop: Exec=blokkily %f), or a path or
    // file:// URL typed after the command.
    parser.addPositionalArgument("project", "A Blokkily project (.blok) to open.", "[project]");
    parser.addOption({"new-window", "Open the project in a window of its own, even when Blokkily "
                                    "is already running."});
    parser.process(app);
    const QStringList launch_files = parser.positionalArguments();
    const QString launch_file = launch_files.isEmpty() ? QString() : launch_files.front();

    // A file opened while Blokkily runs goes to the window already open
    // (open_channel.hpp), which opens it itself only when it holds nothing
    // yet, and otherwise in a window of its own. A gate
    // takes part only on a channel of its own naming.
    const bool verifying = parser.isSet("verify");
    const bool use_open_channel = !parser.isSet("new-window") &&
        (!verifying || !qEnvironmentVariable("BLOKKILY_OPEN_SOCKET").isEmpty());
    const QString open_socket = open_channel::socketPath();
    // Each window also listens on a channel named for the song it holds
    // (features/multiple_windows.feature), so a song open in one window is
    // brought forward there, even by `--new-window`, never opened twice.
    const bool use_file_channel =
        !verifying || !qEnvironmentVariable("BLOKKILY_OPEN_SOCKET").isEmpty();
    if (use_file_channel && !launch_file.isEmpty() &&
        open_channel::forward(open_channel::fileSocketPath(launch_file),
                              open_channel::absoluteFile(launch_file)))
        return 0;
    if (!verifying && use_open_channel && !launch_file.isEmpty() &&
        open_channel::forward(open_socket, open_channel::absoluteFile(launch_file)))
        return 0;

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
    // The floating guide: narration feed, mascot overlay, lessons, Q&A. The
    // pattern's note edits feed the same bus the controller's commands do.
    // Theory questions the rules miss borrow the prompt bar's backend.
    GuideController guide(&controller, &song, &pattern, &transport, nullptr,
                          !parser.isSet("verify"));
    guide.setLlmModel(&llm);
    pattern.setEventBus(&controller.eventBus());
    // The score view's projection of the canonical pattern: re-spelled on
    // every pattern change; notes entered on the staff go into the pattern.
    ScoreModel score(&pattern, &song);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("songModel", &song);
    engine.rootContext()->setContextProperty("patternModel", &pattern);
    engine.rootContext()->setContextProperty("appController", &controller);
    engine.rootContext()->setContextProperty("transport", &transport);
    engine.rootContext()->setContextProperty("keyboardModel", &keyboard);
    engine.rootContext()->setContextProperty("llmModel", &llm);
    engine.rootContext()->setContextProperty("guideController", &guide);
    engine.rootContext()->setContextProperty("scoreModel", &score);
    // A gate drives the rail and the mixer at the canonical 1280 px, where
    // they would auto-hide: it docks them unless it is the gate that proves
    // the auto-hiding (features/window_layout.feature).
    // A gate pumps the engine by hand, so a plugin's helper is waited for;
    // most gates load plugins in the window, where they read the fixture's
    // own logs (features/plugin_isolation.feature).
    if (parser.isSet("verify")) {
        blokkily::set_sandbox_waits(true);
        if (parser.isSet("isolate-plugins")) controller.setPluginIsolation(true);
    }
    if (parser.isSet("verify") && !parser.isSet("auto-hide")) {
        controller.setUiSetting(QStringLiteral("ui/autoHideRail"), false);
        controller.setUiSetting(QStringLiteral("ui/autoHideMixer"), false);
    }
    // A gate reads the window in English whatever the machine's language,
    // unless it is the gate that switches it (features/language.feature).
    if (parser.isSet("verify")) controller.setUiSetting(QStringLiteral("ui/language"), QStringLiteral("en"));
    blokkily::Language language(controller, engine);
    engine.loadFromModule("Blokkily", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    auto* main_window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QObject::connect(&controller, &AppController::raiseRequested, &controller, [main_window] {
        if (main_window == nullptr) return;
        main_window->show();
        main_window->raise();
        main_window->requestActivate();
    });
    open_channel::Server open_server;
    if (use_open_channel) {
        QString why;
        if (open_server.listen(open_socket, &why)) {
            // The first window is the studio's: it remembers where the MIDI
            // clock goes. Other windows leave the clock off, so outboard
            // gear never hears two.
            if (!verifying) {
                controller.rememberMidiClock();
                controller.restoreMidiClock();
            }
            QObject::connect(&open_server, &open_channel::Server::fileRequested, &controller,
                             [&controller](const QString& path) {
                                 // A song already open here stays; the file
                                 // gets a window of its own instead.
                                 if (controller.routeOpenRequest(path))
                                     emit controller.raiseRequested();
                             });
        }
    }
    // The channel for the song this window holds follows it through every
    // open, save-as and new song.
    open_channel::Server file_server;
    QString file_channel_song;
    if (use_file_channel) {
        QObject::connect(&file_server, &open_channel::Server::fileRequested, &controller,
                         [&controller] { emit controller.raiseRequested(); });
        QObject::connect(&controller, &AppController::projectStatusChanged, &file_server,
                         [&controller, &file_server, &file_channel_song] {
                             const QString song = controller.projectPath();
                             if (song == file_channel_song) return;
                             file_channel_song = song;
                             file_server.close();
                             QString why;
                             if (!song.isEmpty() &&
                                 !file_server.listen(open_channel::fileSocketPath(song), &why))
                                 std::fprintf(stderr, "Blokkily: %s cannot be found by other windows: %s\n",
                                              qPrintable(QFileInfo(song).fileName()), qPrintable(why));
                         });
    }

    McpTools mcp_tools(song, pattern, controller, transport,
                       qobject_cast<QQuickWindow*>(engine.rootObjects().front()), &guide);
    McpServer mcp_server(mcp_tools);
    // A render an agent asked for is abandoned when the agent hangs up.
    controller.setRenderCancel([&mcp_server] { return mcp_server.abandoned(); });
    QObject::connect(controller.progress(), &LoadProgress::holdRequests, &mcp_server,
                     &McpServer::setHeld);
    if (serve_mcp) {
        QObject::connect(&mcp_server, &McpServer::tookOver, &app,
                         [&app, &mcp_server] { app.setProperty("blokkilyMcpSocket", mcp_server.path()); });
        if (mcp_server.listen(mcp_socket, *mcp_lock))
            app.setProperty("blokkilyMcpSocket", mcp_server.path());
        else if (!verifying && !mcp_lock->held())
            // Another window serves the agent; this one takes over when it
            // closes (features/multiple_windows.feature).
            (void)mcp_server.serveWhenFree(mcp_socket);
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
        // Recovery is the user's own folder; a gate uses it only when it
        // names one of its own.
        if (!qEnvironmentVariable("BLOKKILY_RECOVERY_DIR").isEmpty())
            controller.startAutosave(RecoveryStore::default_directory(), autosave_ms());
        // A gate that names a project opens it as a launch does, before the
        // scenario runs.
        if (!launch_file.isEmpty())
            QTimer::singleShot(0, &controller, [&controller, launch_file] {
                (void)controller.loadProjectFile(launch_file);
            });
        blokkily::verify::schedule(context, parser.value("scenario"));
        return app.exec();
    } else {
        // The session opens with something to hear. A workstation that makes no
        // sound until a plugin has been hunted down is not one a producer can
        // sit down at, so every track without an instrument is given the
        // machine's General MIDI bank before anything else happens.
        // Unsaved work is copied aside while it is worked on, and whatever a
        // window that crashed left behind is offered back.
        controller.startAutosave(RecoveryStore::default_directory(), autosave_ms());
        // A song opened at launch brings its own instruments, so the bank
        // is only loaded when there is none, or the song cannot be read.
        QTimer::singleShot(0, &controller, [&controller, launch_file] {
            if (launch_file.isEmpty() || !controller.loadProjectFile(launch_file))
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
