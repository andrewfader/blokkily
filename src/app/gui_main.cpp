#include "pattern_model.hpp"

#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <iostream>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Blokkily");
    QGuiApplication::setOrganizationName("Blokkily");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"verify", "Run the whole-app verification scenario and exit."});
    parser.addOption({"screenshot", "Write verification screenshot to path.", "path"});
    parser.addOption({"clap-fixture", "Scan this CLAP module during verification.", "path"});
    parser.addOption({"vst3-fixture", "Scan this VST3 bundle during verification.", "path"});
    parser.addOption({"soundfont-fixture", "Render this SF2/SF3 during verification.", "path"});
    parser.addOption({"project", "Save and reload the verification project here.", "path"});
    parser.addOption({"view", "Leave the interface in this editor view.", "name"});
    parser.addOption({"export", "Bounce the verification arrangement here.", "path"});
    parser.process(app);

    SongModel song;
    PatternModel pattern(&song);
    Transport transport;
    AppController controller(&song, &pattern, &transport);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("songModel", &song);
    engine.rootContext()->setContextProperty("patternModel", &pattern);
    engine.rootContext()->setContextProperty("appController", &controller);
    engine.rootContext()->setContextProperty("transport", &transport);
    engine.loadFromModule("Blokkily", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    if (parser.isSet("verify")) {
        controller.scanPluginPaths(
            {parser.value("clap-fixture").toStdString()},
            {parser.value("vst3-fixture").toStdString()},
            {parser.value("soundfont-fixture").toStdString()});
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        if (window == nullptr) return 2;
        // Reads the rendered step grid, not the model behind it: a projection
        // that stops following the canonical pattern must fail this gate.
        // Repeater delegates are visual children only, so walk childItems().
        const auto rendered_step = [window](int step) {
            const auto* grid = window->findChild<QQuickItem*>("stepGrid");
            if (grid == nullptr) return false;
            const QString wanted = QString("step%1").arg(step);
            for (const auto* cell : grid->childItems())
                if (cell->objectName() == wanted) return cell->property("active").toBool();
            return false;
        };

        QTimer::singleShot(500, &app, [&] {
            // Names the first scenario that broke, so a failing gate says which
            // behaviour regressed instead of only that something did.
            QString failed;
            bool valid = pattern.rowCount() == 4;
            const auto reached = [&](const char* scenario) {
                if (!valid && failed.isEmpty()) failed = QString::fromLatin1(scenario);
            };
            reached("demonstration pattern");
            // Startup discovery is exercised with real format fixtures but
            // isolated from plugins installed on the test host.
            valid = valid && controller.plugins().size() == 3;
            valid = valid && controller.plugins().at(0).toMap().value("format") == "CLAP";
            valid = valid && controller.plugins().at(1).toMap().value("format") == "VST3";
            valid = valid && controller.plugins().at(2).toMap().value("format") == "SF";
            valid = valid && rendered_step(0) && !rendered_step(1);
            pattern.toggleStep(1, 60);
            valid = valid && pattern.rowCount() == 5 && pattern.hasStep(1);
            valid = valid && rendered_step(1) && rendered_step(4) && !rendered_step(2);
            if (parser.isSet("clap-fixture"))
                valid = valid && controller.verifyClap(parser.value("clap-fixture"));
            if (parser.isSet("vst3-fixture"))
                valid = valid && controller.verifyVst3(parser.value("vst3-fixture"));
            if (parser.isSet("clap-fixture") && parser.isSet("vst3-fixture")) {
                // The browser must present both native formats through one list,
                // each entry tagged with the format that produced it.
                const QVariantList browser = controller.plugins();
                valid = valid && browser.size() == 2;
                for (const QVariant& entry : browser) {
                    const QVariantMap fields = entry.toMap();
                    valid = valid && !fields.value("name").toString().isEmpty()
                                  && !fields.value("vendor").toString().isEmpty();
                }
                valid = valid && browser.first().toMap().value("format") == "CLAP"
                              && browser.last().toMap().value("format") == "VST3";
            }
            if (parser.isSet("clap-fixture"))
                valid = valid && controller.verifyParameterLocks(parser.value("clap-fixture"));
            if (parser.isSet("soundfont-fixture")) {
                valid = valid && controller.verifySoundFont(parser.value("soundfont-fixture"));
            }

            reached("instrument formats");
            if (parser.isSet("project")) {
                // Save the session, disturb the live model, then rebuild it
                // from the file alone: the reload must undo the disturbance.
                const QString file = parser.value("project");
                const int saved_events = pattern.rowCount();
                const std::size_t saved_instruments = controller.instruments().size();
                const int saved_tracks = song.trackCount();
                const int saved_clips = song.clips().size();
                // A mixer move must survive the file too, not just the notes.
                song.setTrackGain(1, -7.5);
                song.toggleMute(1);
                valid = valid && saved_instruments > 0 && controller.saveProject(file);
                pattern.toggleStep(3, 64);
                song.toggleClip(0, 6);
                valid = valid && pattern.rowCount() == saved_events + 1;
                valid = valid && song.clips().size() == saved_clips + 1;
                valid = valid && controller.loadProject(file);
                valid = valid && pattern.rowCount() == saved_events && pattern.hasStep(1)
                              && !pattern.hasStep(3)
                              && song.trackCount() == saved_tracks
                              && song.clips().size() == saved_clips
                              && controller.instruments().size() == saved_instruments;
                const auto restored_strip = song.tracks().at(1).toMap();
                valid = valid && restored_strip.value("mute").toBool()
                              && restored_strip.value("gainText").toString() == "-7.5";
                song.toggleMute(1);
                song.setTrackGain(1, -3.0);
                // The grid must follow the reload too, not just the model.
                valid = valid && rendered_step(1) && !rendered_step(3);
            }

            reached("project persistence");
            // ---- user interface ----------------------------------------
            const auto item = [window](const char* name) {
                return window->findChild<QQuickItem*>(QString::fromLatin1(name));
            };
            // Repeater delegates are visual children only, so findChild cannot
            // see them; walk childItems instead, down through the layouts that
            // a delegate nests its controls in.
            const std::function<QQuickItem*(QQuickItem*, const QString&)> childNamed =
                [&childNamed](QQuickItem* parent, const QString& name) -> QQuickItem* {
                    if (parent == nullptr) return nullptr;
                    for (auto* child : parent->childItems()) {
                        if (child->objectName() == name) return child;
                        if (auto* found = childNamed(child, name)) return found;
                    }
                    return nullptr;
                };

            // The view switcher must actually switch: these were dead controls.
            valid = valid && window->setProperty("view", "TRACKER");
            valid = valid && item("trackerView") != nullptr && item("pianoRollView") != nullptr
                          && item("trackerView")->isVisible()
                          && !item("pianoRollView")->isVisible();
            // Shown is not the same as usable: a panel squeezed to nothing by a
            // neighbour is still "visible", so demand real room as well.
            valid = valid && item("trackerView")->height() > 140
                          && item("trackerView")->width() > 200;
            valid = valid && window->setProperty("view", "PIANO");
            valid = valid && !item("trackerView")->isVisible()
                          && item("pianoRollView")->isVisible();
            valid = valid && window->setProperty("view", "ALL");
            valid = valid && item("trackerView")->isVisible()
                          && item("pianoRollView")->isVisible()
                          && item("trackerView")->height() > 140
                          && item("pianoRollView")->height() > 140
                          && item("pianoRollView")->width() > 200;
            if (parser.isSet("view"))
                valid = valid && window->setProperty("view", parser.value("view"));

            // Selecting a step drives the inspector, and the inspector's edits
            // come back through every projection.
            pattern.selectStep(8);
            valid = valid && pattern.selected().value("hasLock").toBool();
            auto* inspector_note = item("inspectorNote");
            valid = valid && inspector_note != nullptr
                          && inspector_note->property("text").toString() == "D2";

            pattern.transposeSelected(12);
            valid = valid && pattern.selected().value("noteName").toString() == "D3"
                          && inspector_note->property("text").toString() == "D3";
            pattern.transposeSelected(-12);

            // Persistence must be reachable in the normal interface, not only
            // through this verification command's private code path.
            valid = valid && item("openProjectButton") != nullptr
                          && item("saveProjectButton") != nullptr;

            pattern.setSelectedVelocity(0.5);
            auto* tracker_row = childNamed(item("trackerRows"), "trackerRow8");
            valid = valid && tracker_row != nullptr;
            valid = valid && pattern.steps().at(8).toMap().value("velocityUnits").toInt() == 64;
            pattern.setSelectedVelocity(0.9);
            valid = valid && pattern.steps().at(8).toMap().value("velocityUnits").toInt() == 114;

            // A step with no note offers placement rather than dead controls.
            pattern.selectStep(5);
            valid = valid && !pattern.selected().value("exists").toBool();
            pattern.selectStep(8);

            // The playhead every editor shares.
            transport.locate(6.0);
            valid = valid && transport.step() == 6;
            // Bar.beat.sixteenth over the whole arrangement: step 6 is the
            // third sixteenth of the second beat of bar one.
            valid = valid && transport.position() == "1.2.3";
            valid = valid && transport.bar() == 0;
            transport.locate(16.0);  // the top of the next bar
            valid = valid && transport.step() == 0 && transport.bar() == 1;
            valid = valid && transport.position() == "2.1.1";
            // The playhead runs the length of the song, not one pattern.
            valid = valid && transport.bars() == song.bars() && transport.bars() >= 8;
            transport.locate(transport.bars() * 16.0);   // wraps at the end of the song
            valid = valid && transport.bar() == 0 && transport.step() == 0;
            // The playhead is the engine's position, not a clock beside it:
            // two seconds of audio at 120 BPM is exactly one bar.
            transport.followSamples(96000, 48000.0);
            valid = valid && transport.bar() == 1 && transport.position() == "2.1.1";
            transport.followSamples(0, 48000.0);
            valid = valid && transport.bar() == 0 && transport.position() == "1.1.1";
            transport.releaseFollowing();
            transport.locate(6.0);

            reached("user interface");
            // ---- arrangement --------------------------------------------
            // The timeline must be a real editor over the song, not a picture
            // of one: cells report the clips the song actually holds, and
            // clicking one changes the song.
            const auto arrangeCell = [&](int track, int bar) -> QQuickItem* {
                auto* rows = item("arrangementRows");
                auto* row = childNamed(rows, QString("arrangeRow%1").arg(track));
                return childNamed(row, QString("clip%1-%2").arg(track).arg(bar));
            };
            const auto cellFilled = [&](int track, int bar) {
                auto* cell = arrangeCell(track, bar);
                return cell != nullptr && cell->property("filled").toBool();
            };
            valid = valid && item("arrangement") != nullptr;
            valid = valid && song.trackCount() == 3;   // CLAP, VST3, and SoundFont
            valid = valid && cellFilled(0, 0) && cellFilled(0, 3) && !cellFilled(0, 5);
            valid = valid && !cellFilled(1, 0) && cellFilled(1, 2);

            const int clips_before = song.clips().size();
            song.toggleClip(1, 0);
            valid = valid && song.clips().size() == clips_before + 1 && cellFilled(1, 0);
            song.toggleClip(1, 0);
            valid = valid && song.clips().size() == clips_before && !cellFilled(1, 0);

            // Switching the pattern the arrangement has open moves every editor.
            valid = valid && song.currentPattern() == 0;
            const int verse_events = pattern.rowCount();
            song.selectPattern(1);
            valid = valid && pattern.rowCount() == 3 && pattern.rowCount() != verse_events;
            valid = valid && item("patternTitle") != nullptr
                          && item("patternTitle")->property("text").toString() == "CHORUS";
            song.selectPattern(0);
            valid = valid && pattern.rowCount() == verse_events
                          && item("patternTitle")->property("text").toString() == "VERSE";

            reached("arrangement");
            // ---- mixer ---------------------------------------------------
            valid = valid && item("mixerPanel") != nullptr && item("masterStrip") != nullptr;
            // One strip per track, each with its own mute and solo.
            auto* strips = item("mixerStrips");
            for (int track = 0; track < song.trackCount(); ++track) {
                auto* strip = childNamed(strips, QString("mixerStrip%1").arg(track));
                valid = valid && strip != nullptr
                              && childNamed(strip, QString("mute%1").arg(track)) != nullptr
                              && childNamed(strip, QString("solo%1").arg(track)) != nullptr
                              && childNamed(strip, QString("meter%1").arg(track)) != nullptr;
            }

            song.toggleMute(1);
            valid = valid && song.tracks().at(1).toMap().value("mute").toBool()
                          && !song.tracks().at(1).toMap().value("audible").toBool();
            song.toggleMute(1);
            song.toggleSolo(0);
            valid = valid && song.tracks().at(0).toMap().value("audible").toBool()
                          && !song.tracks().at(1).toMap().value("audible").toBool();
            song.toggleSolo(0);
            song.setTrackGain(1, -6.0);
            valid = valid && song.tracks().at(1).toMap().value("gainText").toString() == "-6.0";
            song.setTrackPan(1, -1.0);
            valid = valid && song.tracks().at(1).toMap().value("panText").toString() == "L100";
            song.setTrackPan(1, 0.25);
            // Mute and solo must move audio, not just the strip's colours.
            valid = valid && controller.verifyMixer();

            reached("mixer");
            // ---- export --------------------------------------------------
            valid = valid && item("exportButton") != nullptr;
            if (parser.isSet("export")) {
                valid = valid && controller.verifyBounce(parser.value("export"));
                valid = valid && !controller.exportStatus().contains("failed");
            }

            reached("export");
            const QString screenshot = parser.value("screenshot");
            if (!screenshot.isEmpty()) {
                QDir{}.mkpath(QFileInfo(screenshot).absolutePath());
                const QImage image = window->grabWindow();
                valid = valid && !image.isNull() && image.width() == 1280 && image.height() == 800;
                if (!image.isNull()) {
                    const auto first = image.pixelColor(0, 0);
                    bool varied = false;
                    for (int y = 0; y < image.height() && !varied; y += 20)
                        for (int x = 0; x < image.width(); x += 20)
                            if (image.pixelColor(x, y) != first) { varied = true; break; }
                    valid = valid && varied && image.save(screenshot, "PNG");
                }
            }
            reached("screenshot");
            std::cout << (valid ? "BDD PASS" : "BDD FAIL")
                      << (failed.isEmpty() ? "" : " at: " + failed.toStdString())
                      << " | shared-model edit=" << pattern.rowCount()
                      << " | plugins=" << controller.plugins().size()
                      << " | tracks=" << song.trackCount()
                      << " | instruments=" << controller.instruments().size()
                      << " | clips=" << song.clips().size()
                      << " | " << controller.exportStatus().toStdString()
                      << " | " << controller.status().toStdString() << '\n';
            app.exit(valid ? 0 : 3);
        });
    } else {
        // Let the first frame appear before potentially large plugin trees are
        // scanned. Discovery still begins automatically on every normal launch.
        QTimer::singleShot(0, &controller, &AppController::scanPlugins);
    }
    return app.exec();
}
