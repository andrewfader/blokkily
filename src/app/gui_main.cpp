#include "keyboard_model.hpp"
#include "pattern_model.hpp"

#include "blokkily/sequencer/scheduler.hpp"

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
#include <QElapsedTimer>
#include <QEventLoop>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <filesystem>

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
    parser.addOption({"scan-fixture", "Scan this directory out of process during verification.",
                      "path"});
    parser.addOption({"tuning", "Leave the session in this tuning.", "name"});
    parser.addOption({"scale", "Leave the session in this scale.", "name"});
    parser.addOption({"surface", "Leave the keyboard on this playable surface.", "name"});
    parser.process(app);

    SongModel song;
    PatternModel pattern(&song);
    Transport transport;
    AppController controller(&song, &pattern, &transport);
    KeyboardModel keyboard(&song, &pattern, &controller);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("songModel", &song);
    engine.rootContext()->setContextProperty("patternModel", &pattern);
    engine.rootContext()->setContextProperty("appController", &controller);
    engine.rootContext()->setContextProperty("transport", &transport);
    engine.rootContext()->setContextProperty("keyboardModel", &keyboard);
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
        // Repeater delegates hang off the item that laid them out rather than
        // off the window, so a named item is looked for down the visual tree.
        const std::function<QQuickItem*(QQuickItem*, const QString&)> find_item =
            [&find_item](QQuickItem* from, const QString& name) -> QQuickItem* {
            if (from == nullptr) return nullptr;
            if (from->objectName() == name) return from;
            for (auto* child : from->childItems())
                if (auto* found = find_item(child, name)) return found;
            return nullptr;
        };
        const auto named = [window, &find_item](const QString& name) {
            return find_item(window->contentItem(), name);
        };
        // Presses the rendered editor rather than calling the model behind it:
        // an editor that stops turning a click into an edit must fail this gate.
        const auto click_at = [window](QQuickItem* item, QPointF local,
                                       Qt::MouseButton button) {
            if (item == nullptr) return;
            const QPointF scene = item->mapToScene(local);
            const QPointF global = window->mapToGlobal(scene);
            QMouseEvent press(QEvent::MouseButtonPress, scene, scene, global, button, button,
                              Qt::NoModifier);
            QCoreApplication::sendEvent(window, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, scene, scene, global, button,
                                Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &release);
        };
        // Types one of the tracker's note keys at the window, the way a
        // producer writing a phrase into the tracker does.
        const auto type_key = [window](Qt::Key key, const QString& text) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(window, &press);
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(window, &release);
        };
        // The note name the tracker prints on a row, read off the rendered row
        // rather than recomputed from the model.
        const auto tracker_note = [&named, &find_item](int row) {
            auto* cell = named(QString("trackerNote%1").arg(row));
            if (cell == nullptr) return QString();
            for (const auto* child : cell->childItems())
                if (child->property("text").isValid()) return child->property("text").toString();
            return QString();
        };
        const auto roll_draws = [&named](int step) {
            return named(QString("rollNote%1").arg(step)) != nullptr;
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

            // ------------------------------------------------------------------
            // Scenario: the piano roll is an input, not a picture of one.
            // The lane and the column that were pressed decide the pitch and
            // the step, and every other projection redraws from that one edit.
            reached("shared-model edit");
            {
                auto* roll = named("rollInput");
                valid = valid && roll != nullptr && roll->width() > 0 && roll->height() > 0;
                if (valid) {
                    const int high = pattern.highKey();
                    const int lanes = std::max(1, high - pattern.lowKey() + 1);
                    const double lane_height = roll->height() / lanes;
                    const double lane_width = roll->width() / PatternModel::step_count;
                    const int wanted_key = high - 3;
                    const QPointF spot((3 + 0.5) * lane_width,
                                       (high - wanted_key + 0.5) * lane_height);
                    click_at(roll, spot, Qt::LeftButton);
                    valid = valid && pattern.hasStep(3);
                    valid = valid && pattern.steps().at(3).toMap().value("key").toInt()
                                         == wanted_key;
                    // The grid, the tracker and the roll all followed the click.
                    valid = valid && rendered_step(3);
                    valid = valid && tracker_note(3) ==
                                         song.pitchName(wanted_key, 0.0);
                    valid = valid && roll_draws(3);
                    reached("piano roll writes a note");

                    // The right button erases, and the same three projections
                    // agree that the step is now empty.
                    click_at(roll, spot, Qt::RightButton);
                    valid = valid && !pattern.hasStep(3) && !rendered_step(3)
                                  && !roll_draws(3) && tracker_note(3) == "---";
                    reached("piano roll erases a note");
                }
            }

            // ------------------------------------------------------------------
            // Scenario: the tracker is an input too. Its note column takes a
            // click, and its note keys type a phrase into the same pattern.
            {
                auto* cell = named("trackerNote5");
                valid = valid && cell != nullptr;
                if (valid) {
                    click_at(cell, QPointF(cell->width() / 2, cell->height() / 2),
                             Qt::LeftButton);
                    // The entry octave the tracker shows is the octave it writes
                    // in: octave 3 means C3, the key numbered 48.
                    valid = valid && pattern.hasStep(5);
                    valid = valid && pattern.steps().at(5).toMap().value("key").toInt() == 48;
                    valid = valid && pattern.selectedStep() == 5;
                    valid = valid && rendered_step(5) && roll_draws(5);
                    reached("tracker note column");

                    // "X" is D in a tracker's note layout, and typing it moves
                    // the cursor on, which is what makes a tracker fast.
                    type_key(Qt::Key_X, "x");
                    valid = valid && pattern.steps().at(5).toMap().value("key").toInt() == 50;
                    valid = valid && pattern.selectedStep() == 6;
                    valid = valid && tracker_note(5) == song.pitchName(50, 0.0);
                    valid = valid && roll_draws(5);
                    reached("tracker note keys");

                    // Backspace empties the row instead of toggling a note back
                    // onto it.
                    pattern.selectStep(5);
                    type_key(Qt::Key_Backspace, "\b");
                    valid = valid && !pattern.hasStep(5) && !rendered_step(5);
                    reached("tracker clears a row");
                }
            }

            // ------------------------------------------------------------------
            // Scenario: the session opens with something to hear. Every track
            // without an instrument is given a General MIDI bank, and a drum
            // track is given the percussion bank rather than a grand piano.
            if (parser.isSet("soundfont-fixture")) {
                SongModel fresh_song;
                PatternModel fresh_pattern(&fresh_song);
                Transport fresh_transport;
                AppController fresh(&fresh_song, &fresh_pattern, &fresh_transport);
                const std::filesystem::path fixture =
                    parser.value("soundfont-fixture").toStdString();
                (void)fresh.loadDefaultInstrument({fixture.parent_path()});
                const auto& tracks = fresh_song.song().tracks;
                valid = valid && !tracks.empty();
                for (const auto& track : tracks) {
                    valid = valid && track.instrument.format == "SoundFont";
                    valid = valid && !track.instrument.path.empty();
                    valid = valid && !track.instrument.state.empty();
                }
                if (valid) {
                    // The bank and program travel as the instrument's own state,
                    // the same road a saved project takes.
                    const auto& drums = tracks.front().instrument.state;
                    const std::string state(reinterpret_cast<const char*>(drums.data()),
                                            drums.size());
                    valid = valid && state.find("\n128\n0") != std::string::npos;
                }
                // A session that already has an instrument is left alone.
                valid = valid && !fresh.loadDefaultInstrument({fixture.parent_path()});
                // And the point of all of it: the opening song makes a sound.
                // A label naming a bank is not evidence that anything is
                // audible, so the engine the speakers would hear is rendered.
                if (auto* fresh_engine = fresh.engine()) {
                    fresh_engine->set_playing(true);
                    std::vector<float> left(512, 0.0F), right(512, 0.0F);
                    float loudest = 0.0F;
                    for (int block = 0; block < 64 && loudest == 0.0F; ++block) {
                        std::fill(left.begin(), left.end(), 0.0F);
                        std::fill(right.begin(), right.end(), 0.0F);
                        fresh_engine->process({left, right});
                        for (const float sample : left)
                            loudest = std::max(loudest, std::abs(sample));
                    }
                    valid = valid && loudest > 0.0F;
                } else {
                    valid = false;
                }
                reached("default instrument");
            }

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

            // ------------------------------------------------------------------
            // Scenario: an instrument is found by typing a few letters of it.
            // The browser is searched through the rendered field, and the list
            // that answers is the rendered list: a filter that stops narrowing
            // it, or a filtered row that stops pointing at the instrument it
            // names, fails here.
            if (parser.isSet("clap-fixture") && parser.isSet("vst3-fixture")) {
                auto* filter = named("pluginFilter");
                auto* browser = named("pluginBrowser");
                const auto listed = [browser] { return browser->property("count").toInt(); };
                const auto type = [&type_key](const QString& text) {
                    for (const QChar character : text)
                        type_key(static_cast<Qt::Key>(character.toUpper().unicode()),
                                 QString(character));
                };
                valid = valid && filter != nullptr && browser != nullptr
                              && named("pluginScrollBar") != nullptr;
                if (valid) {
                    filter->forceActiveFocus();
                    valid = valid && listed() == 2;
                    // Letters of a name reach it wherever they sit, and leave
                    // the instruments they cannot reach out of the list.
                    type("vst");
                    valid = valid && filter->property("text").toString() == "vst"
                                  && listed() == 1;
                    // The row of a filtered browser still knows which
                    // instrument it is: loading row zero here must load the
                    // VST3, which is the *second* entry of the full list.
                    const auto rows = controller.browserPlugins();
                    valid = valid && rows.size() == 1
                                  && rows.first().toMap().value("source").toInt() == 1
                                  && rows.first().toMap().value("format") == "VST3";
                    // Clearing the field lists the whole installation again.
                    type_key(Qt::Key_Escape, QString(QChar(27)));
                    valid = valid && filter->property("text").toString().isEmpty()
                                  && listed() == 2;
                    // The arrow keys walk the list without leaving the field.
                    valid = valid && browser->property("currentIndex").toInt() == 0;
                    type_key(Qt::Key_Down, QString());
                    valid = valid && browser->property("currentIndex").toInt() == 1;
                    type_key(Qt::Key_Up, QString());
                    valid = valid && browser->property("currentIndex").toInt() == 0;
                    // Hands the keyboard back, so note entry is typed at the
                    // tracker and not into the browser's field.
                    if (auto* entry = named("noteEntry")) entry->forceActiveFocus();
                }
            }
            reached("instrument browser");
            // Regression: view notifications used to rebuild the graph, so even
            // moving the cursor or a fader reset the song to sample zero.
            const auto uninterrupted = [&] {
                auto* running = controller.engine();
                if (running == nullptr) return false;
                running->rewind();
                running->set_playing(true);
                std::vector<float> left(256), right(256);
                running->process({left, right});
                const auto position = running->sample_position();
                pattern.selectStep(4);
                song.selectTrack(1);
                song.selectPattern(1);
                song.setTrackGain(0, -6.0);
                const bool retained = controller.engine() == running
                    && controller.engine()->sample_position() == position
                    && controller.engine()->is_playing();
                controller.engine()->set_playing(false);
                song.setTrackGain(0, 0.0);
                song.selectTrack(0);
                song.selectPattern(0);
                return retained;
            };
            valid = uninterrupted() && valid;
            reached("selection and mixer preserve running playback");
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
            // ---- keyboards -----------------------------------------------
            // The playable surface is a projection of the song's tuning and
            // scale, and playing it is an edit of the canonical pattern like
            // any other. Everything below drives the real objects the window
            // is bound to, then reads the rendered surface back.
            {
                auto* keyboard_panel = item("keyboardPanel");
                auto* surface_item = item("keyboardSurface");
                valid = valid && keyboard_panel != nullptr && surface_item != nullptr;
                // The surface must be usable in the layout that shows every
                // editor at once and in its own view. Present is not enough: a
                // panel squeezed to nothing by its neighbours still reports
                // itself visible.
                for (const char* shown : {"ALL", "KEYS"}) {
                    valid = valid && window->setProperty("view", shown);
                    valid = valid && keyboard_panel != nullptr
                                  && keyboard_panel->isVisible()
                                  && keyboard_panel->height() > 120
                                  && keyboard_panel->width() > 400;
                    valid = valid && surface_item != nullptr && surface_item->height() > 40;
                }
                valid = valid && window->setProperty("view", "ALL");

                song.selectTrack(0);   // the CLAP synth, so a press is audible
                song.setTuning("12-EDO");
                song.setScale("Chromatic");
                song.setRootDegree(60);
                keyboard.setSurface("PIANO");
                keyboard.setRegister("Full");

                // One key per degree of the tuning, named the way the tuning
                // names it, and actually drawn.
                valid = valid && keyboard.columns() == 49;
                valid = valid && keyboard.cells().size() == 49;
                valid = valid && keyboard.cells().first().toMap().value("label") == "C2";
                auto* first_key = childNamed(surface_item, "keyboardCell0");
                valid = valid && first_key != nullptr && first_key->width() > 2
                              && first_key->height() > 20;

                // A tuning with more degrees to the octave is more keys over
                // the same span, and its keys carry the retune the instrument
                // will be told.
                song.setTuning("19-EDO");
                valid = valid && keyboard.cells().size() > 49;
                int retuned = 0;
                for (const QVariant& entry : keyboard.cells())
                    if (!entry.toMap().value("retune").toString().isEmpty()) ++retuned;
                valid = valid && retuned > 0;
                valid = valid && childNamed(surface_item, "keyboardCell0") != nullptr;

                // The scale marks the keyboard; it does not shorten it.
                song.setTuning("12-EDO");
                song.setScale("Major");
                song.setRootDegree(60);
                const auto degree_cell = [&](int degree) {
                    for (const QVariant& entry : keyboard.cells())
                        if (entry.toMap().value("degree").toInt() == degree) return entry.toMap();
                    return QVariantMap{};
                };
                valid = valid && keyboard.cells().size() == 49;
                valid = valid && degree_cell(60).value("root").toBool()
                              && degree_cell(60).value("inScale").toBool();
                valid = valid && degree_cell(72).value("root").toBool();
                valid = valid && !degree_cell(61).value("inScale").toBool();
                valid = valid && degree_cell(62).value("inScale").toBool();

                // Playing a key writes it onto the selected step and every
                // projection names it.
                pattern.selectStep(6);
                const int before = pattern.rowCount();
                valid = valid && keyboard.pressDegree(64);
                valid = valid && pattern.rowCount() == before + 1;
                valid = valid && pattern.selected().value("key").toInt() == 64;
                valid = valid && pattern.selected().value("noteName").toString() == "E4";
                valid = valid && pattern.steps().at(6).toMap().value("noteName") == "E4";
                valid = valid && rendered_step(6);

                // Auto-scale plays the note the song is in; without it the key
                // sounds the pitch it is drawn at.
                valid = valid && !song.autoScale();
                valid = valid && keyboard.pressDegree(61);
                valid = valid && pattern.selected().value("key").toInt() == 61;
                song.toggleAutoScale();
                valid = valid && song.autoScale();
                valid = valid && keyboard.pressDegree(61);
                valid = valid && pattern.selected().value("key").toInt() == 62;
                song.toggleAutoScale();

                // A press is heard through the selected track's instrument even
                // though the transport is stopped: measured from the audio the
                // engine renders, not from the fact that a call returned.
                const auto audible_press = [&](int degree) {
                    std::vector<float> left(256, 0.0F), right(256, 0.0F);
                    if (controller.engine() == nullptr) return false;
                    controller.engine()->set_playing(false);
                    controller.engine()->process({left, right});  // drain the tail
                    if (!keyboard.pressDegree(degree)) return false;
                    // Writing the step recompiles the arrangement, so the
                    // engine to listen to is the one the press was sent to and
                    // not whichever one existed beforehand.
                    auto* running = controller.engine();
                    if (running == nullptr) return false;
                    running->set_playing(false);
                    float peak = 0.0F;
                    for (int block = 0; block < 8; ++block) {
                        std::fill(left.begin(), left.end(), 0.0F);
                        std::fill(right.begin(), right.end(), 0.0F);
                        running->process({left, right});
                        for (const float sample : left) peak = std::max(peak, std::abs(sample));
                    }
                    return peak > 0.001F;
                };
                valid = valid && audible_press(67);

                // A microtonal degree keeps its pitch: the pattern stores the
                // nearer key and the retune away from it, and both survive the
                // session being written and read back.
                song.setTuning("24-EDO quarter tones");
                pattern.selectStep(7);
                valid = valid && keyboard.pressDegree(61);
                valid = valid && std::abs(pattern.selected().value("cents").toDouble()) > 20.0;
                const double played_cents = pattern.selected().value("cents").toDouble();
                if (parser.isSet("project")) {
                    const QString tuned_file = parser.value("project") + ".tuned";
                    valid = valid && controller.saveProject(tuned_file);
                    song.setTuning("12-EDO");
                    valid = valid && controller.loadProject(tuned_file);
                    valid = valid && song.tuningName() == "24-EDO quarter tones";
                    pattern.selectStep(7);
                    valid = valid && std::abs(pattern.selected().value("cents").toDouble()
                                              - played_cents) < 1e-6;
                }

                // Changing surface changes the layout and nothing else.
                song.setTuning("12-EDO");
                song.setScale("Major");
                song.setRootDegree(60);
                const int events_before_surfaces = pattern.rowCount();
                for (const QString& surface : {QStringLiteral("GRID"), QStringLiteral("FRETS"),
                                               QStringLiteral("CHORDS")}) {
                    keyboard.setSurface(surface);
                    valid = valid && keyboard.surface() == surface;
                    valid = valid && !keyboard.cells().isEmpty();
                    valid = valid && keyboard.rows() > 1;
                    valid = valid && childNamed(item("keyboardSurface"), "keyboardCell0") != nullptr;
                }
                valid = valid && pattern.rowCount() == events_before_surfaces;

                // The tuning and scale controls are real controls: the list
                // behind one is populated from the model, and choosing an entry
                // reaches the session rather than only the chip's own label.
                auto* tuning_picker = item("tuningPicker");
                auto* tuning_menu = window->findChild<QObject*>("tuningPickerMenu");
                valid = valid && tuning_picker != nullptr && tuning_menu != nullptr;
                valid = valid && tuning_menu != nullptr
                              && tuning_menu->property("count").toInt()
                                     == song.tuningNames().size();
                if (tuning_picker != nullptr) {
                    QMetaObject::invokeMethod(tuning_picker, "picked",
                                              Q_ARG(QString, QStringLiteral("31-EDO")));
                    valid = valid && song.tuningName() == "31-EDO";
                    valid = valid && tuning_picker->property("value").toString() == "31-EDO";
                }
                auto* scale_picker = item("scalePicker");
                auto* scale_menu = window->findChild<QObject*>("scalePickerMenu");
                valid = valid && scale_picker != nullptr && scale_menu != nullptr
                              && scale_menu->property("count").toInt()
                                     == song.scaleNames().size();
                if (scale_picker != nullptr) {
                    QMetaObject::invokeMethod(scale_picker, "picked",
                                              Q_ARG(QString, QStringLiteral("Phrygian")));
                    valid = valid && song.scaleName() == "Phrygian";
                }
                song.setTuning("12-EDO");
                song.setScale("Major");
                song.setRootDegree(60);

                // Regression: a note was released on whichever track happened to
                // be selected when the next key was pressed, so changing track
                // mid-press left a voice sounding on the instrument just left.
                const auto track_peak = [&](std::size_t track) {
                    auto* running = controller.engine();
                    if (running == nullptr) return 0.0F;
                    running->set_playing(false);
                    std::vector<float> left(256, 0.0F), right(256, 0.0F);
                    running->process({left, right});
                    return running->track_peak(track);
                };
                keyboard.toggleRecording();          // audition only: no edits,
                valid = valid && !keyboard.recording();  // so no engine rebuild
                song.selectTrack(0);
                valid = valid && keyboard.pressDegree(60);
                valid = valid && track_peak(0) > 0.001F;
                song.selectTrack(1);
                valid = valid && keyboard.pressDegree(67);
                valid = valid && track_peak(0) < 0.001F;   // the left track let go
                valid = valid && track_peak(1) > 0.001F;
                keyboard.toggleRecording();
                valid = valid && keyboard.recording();
                song.selectTrack(0);

                // Chord pads: named by numeral, one inversion per row, and a
                // press writes one chord event rather than a pile of notes.
                keyboard.setSurface("CHORDS");
                valid = valid && keyboard.cells().first().toMap().value("label") == "I";
                valid = valid && keyboard.cells().at(4).toMap().value("label") == "V";
                const int steps_across = keyboard.columns();
                valid = valid && steps_across == 7;
                valid = valid && keyboard.cells().at(steps_across).toMap().value("degree").toInt()
                                     != keyboard.cells().first().toMap().value("degree").toInt();
                pattern.selectStep(11);
                valid = valid && keyboard.press(0);
                valid = valid && pattern.selected().value("voices").toInt() == 3;
                valid = valid && pattern.selected().value("key").toInt() == 60;
                valid = valid && rendered_step(11);
                // One event, three voices: the scheduler that feeds the engine
                // expands the pad's step into the notes it named.
                const auto sounded = blokkily::Scheduler{}.render_loop(song.song().pattern(0), 1, 0);
                std::vector<int> chord_keys;
                for (const auto& note : sounded)
                    if (note.start == 11 * PatternModel::ticks_per_step)
                        chord_keys.push_back(note.key);
                valid = valid && (chord_keys == std::vector<int>{60, 64, 67});

                keyboard.setSurface("PIANO");
                // The run can be left in a named state so a screenshot shows
                // one surface, in one tuning, on its own.
                if (parser.isSet("tuning")) {
                    song.setTuning(parser.value("tuning"));
                    valid = valid && song.tuningName() == parser.value("tuning");
                }
                if (parser.isSet("scale")) {
                    song.setScale(parser.value("scale"));
                    valid = valid && song.scaleName() == parser.value("scale");
                }
                if (parser.isSet("surface")) {
                    keyboard.setSurface(parser.value("surface"));
                    valid = valid && keyboard.surface() == parser.value("surface")
                                  && !keyboard.cells().isEmpty();
                }
                // The requested view is restored last, because measuring the
                // keyboard needed the layouts that show it.
                if (parser.isSet("view"))
                    valid = valid && window->setProperty("view", parser.value("view"));
            }

            reached("keyboards");
            // ---- plugin discovery ---------------------------------------
            if (parser.isSet("scan-fixture")) {
                // The fixture directory holds a working plugin beside one whose
                // entry point never returns. Scanning used to run in this
                // process, so the second one froze the application before it
                // could show a window at all.
                const std::vector<std::filesystem::path> fixtures{
                    parser.value("scan-fixture").toStdString()};
                const auto run_scan = [&](bool forget_cache) {
                    QEventLoop waiting;
                    bool completed = false;
                    QObject::connect(&controller, &AppController::scanFinished, &waiting,
                                     [&] { completed = true; waiting.quit(); });
                    // Bounded, so a scan that never ends fails the gate instead
                    // of hanging it.
                    QTimer guard;
                    guard.setSingleShot(true);
                    QObject::connect(&guard, &QTimer::timeout, &waiting, &QEventLoop::quit);
                    guard.start(20000);
                    controller.beginScan(fixtures, {}, {}, forget_cache);
                    waiting.exec();
                    return completed;
                };

                // An ordinary edit lands while the scan is still working: the
                // thread that draws the interface is not the one waiting on
                // plugin code.
                bool edited_during_scan = false;
                QTimer::singleShot(200, &app, [&] {
                    if (!controller.scanning()) return;
                    pattern.toggleStep(2, 62);
                    edited_during_scan = pattern.hasStep(2) && rendered_step(2);
                    pattern.toggleStep(2, 62);
                });
                valid = valid && run_scan(true);
                valid = valid && edited_during_scan;
                // The working plugin is listed; the one that hangs is reported
                // as a failure rather than waited for.
                valid = valid && controller.plugins().size() == 1
                              && controller.plugins().first().toMap().value("format") == "CLAP"
                              && controller.plugins().first().toMap().value("name")
                                     == "Blokkily Test Synth";
                valid = valid && controller.status().contains("1 failure");

                // The next launch skips what already failed, so a hanging
                // plugin costs its deadline once rather than on every start.
                QElapsedTimer repeat;
                repeat.start();
                valid = valid && run_scan(false);
                valid = valid && controller.plugins().size() == 1
                              && controller.status().contains("1 failure");
                valid = valid && repeat.elapsed() < 1000;
            }

            reached("plugin discovery");
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
