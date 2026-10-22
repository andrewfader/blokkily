// features/loading_progress.feature, run on its own by the bdd_loading_progress
// gate (`--scenario loading`). In the rendered window: a song opening and an
// export each show the loading card with a bar that fills as the work goes,
// read off the frames the work lets the window draw; nothing typed, clicked or
// asked over MCP lands while a load is shown; the export is the same file
// whether its progress is shown or not; the plugin scan fills the strip along
// the transport bar; a scan the cache already knows reaches the browser at
// once; and the closed tour looks for nothing while the window is laid out.

#include "verify/harness.hpp"

#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

// One frame a load let the window draw, as the window drew it.
struct Frame {
    bool active = false;
    bool overlay_shown = false;
    bool card_usable = false;
    QString title;
    QString detail;
    double fraction = -1.0;
    // How much of the bar is filled, as a share of the bar.
    double filled = 0.0;
};

void run_loading(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    auto& transport = ctx.transport;
    const auto& parser = ctx.parser;
    auto* const progress = controller.progress();
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    std::ostringstream details;

    // ---- the closed tour looks for nothing ----------------------------------
    // Its NEXT/DONE label was a binding over a walk of every item in the
    // window, re-run whenever any of them changed size or visibility, tour or
    // no tour: eleven seconds of a release build's start-up went on it.
    auto* tour = ctx.named("tutorial");
    check(tour != nullptr && !tour->property("running").toBool());
    lay_out();
    window->resize(1100, 900);
    lay_out();
    window->resize(1280, 1080);
    lay_out();
    const int searches = tour != nullptr ? tour->property("searches").toInt() : -1;
    check(searches == 0);
    if (searches != 0) std::cerr << "REGRESSION: the closed tour searched " << searches << " times\n";
    details << " | closed-tour searches=" << searches;
    reached("loading: the closed tour looks for nothing while the window is laid out");

    // ---- a song to open: the CLAP fixture and a SoundFont ------------------
    check(controller.verifyClap(parser.value("clap-fixture")));
    while (song.trackCount() < 2) song.addTrack();
    blokkily::InstrumentSlot bank;
    bank.format = "SoundFont";
    bank.path = parser.value("soundfont-fixture").toStdString();
    song.setInstrument(1, bank);
    controller.setTempo(120.0);
    for (int track = 0; track < 2; ++track) {
        song.selectTrack(track);
        song.selectPattern(0);
        for (int step = 0; step < 16; ++step) pattern.clearStep(step);
        pattern.setStepKey(0, 60);
        pattern.setStepKey(8, 67);
    }
    lay_out();
    const QString project = parser.value("project");
    check(!project.isEmpty() && controller.saveProjectFile(project));

    // Every load is shown from its first report and draws a frame at every
    // percent, so what the gate sees does not hang on the machine's speed.
    progress->setTiming(0, 0);
    std::vector<Frame> frames;
    std::function<void()> on_frame;
    QObject::connect(progress, &LoadProgress::frameDrawn, &ctx.app, [&] {
        (void)window->grabWindow();
        Frame frame;
        frame.active = progress->active();
        frame.title = progress->title();
        frame.detail = progress->detail();
        frame.fraction = progress->fraction();
        auto* overlay = ctx.named("loadingOverlay");
        auto* bar = ctx.named("loadingBar");
        auto* fill = ctx.named("loadingFill");
        frame.overlay_shown = overlay != nullptr && overlay->isVisible() &&
                              overlay->width() >= window->width() - 1 &&
                              overlay->height() >= window->height() - 1;
        frame.card_usable = VerifyContext::usable(ctx.named("loadingCard"), 240, 60) &&
                            VerifyContext::usable(bar, 200, 6);
        if (bar != nullptr && fill != nullptr && fill->isVisible() && bar->width() > 0)
            frame.filled = fill->width() / bar->width();
        frames.push_back(frame);
        if (on_frame) on_frame();
    });
    const auto steady = [](const std::vector<Frame>& seen) {
        double last = 0.0;
        for (const auto& frame : seen) {
            if (frame.fraction < 0.0) continue;
            if (frame.fraction + 1e-9 < last) return false;
            last = frame.fraction;
        }
        return true;
    };
    // Every counted frame draws the bar as full as the work said it was.
    const auto drawn_as_told = [](const std::vector<Frame>& seen) {
        for (const auto& frame : seen)
            if (frame.fraction > 0.05 && std::abs(frame.filled - frame.fraction) > 0.03) return false;
        return true;
    };

    // ---- opening it -------------------------------------------------------
    const int rebuilds_before = controller.rebuildCount();
    frames.clear();
    check(controller.loadProjectFile(project));
    const auto opening = frames;
    const QString name = QFileInfo(project).completeBaseName().toUpper();
    const bool opening_shown =
        !opening.empty() &&
        std::all_of(opening.begin(), opening.end(), [&](const Frame& frame) {
            return frame.active && frame.overlay_shown && frame.card_usable &&
                   frame.title == "OPENING " + name;
        });
    check(opening_shown);
    // The instruments are named as they load, each with how many there are.
    const bool named_instrument =
        std::any_of(opening.begin(), opening.end(), [](const Frame& frame) {
            return frame.detail == "BLOKKILY-TEST · 1 OF 2";
        }) &&
        std::any_of(opening.begin(), opening.end(), [&](const Frame& frame) {
            return frame.detail ==
                   QFileInfo(QString::fromStdString(bank.path)).completeBaseName().toUpper() +
                       " · 2 OF 2";
        });
    check(named_instrument);
    check(steady(opening) && drawn_as_told(opening));
    lay_out();
    check(!progress->active() && ctx.named("loadingOverlay") != nullptr &&
          !ctx.named("loadingOverlay")->isVisible());
    // The song's engine is built once. Replacing the song built it, and the
    // load built it again, activating every plugin twice (a regression).
    const int rebuilds = controller.rebuildCount() - rebuilds_before;
    check(rebuilds == 1);
    if (rebuilds != 1) std::cerr << "REGRESSION: opening a song built its engine " << rebuilds << " times\n";
    // And it plays what was saved.
    controller.seekToStep(0);
    controller.togglePlayback();
    float loudest = 0.0F;
    for (int block = 0; block < 6; ++block) loudest = std::max(loudest, ctx.pump());
    controller.togglePlayback();
    (void)ctx.pump();
    check(loudest > 1e-3F);
    details << " | open frames=" << opening.size() << " rebuilds=" << rebuilds
            << " peak=" << loudest;
    reached("loading: opening a song shows its progress and builds its engine once");

    // ---- exporting it -----------------------------------------------------
    // An agent connected before the export, the way one already working
    // with the window is.
    const QString socket_path = ctx.app.property("blokkilyMcpSocket").toString();
    QLocalSocket agent;
    agent.connectToServer(socket_path);
    check(!socket_path.isEmpty() && agent.waitForConnected(3000));
    const auto send = [&agent](int id, const QString& method, const QJsonObject& params) {
        const QJsonObject message{{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
                                  {"params", params}};
        agent.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + "\n");
        agent.flush();
    };
    // Waits, bounded, for the reply carrying `id`.
    const auto reply_to = [&agent](int id, int milliseconds) -> std::optional<QJsonObject> {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < milliseconds) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            while (agent.canReadLine()) {
                const auto reply = QJsonDocument::fromJson(agent.readLine()).object();
                if (reply.value("id").toInt() == id) return reply;
            }
            (void)agent.waitForReadyRead(5);
        }
        return std::nullopt;
    };
    send(1, "initialize", {{"protocolVersion", "2025-06-18"},
                           {"capabilities", QJsonObject{}},
                           {"clientInfo", QJsonObject{{"name", "gate"}, {"version", "1"}}}});
    check(reply_to(1, 5000).has_value());

    const int events_before = pattern.rowCount();
    const QString exported = parser.value("export");
    bool asked_during_export = false;
    bool answered_during_export = false;
    bool screenshot = false;
    on_frame = [&] {
        const auto& frame = frames.back();
        if (frame.fraction < 0.3) return;
        if (!asked_during_export) {
            asked_during_export = true;
            // A producer reaching for PLAY and typing a note, and an agent
            // asking for the song, half way through.
            if (auto* play = ctx.named("playButton"))
                ctx.click_at(play, {play->width() / 2, play->height() / 2}, Qt::LeftButton);
            ctx.type_key(Qt::Key_Z, QStringLiteral("z"));
            send(2, "tools/call", {{"name", "get_session"}, {"arguments", QJsonObject{}}});
            // The evidence: the window as it was drawn mid-export.
            screenshot = ctx.save_screenshot();
            return;
        }
        if (agent.bytesAvailable() > 0 || agent.waitForReadyRead(1)) answered_during_export = true;
    };
    frames.clear();
    check(!exported.isEmpty() && controller.exportAudioFile(exported, "FLOAT32"));
    on_frame = nullptr;
    const auto exporting = frames;
    const bool export_shown =
        exporting.size() >= 10 &&
        std::all_of(exporting.begin(), exporting.end(), [&](const Frame& frame) {
            return frame.active && frame.overlay_shown && frame.card_usable &&
                   frame.title == "EXPORTING " + QFileInfo(exported).fileName().toUpper();
        });
    check(export_shown);
    const QRegularExpression clock(QStringLiteral(R"(^\d+:\d\d OF \d+:\d\d$)"));
    const bool told_where = std::any_of(exporting.begin(), exporting.end(), [&](const Frame& frame) {
        return clock.match(frame.detail).hasMatch() && frame.fraction > 0.0;
    });
    check(told_where && steady(exporting) && drawn_as_told(exporting) &&
          !exporting.empty() && exporting.back().fraction > 0.97);
    check(screenshot);
    lay_out();
    check(!progress->active() && !ctx.named("loadingOverlay")->isVisible());
    details << " | export frames=" << exporting.size();
    reached("loading: an export shows how far it has got");

    // Nothing pressed or typed while it showed reached the song.
    check(asked_during_export && !transport.playing() && pattern.rowCount() == events_before);
    reached("loading: nothing can be clicked or typed while a load is shown");

    // The agent's question waited for the export and was answered after it.
    const auto session = reply_to(2, 5000);
    check(asked_during_export && !answered_during_export && session.has_value() &&
          session->contains("result"));
    reached("loading: an agent's request waits until the load is over");

    // The same export with its progress never shown draws no frame and lets
    // nothing in, and writes the same file sample for sample: showing the
    // bar changes nothing about what is rendered.
    progress->setTiming(600000, LoadProgress::default_frame_ms);
    frames.clear();
    const QString hidden = QFileInfo(exported).absolutePath() + "/loading-export-unshown.wav";
    check(controller.exportAudioFile(hidden, "FLOAT32"));
    check(frames.empty() && progress->frames() == 0);
    {
        std::string error;
        const auto shown_wave = blokkily::read_wave(exported.toStdString(), &error);
        const auto hidden_wave = blokkily::read_wave(hidden.toStdString(), &error);
        check(shown_wave && hidden_wave && shown_wave->frames > 0 &&
              shown_wave->frames == hidden_wave->frames &&
              shown_wave->interleaved == hidden_wave->interleaved);
        float peak = 0.0F;
        if (shown_wave)
            for (const float sample : shown_wave->interleaved) peak = std::max(peak, std::abs(sample));
        check(peak > 1e-3F);
        details << " | export peak=" << peak;
    }
    reached("loading: an export writes the same file whether its progress is shown or not");

    // CANCEL on an export's progress, pressed a few frames in: the export
    // stops, nothing of it is left, and the window says so.
    {
        progress->setTiming(0, 0);
        const QString abandoned = QFileInfo(exported).absolutePath() + "/loading-export-cancelled.wav";
        QFile::remove(abandoned);
        int drawn = 0;
        bool offered = false;
        on_frame = [&] {
            if (++drawn != 3) return;
            auto* chip = ctx.named("cancelLoad");
            offered = chip != nullptr && chip->isVisible() && VerifyContext::usable(chip, 40, 18);
            if (chip == nullptr) return;
            // A press as the device would send it, to the window, held back
            // from everything but CANCEL.
            const QPointF at = chip->mapToScene({chip->width() / 2, chip->height() / 2});
            QCoreApplication::postEvent(window, new QMouseEvent(QEvent::MouseButtonPress, at, at,
                                                                window->mapToGlobal(at.toPoint()),
                                                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
            QCoreApplication::postEvent(window, new QMouseEvent(QEvent::MouseButtonRelease, at, at,
                                                                window->mapToGlobal(at.toPoint()),
                                                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
        };
        const bool written = controller.exportAudioFile(abandoned, "FLOAT32");
        on_frame = nullptr;
        lay_out();
        const auto notices = controller.notices();
        std::cerr << "cancel: offered " << offered << ", written " << written << ", frames " << drawn << '\n';
        check(offered && !written && !QFile::exists(abandoned) && drawn < 90);
        check(!notices.isEmpty() && notices.back().toMap().value("text").toString().contains("cancelled"));
        check(!progress->active() && !progress->cancellable());
    }
    reached("loading: CANCEL stops an export and leaves nothing behind");

    // ---- the plugin scan ---------------------------------------------------
    // A folder of its own beside the gate's artifacts (a plugin must be on a
    // filesystem it may be mapped executable from): the plugin that scans
    // first, then the one that never returns, so the scan spends its deadline
    // half way along.
    progress->setTiming(0, 0);
    const QDir artifacts(QFileInfo(project).absolutePath());
    const auto fresh_folder = [&artifacts](const QString& name) {
        QDir folder(artifacts.filePath(name));
        (void)folder.removeRecursively();
        (void)artifacts.mkpath(name);
        return folder;
    };
    const QString fixture = parser.value("clap-fixture");
    const QDir scan_folder = fresh_folder(QStringLiteral("loading-scan"));
    check(QFile::copy(fixture, scan_folder.filePath("a-scans.clap")) &&
          QFile::copy(QDir(parser.value("scan-fixture")).filePath("blokkily-hang.clap"),
                      scan_folder.filePath("b-hangs.clap")));
    const std::vector<std::filesystem::path> fixtures{scan_folder.path().toStdString()};
    const auto run_scan = [&](const std::vector<std::filesystem::path>& paths, bool forget,
                              const std::function<void()>& meanwhile) {
        QEventLoop waiting;
        bool completed = false;
        QObject::connect(&controller, &AppController::scanFinished, &waiting,
                         [&] { completed = true; waiting.quit(); });
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &waiting, &QEventLoop::quit);
        guard.start(20000);
        QTimer watch;
        if (meanwhile) {
            QObject::connect(&watch, &QTimer::timeout, &waiting, meanwhile);
            watch.start(50);
        }
        controller.beginScan(paths, {}, {}, forget);
        waiting.exec();
        return completed;
    };
    bool strip_filled = false;
    bool scan_screenshot = false;
    check(run_scan(fixtures, true, [&] {
        if (!controller.scanning()) return;
        (void)window->grabWindow();
        auto* strip = ctx.named("activityStrip");
        auto* fill = ctx.named("activityFill");
        const double share = controller.scanProgress();
        if (strip != nullptr && strip->isVisible() && strip->width() >= window->width() - 1 &&
            strip->height() >= 2 && fill != nullptr && fill->isVisible() && share > 0.0 &&
            std::abs(fill->width() - strip->width() * share) < 2.0 && !strip_filled) {
            strip_filled = true;
            // The evidence: the transport bar as it was drawn mid-scan.
            scan_screenshot = ctx.save_screenshot_to(artifacts.filePath("loading-scan.png"));
        }
    }));
    lay_out();
    check(strip_filled && scan_screenshot && !controller.scanning() && controller.scanProgress() < 0.0 &&
          !ctx.named("activityStrip")->isVisible());
    reached("loading: the plugin scan fills a strip along the transport bar");

    // ---- a scan the cache already knows ------------------------------------
    // Four plugins, described once; the next scan finds them all in the cache.
    const QDir installed = fresh_folder(QStringLiteral("loading-cached"));
    for (const char* copy : {"a.clap", "b.clap", "c.clap", "d.clap"})
        check(QFile::copy(fixture, installed.filePath(QString::fromLatin1(copy))));
    const std::vector<std::filesystem::path> four{installed.path().toStdString()};
    check(run_scan(four, true, {}));
    const auto clap_entries = [&controller] {
        int count = 0;
        for (const auto& entry : controller.plugins())
            if (entry.toMap().value("format").toString() == "CLAP") ++count;
        return count;
    };
    check(clap_entries() == 4);
    int listings = 0;
    const auto counter = QObject::connect(&controller, &AppController::pluginsChanged, &ctx.app,
                                          [&listings] { ++listings; });
    QElapsedTimer cached;
    cached.start();
    check(run_scan(four, false, {}));
    QObject::disconnect(counter);
    // The list is emptied, the cached four arrive together, the scan ends:
    // three listings, not one more for every plugin the cache knew.
    check(clap_entries() == 4 && listings == 3);
    if (listings != 3) std::cerr << "REGRESSION: a cached scan listed the browser " << listings << " times\n";
    details << " | cached listings=" << listings << " in " << cached.elapsed() << " ms";
    reached("loading: a scan the cache already knows reaches the browser in one piece");

    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("loading", run_loading);

}  // namespace
}  // namespace blokkily::verify
