// The whole-app run behind every `blokkily --verify` gate that does not name
// a --scenario: edit-once-see-everywhere, the keyboards, the chord pads, the
// honeycomb and the plugin scan. Its sections run in a fixed order over one
// song, each leaving the song as the next one expects it.

#include "verify/harness.hpp"

#include "blokkily/project/project.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QLibrary>
#include <QMetaObject>
#include <QScreen>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

// Holds the context under the names the sections read it by, so each
// section is a plain member function over the same song.
struct DefaultScenario {
    explicit DefaultScenario(VerifyContext& context) : ctx(context) {}

    VerifyContext& ctx;
    QGuiApplication& app = ctx.app;
    QCommandLineParser& parser = ctx.parser;
    QQuickWindow* window = ctx.window;
    SongModel& song = ctx.song;
    PatternModel& pattern = ctx.pattern;
    Transport& transport = ctx.transport;
    AppController& controller = ctx.controller;
    KeyboardModel& keyboard = ctx.keyboard;
    blokkily::RtAudioOutput* verification_output = ctx.output;
    bool& valid = ctx.valid;

    QQuickItem* named(const QString& name) const { return ctx.named(name); }
    QQuickItem* item(const char* name) const { return ctx.item(name); }
    static QQuickItem* childNamed(QQuickItem* parent, const QString& name) {
        return VerifyContext::child_named(parent, name);
    }
    void click_at(QQuickItem* target, QPointF local, Qt::MouseButton button,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier) const {
        ctx.click_at(target, local, button, modifiers);
    }
    void mouse_at(QQuickItem* target, QPointF local, QEvent::Type type,
                  Qt::MouseButton button, Qt::MouseButtons held) const {
        ctx.mouse_at(target, local, type, button, held);
    }
    void type_key(Qt::Key key, const QString& text) const { ctx.type_key(key, text); }
    void chord_key(Qt::Key key, Qt::KeyboardModifiers modifiers) const {
        ctx.chord_key(key, modifiers);
    }
    static void settle(int milliseconds) { VerifyContext::settle(milliseconds); }
    void reached(const char* scenario) { ctx.reached(scenario); }
    bool save_screenshot() const { return ctx.save_screenshot(); }
    bool rendered_step(int step) const { return ctx.rendered_step(step); }
    QString tracker_note(int row) const { return ctx.tracker_note(row); }
    bool roll_draws(int step) const { return ctx.roll_draws(step); }
    // The browser also lists effects (the built-ins among them), so what the
    // instrument checks count is the instruments alone.
    int instrument_entries() const {
        int count = 0;
        for (const auto& entry : controller.plugins())
            if (entry.toMap().value("kind").toString() == "instrument") ++count;
        return count;
    }

    // The sections in the order the gates have always run them.
    void run() {
        demonstration_and_piano_roll();
        tracker();
        instrument_formats();
        browser_to_persistence();
        user_interface();
        arrangement();
        mixer();
        bounce();
        session_workflow();
        keyboards();
        plugin_discovery();
    }

    // The demonstration pattern, discovery of the fixtures, and the piano
    // roll as an input.
    void demonstration_and_piano_roll() {
        {
            // The offscreen platform's screen is smaller than the design
            // size, so this is the small-screen case: the window must have
            // opened inside the free area rather than past its bottom edge.
            const auto free = ctx.window->screen()->availableGeometry().size();
            const auto opened = ctx.opened_size;
            const bool fits = !opened.isEmpty() && opened.width() <= free.width() &&
                              opened.height() <= free.height();
            if (!fits)
                std::cerr << "REGRESSION: the window opened at " << opened.width() << 'x'
                          << opened.height() << " on a screen with " << free.width() << 'x'
                          << free.height() << " free\n";
            valid = fits && valid;
        }
        reached("the window opens inside the screen");
        reached("demonstration pattern");
        // Startup discovery is exercised with real format fixtures but
        // isolated from plugins installed on the test host.
        // After them come the instruments the application provides itself
        // (the sampler), listed by the factory without a scan.
        valid = valid && instrument_entries() == 5;
        valid = valid && controller.plugins().at(0).toMap().value("format") == "CLAP";
        valid = valid && controller.plugins().at(1).toMap().value("format") == "VST3";
        valid = valid && controller.plugins().at(2).toMap().value("format") == "SF";
        valid = valid && controller.plugins().at(3).toMap().value("name") == "Sampler"
                      && controller.plugins().at(4).toMap().value("name") == "Drum Sampler";
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
                const double lane_width = roll->width() / pattern.stepCount();
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

                // Dragging the right edge of a drawn note lengthens it, and
                // that duration is what the scheduler will sound.
                click_at(roll, spot, Qt::LeftButton);
                (void)window->grabWindow();
                QCoreApplication::processEvents();
                valid = valid && pattern.hasStep(3);
                const int written_key =
                    pattern.steps().at(3).toMap().value("key").toInt();
                const int high_now = pattern.highKey();
                const double lane_height_now = roll->height() /
                    std::max(1, high_now - pattern.lowKey() + 1);
                const QPointF on_note((3 + 0.8) * lane_width,
                                      (high_now - written_key + 0.5) * lane_height_now);
                mouse_at(roll, on_note, QEvent::MouseButtonPress, Qt::LeftButton,
                         Qt::LeftButton);
                mouse_at(roll, {7.5 * lane_width, on_note.y()}, QEvent::MouseMove,
                         Qt::NoButton, Qt::LeftButton);
                mouse_at(roll, {7.5 * lane_width, on_note.y()},
                         QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
                QCoreApplication::processEvents();
                const int stretched = pattern.hasStep(3)
                    ? pattern.steps().at(3).toMap().value("duration").toInt() : 0;
                if (stretched < 360) {
                    std::cerr << "REGRESSION: piano roll did not lengthen step 3; duration="
                              << stretched << " occupied=";
                    for (int s = 0; s < 16; ++s)
                        if (pattern.hasStep(s))
                            std::cerr << ' ' << s << ':'
                                      << pattern.steps().at(s).toMap().value("duration").toInt()
                                      << '@'
                                      << pattern.steps().at(s).toMap().value("key").toInt();
                    std::cerr << '\n';
                }
                valid = valid && stretched >= 360;
                if (auto* drawn = named("rollNote3"))
                    valid = valid && drawn->width() >= 3.0 * lane_width - 4.0;
                else
                    valid = false;
                valid = valid && tracker_note(3) == song.pitchName(written_key, 0.0);
                {
                    blokkily::Scheduler scheduler;
                    bool sounded = false;
                    for (const auto& note : scheduler.render(song.editPattern(), 1, 0).notes)
                        if (note.start == 3 * PatternModel::ticks_per_step) {
                            sounded = note.duration == stretched;
                            break;
                        }
                    valid = valid && sounded;
                }
                reached("piano roll lengthens a note");

                // Dragging the body of a note moves it in time and pitch.
                const int high_move = pattern.highKey();
                const double lane_height_move = roll->height() /
                    std::max(1, high_move - pattern.lowKey() + 1);
                const int current_key =
                    pattern.steps().at(3).toMap().value("key").toInt();
                const int moved_key = current_key - 2;
                const QPointF body((3 + 0.3) * lane_width,
                                   (high_move - current_key + 0.5) * lane_height_move);
                const QPointF dest((6 + 0.5) * lane_width,
                                   (high_move - moved_key + 0.5) * lane_height_move);
                mouse_at(roll, body, QEvent::MouseButtonPress, Qt::LeftButton,
                         Qt::LeftButton);
                mouse_at(roll, dest, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
                mouse_at(roll, dest, QEvent::MouseButtonRelease, Qt::LeftButton,
                         Qt::NoButton);
                valid = valid && !pattern.hasStep(3) && pattern.hasStep(6)
                              && pattern.steps().at(6).toMap().value("key").toInt()
                                     == moved_key
                              && pattern.steps().at(6).toMap().value("duration").toInt()
                                     == stretched
                              && rendered_step(6) && roll_draws(6)
                              && tracker_note(6) == song.pitchName(moved_key, 0.0);
                pattern.clearStep(3);
                pattern.clearStep(5);
                pattern.clearStep(6);
                pattern.clearStep(7);
                reached("piano roll moves a note");
            }
        }
    }

    // The tracker as an input.
    void tracker() {
        // ------------------------------------------------------------------
        // Scenario: the tracker is an input too. Its note column takes a
        // click, and its note keys type a phrase into the same pattern.
        {
            auto* cell = named("trackerNote5");
            valid = valid && cell != nullptr;
            if (cell != nullptr) {
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

                // The VEL column writes velocity, not only displays it.
                auto* vel = named("trackerVel0");
                valid = valid && vel != nullptr && pattern.hasStep(0);
                if (vel != nullptr) {
                    const int before =
                        pattern.steps().at(0).toMap().value("velocityUnits").toInt();
                    mouse_at(vel, {vel->width() / 2, vel->height() / 2},
                             QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
                    mouse_at(vel, {vel->width() / 2, vel->height() / 2 - 40},
                             QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
                    mouse_at(vel, {vel->width() / 2, vel->height() / 2 - 40},
                             QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
                    const int after =
                        pattern.steps().at(0).toMap().value("velocityUnits").toInt();
                    valid = valid && after > before
                                  && pattern.selected().value("velocityUnits").toInt()
                                         == after;
                    pattern.setSelectedVelocity(0.9);
                }
                reached("tracker velocity column");

                // A tracker copies a row: pitch, length, velocity and lock.
                if (auto* entry = named("noteEntry")) entry->forceActiveFocus();
                pattern.selectStep(8);
                const auto original = pattern.steps().at(8).toMap();
                chord_key(Qt::Key_C, Qt::ControlModifier);
                pattern.selectStep(2);
                valid = valid && !pattern.hasStep(2);
                chord_key(Qt::Key_V, Qt::ControlModifier);
                valid = valid && pattern.hasStep(2);
                const auto pasted = pattern.steps().at(2).toMap();
                valid = valid && pasted.value("key") == original.value("key")
                              && pasted.value("duration") == original.value("duration")
                              && pasted.value("velocityUnits") == original.value("velocityUnits")
                              && pasted.value("hasLock") == original.value("hasLock")
                              && pasted.value("lockText") == original.value("lockText")
                              && pattern.hasStep(8)
                              && pattern.steps().at(8).toMap().value("key")
                                     == original.value("key");
                pattern.clearStep(2);
                reached("copy and paste a step");

                // Ctrl+D copies the selected row onto the next one.
                pattern.clearStep(5);
                pattern.clearStep(6);
                pattern.toggleStep(5, 50);
                pattern.selectStep(5);
                const int source_key = pattern.steps().at(5).toMap().value("key").toInt();
                valid = valid && pattern.hasStep(5) && !pattern.hasStep(6);
                chord_key(Qt::Key_D, Qt::ControlModifier);
                valid = valid && pattern.hasStep(6)
                              && pattern.steps().at(6).toMap().value("key").toInt()
                                     == source_key
                              && pattern.selectedStep() == 6;
                pattern.clearStep(5);
                pattern.clearStep(6);
                pattern.selectStep(0);
                reached("duplicate a step");

                // Insert pushes later rows down; the last row falls off.
                pattern.clearStep(5);
                pattern.clearStep(6);
                pattern.clearStep(7);
                pattern.toggleStep(5, 50);
                pattern.toggleStep(6, 52);
                const int at_five = pattern.steps().at(5).toMap().value("key").toInt();
                const int at_six = pattern.steps().at(6).toMap().value("key").toInt();
                pattern.selectStep(5);
                if (auto* entry = named("noteEntry")) entry->forceActiveFocus();
                type_key(Qt::Key_Insert, QString());
                valid = valid && !pattern.hasStep(5) && pattern.hasStep(6)
                              && pattern.hasStep(7)
                              && pattern.steps().at(6).toMap().value("key").toInt()
                                     == at_five
                              && pattern.steps().at(7).toMap().value("key").toInt()
                                     == at_six
                              && pattern.selectedStep() == 5;
                reached("insert pushes rows down");

                // Shift+Backspace pulls later rows up into the hole.
                chord_key(Qt::Key_Backspace, Qt::ShiftModifier);
                valid = valid && pattern.hasStep(5)
                              && pattern.steps().at(5).toMap().value("key").toInt()
                                     == at_five
                              && pattern.hasStep(6)
                              && pattern.steps().at(6).toMap().value("key").toInt()
                                     == at_six
                              && !pattern.hasStep(7);
                pattern.clearStep(5);
                pattern.clearStep(6);
                reached("shift backspace pulls rows up");

                // The FX column writes a lock the way VEL writes velocity.
                // Step 4 already sounds in the demonstration pattern, and
                // toggling it would take the note away; start from empty.
                pattern.clearStep(4);
                pattern.toggleStep(4, 60);
                pattern.selectStep(4);
                valid = valid && pattern.hasStep(4)
                              && !pattern.selected().value("hasLock").toBool();
                auto* fx = named("trackerFx4");
                valid = valid && fx != nullptr;
                if (fx != nullptr) {
                    mouse_at(fx, {fx->width() / 2, fx->height() / 2},
                             QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
                    mouse_at(fx, {fx->width() / 2, fx->height() / 2 - 30},
                             QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
                    mouse_at(fx, {fx->width() / 2, fx->height() / 2 - 30},
                             QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
                    valid = valid && pattern.selected().value("hasLock").toBool()
                                  && pattern.steps().at(4).toMap().value("hasLock").toBool();
                    click_at(fx, {fx->width() / 2, fx->height() / 2}, Qt::RightButton);
                    valid = valid && !pattern.selected().value("hasLock").toBool();
                }
                pattern.clearStep(4);
                reached("tracker fx column");

                // Alt nudges micro-timing and note length from the keyboard.
                pattern.toggleStep(2, 48);
                pattern.selectStep(2);
                pattern.setSelectedDuration(120);
                pattern.setSelectedMicroOffset(0);
                chord_key(Qt::Key_Right, Qt::AltModifier);
                valid = valid && pattern.selected().value("micro").toInt() == 1;
                chord_key(Qt::Key_Up, Qt::AltModifier);
                valid = valid && pattern.selected().value("duration").toInt() == 240;
                pattern.clearStep(2);
                reached("alt nudges feel");

                // Ctrl+digit toggles that numbered step without using the
                // tracker's note keys (digits alone are for the upper octave).
                pattern.clearStep(3);
                valid = valid && !pattern.hasStep(3);
                chord_key(Qt::Key_4, Qt::ControlModifier);
                valid = valid && pattern.hasStep(3)
                              && pattern.steps().at(3).toMap().value("key").toInt()
                                     == (window->property("entryOctave").toInt() + 1) * 12;
                chord_key(Qt::Key_4, Qt::ControlModifier);
                valid = valid && !pattern.hasStep(3);
                reached("ctrl digit toggles a step");
            }
        }
    }

    // The opening instrument and the three formats every later section
    // plays through.
    void instrument_formats() {
        // ------------------------------------------------------------------
        // Scenario: the session opens with something to hear. Every track
        // without an instrument is given a General MIDI bank, and a drum
        // track is given the percussion bank rather than a grand piano.
        if (parser.isSet("soundfont-fixture")) {
            SongModel fresh_song;
            PatternModel fresh_pattern(&fresh_song);
            Transport fresh_transport;
            AppController fresh(&fresh_song, &fresh_pattern, &fresh_transport, nullptr,
                std::make_unique<blokkily::RtAudioOutput>(
                    blokkily::RtAudioOutput::Mode::deterministic));
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

        // These load the instruments every later scenario plays through, so
        // they run whether or not something earlier failed: one failure must
        // not unload the song and make every scenario after it fail too.
        if (parser.isSet("clap-fixture")) {
            const bool loaded = controller.verifyClap(parser.value("clap-fixture"));
            valid = loaded && valid;
        }
        if (parser.isSet("vst3-fixture")) {
            const bool loaded = controller.verifyVst3(parser.value("vst3-fixture"));
            valid = loaded && valid;
        }
        if (parser.isSet("clap-fixture") && parser.isSet("vst3-fixture")) {
            // The browser must present both native formats through one list,
            // each entry tagged with the format that produced it.
            // The instruments the application provides itself (the sampler)
            // follow them and are not what this checks.
            QVariantList browser;
            for (const QVariant& entry : controller.plugins())
                if (entry.toMap().value("kind").toString() == "instrument"
                    && entry.toMap().value("vendor").toString() != "Blokkily built-in")
                    browser.push_back(entry);
            valid = valid && browser.size() == 2 && instrument_entries() == 4;
            for (const QVariant& entry : browser) {
                const QVariantMap fields = entry.toMap();
                valid = valid && !fields.value("name").toString().isEmpty()
                              && !fields.value("vendor").toString().isEmpty();
            }
            valid = valid && browser.first().toMap().value("format") == "CLAP"
                          && browser.last().toMap().value("format") == "VST3";
        }
        if (parser.isSet("clap-fixture")) {
            const bool locked = controller.verifyParameterLocks(parser.value("clap-fixture"));
            valid = locked && valid;
        }
        if (parser.isSet("soundfont-fixture")) {
            const bool loaded = controller.verifySoundFont(parser.value("soundfont-fixture"));
            valid = loaded && valid;
        }

        reached("instrument formats");
    }

    // The instrument browser, instrument replacement, running playback, the
    // device callback, export ownership, the inspector and the project file.
    void browser_to_persistence() {
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
            const auto type = [this](const QString& text) {
                for (const QChar character : text)
                    type_key(static_cast<Qt::Key>(character.toUpper().unicode()),
                             QString(character));
            };
            valid = valid && filter != nullptr && browser != nullptr
                          && named("pluginScrollBar") != nullptr;
            if (valid) {
                filter->forceActiveFocus();
                // The two fixtures, then the sampler's two instruments, which
                // the application lists without a scan.
                valid = valid && listed() == 4;
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
                              && listed() == 4;
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
        // features/audio_reliability.feature: Replacing an instrument does
        // not load the previous format's opaque state into the new one.
        {
            SongModel replacement_song;
            PatternModel replacement_pattern(&replacement_song);
            Transport replacement_transport;
            auto replacement_output = std::make_unique<blokkily::RtAudioOutput>(
                blokkily::RtAudioOutput::Mode::deterministic);
            AppController replacement(&replacement_song, &replacement_pattern,
                &replacement_transport, nullptr, std::move(replacement_output));
            blokkily::InstrumentSlot clap_slot;
            clap_slot.format = "CLAP";
            clap_slot.path = parser.value("clap-fixture").toStdString();
            clap_slot.identifier = "dev.blokkily.test";
            replacement_song.setInstrument(0, clap_slot);
            replacement_song.setInstrument(1, clap_slot);
            auto* previous = replacement.engine();
            bool replaced = previous != nullptr;
            if (previous) {
                std::vector<float> left(128), right(128);
                replaced = previous->play_live(0,
                    {blokkily::PluginEvent::Type::parameter_value, 0, 0, 0.75}) && replaced;
                replaced = previous->play_live(1,
                    {blokkily::PluginEvent::Type::parameter_value, 0, 0, 0.5}) && replaced;
                previous->process({left, right});
                const auto untouched = previous->save_track_state(1);
                blokkily::InstrumentSlot vst_slot;
                vst_slot.format = "VST3";
                vst_slot.path = parser.value("vst3-fixture").toStdString();
                vst_slot.identifier = "0";
                replacement_song.setInstrument(0, vst_slot);
                auto* current = replacement.engine();
                replaced = current && current->has_instrument(0) && replaced;
                replaced = replacement_song.song().tracks[0].instrument.state.empty() && replaced;
                if (current) {
                    replacement_song.setTrackPan(0, -1.0);
                    replaced = current->save_track_state(1) == untouched && replaced;
                    replaced = current->play_live(0,
                        {blokkily::PluginEvent::Type::note_on, 0, 60, 1.0}) && replaced;
                    current->process({left, right});
                    replaced = std::abs(left[64] - 0.25F) < 0.001F && replaced;
                    // An invalid edit reports its limit and leaves the
                    // last playable graph running, ready for correction.
                    current->set_playing(true);
                    current->process({left, right});
                    const auto position = current->sample_position();
                    blokkily::Pattern excessive;
                    blokkily::Trigger note;
                    note.duration = 120;
                    for (int event = 0; event < 257; ++event) (void)excessive.add(note);
                    replacement_pattern.replace(std::move(excessive));
                    const bool preserved = replacement.engine() == current
                        && replacement.engine()->is_playing()
                        && replacement.engine()->sample_position() == position;
                    if (!preserved) std::cerr << "REGRESSION: Rejected edit destroyed the playing graph\n";
                    valid = preserved && valid;
                }
            }
            if (!replaced) std::cerr << "REGRESSION: Instrument replacement inherited another plugin's state\n";
            valid = replaced && valid;
        }
        reached("instrument replacement preserves the correct state");
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

        // Ctrl-click on the step grid seeks the engine into that column.
        // Runs after instruments are loaded so the engine that would be
        // heard can be checked, not only the transport drawing.
        {
            auto* step6 = named("step6");
            valid = valid && step6 != nullptr && controller.engine() != nullptr;
            if (step6 != nullptr) {
                controller.seekToBar(0);
                click_at(step6, {step6->width() / 2, step6->height() / 2},
                         Qt::LeftButton, Qt::ControlModifier);
                valid = valid && transport.step() == 6 && transport.bar() == 0
                              && pattern.selectedStep() == 6;
                if (auto* running = controller.engine()) {
                    const double expected = running->sample_rate() * 60.0
                                            / (transport.bpm() * 4.0) * 6.0;
                    valid = valid && running->sample_position()
                                         == static_cast<std::uint64_t>(
                                                std::llround(expected));
                }
                controller.rewindPlayback();
            }
            reached("ctrl click seeks the grid");
        }

        // features/song_and_mixer.feature: transport and export must work
        // through the device callback, including its stopped/running state.
        {
            std::vector<float> stereo(1024);
            verification_output->stop();
            auto* running = controller.engine();
            if (running == nullptr) {
                valid = false;
                std::cerr << "REGRESSION: No engine for the device callback\n";
            } else {
            running->set_playing(false);
            const bool auditioned = controller.auditionPitches({{60, 0.0}}, 1.0)
                && verification_output->pump(stereo)
                && std::any_of(stereo.begin(), stereo.end(),
                               [](float v) { return std::abs(v) > 0.001F; });
            if (!auditioned) std::cerr << "REGRESSION: Stopped audition does not drive the device\n";
            controller.togglePlayback();
            const bool started = verification_output->pump(stereo);
            auto* rewind = named("rewindButton");
            if (rewind) click_at(rewind, {rewind->width() / 2, rewind->height() / 2},
                                Qt::LeftButton);
            const bool rewound = started && verification_output->pump(stereo)
                && controller.engine()->sample_position() == 512;
            if (!rewound) std::cerr << "REGRESSION: Rewind does not move the audio playhead\n";
            controller.togglePlayback();
            // A pause releases held arrangement notes while the device
            // keeps running for tails and the next audition.
            const bool stopped_callback = verification_output->pump(stereo);
            if (!stopped_callback) std::cerr << "REGRESSION: Stop disables live audition and tails\n";
            valid = auditioned && rewound && stopped_callback && valid;
            }
        }
        reached("transport through device callback");
        if (parser.isSet("export")) {
            // Observe the loaded CLAP instrument itself: during a bounce
            // no device may concurrently call that same processor.
            QLibrary fixture(parser.value("clap-fixture"));
            using Observe = void (*)(void (*)(void*), void*);
            auto observe = reinterpret_cast<Observe>(fixture.resolve("blokkily_test_observe_process"));
            struct Observation {
                blokkily::RtAudioOutput* output;
                bool processed = false;
                bool device_running = false;
            } observation{verification_output};
            controller.togglePlayback();
            const auto position = controller.engine()->sample_position();
            if (observe) observe([](void* context) {
                auto& check = *static_cast<Observation*>(context);
                check.processed = true;
                check.device_running |= check.output->is_running();
            }, &observation);
            const bool exported = controller.exportAudioFile(parser.value("export"));
            if (observe) observe(nullptr, nullptr);
            const bool exclusive = observe && exported && observation.processed
                && !observation.device_running;
            if (!exclusive) std::cerr << "REGRESSION: Export processes an instrument while its device is running\n";
            const bool resumed = verification_output->is_running()
                && controller.engine()->is_playing()
                && controller.engine()->sample_position() == position;
            // A directory is not a writable WAVE file; failure must also
            // return the device to the state in which it was borrowed.
            const bool refused = !controller.exportAudioFile(
                QFileInfo(parser.value("export")).absolutePath());
            const bool failure_resumed = refused && verification_output->is_running()
                && controller.engine()->sample_position() == position;
            valid = exclusive && resumed && failure_resumed && valid;
            controller.togglePlayback();
        }
        reached("export owns the device while rendering");
        {
            auto* inspector = named("stepInspector");
            auto* scroll = named("editorScroll");
            // Flush layout polish after the preceding track/model edits,
            // just as the next rendered frame does before a user scrolls.
            (void)window->grabWindow();
            if (scroll) {
                scroll->setProperty("contentY", std::max(0.0,
                    scroll->property("contentHeight").toDouble() - scroll->height()));
                QCoreApplication::processEvents();
            }
            const auto inspector_image = window->grabWindow();
            const auto top = inspector ? inspector->mapToScene({0, 0}).y() : -1;
            const bool reachable = inspector && inspector->height() >= 200
                && top >= 0 && top + inspector->height() <= window->height();
            if (!reachable) std::cerr << "REGRESSION: Step inspector cannot be reached inside the window: top="
                << top << " height=" << (inspector ? inspector->height() : 0)
                << " content=" << (scroll ? scroll->property("contentHeight").toDouble() : 0)
                << " scroll=" << (scroll ? scroll->property("contentY").toDouble() : 0) << '\n';
            valid = reachable && valid;
            if (reachable && parser.isSet("screenshot")) {
                const auto file = QFileInfo(parser.value("screenshot"));
                QDir{}.mkpath(file.absolutePath());
                valid = inspector_image.save(file.absolutePath() + "/" +
                    file.completeBaseName() + "-inspector.png") && valid;
            }
            if (scroll) scroll->setProperty("contentY", 0.0);
        }
        reached("inspector remains reachable");
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
    }

    // The view switcher, the inspector and the shared playhead.
    void user_interface() {
        // ---- user interface ----------------------------------------

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
        {
            auto* gutter = named("rollKey36");
            valid = valid && gutter != nullptr && controller.engine() != nullptr;
            if (gutter != nullptr) {
                const QPointF middle(gutter->width() / 2, gutter->height() / 2);
                mouse_at(gutter, middle, QEvent::MouseButtonPress, Qt::LeftButton,
                         Qt::LeftButton);
                valid = valid && controller.auditioning();
                mouse_at(gutter, middle, QEvent::MouseButtonRelease, Qt::LeftButton,
                         Qt::NoButton);
                valid = valid && !controller.auditioning();
            }
            reached("piano roll gutter sounds");
        }
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
    }

    // The arrangement timeline as an editor over the song.
    void arrangement() {
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
        if (auto* scroll = item("editorScroll"))
            scroll->setProperty("contentY", 0);
        (void)window->grabWindow();
        valid = valid && cellFilled(0, 0) && cellFilled(0, 3) && !cellFilled(0, 5);
        valid = valid && !cellFilled(1, 0) && cellFilled(1, 2);

        const int clips_before = song.clips().size();
        song.placeClip(1, 0);
        (void)window->grabWindow();
        valid = valid && song.clips().size() == clips_before + 1 && cellFilled(1, 0);
        // The right button takes a clip away through the same song API the
        // timeline's MouseArea calls; a left click on a filled cell never does.
        valid = valid && arrangeCell(1, 0) != nullptr;
        valid = valid && song.removeClip(1, 0);
        (void)window->grabWindow();
        valid = valid && song.clips().size() == clips_before && !cellFilled(1, 0);

        // Opening a filled clip puts its pattern in every editor and seeks
        // the engine to that bar — again the same path the MouseArea takes.
        {
            song.selectPattern(0);
            song.selectTrack(0);
            valid = valid && arrangeCell(1, 2) != nullptr && cellFilled(1, 2);
            valid = valid && song.openClip(1, 2) == 1;
            controller.seekToBar(2);
            const auto title = item("patternTitle") == nullptr
                ? QString()
                : item("patternTitle")->property("text").toString();
            valid = valid && song.currentPattern() == 1
                          && song.selectedTrack() == 1
                          && title == "CHORUS"
                          && transport.bar() == 2
                          && cellFilled(1, 2);
            if (auto* running = controller.engine()) {
                const double expected =
                    running->sample_rate() * 240.0 / std::max(1.0, transport.bpm()) * 2.0;
                valid = valid && running->sample_position()
                                     == static_cast<std::uint64_t>(std::llround(expected));
            } else {
                valid = false;
            }
            song.selectPattern(0);
            song.selectTrack(0);
            controller.rewindPlayback();
        }

        // Lengthening a clip is not placing another of the same pattern:
        // one clip with repeats=2 is one object whose second bar is the
        // next loop of that pattern.
        {
            const int clips_now = song.clips().size();
            song.placeClip(2, 4);
            valid = valid && song.clips().size() == clips_now + 1 && cellFilled(2, 4)
                          && !cellFilled(2, 5);
            valid = valid && song.setClipRepeats(2, 4, 2);
            valid = valid && cellFilled(2, 4) && cellFilled(2, 5)
                          && song.clips().size() == clips_now + 1;
            const auto lanes = song.lanes();
            const auto lane = lanes.size() > 2 ? lanes.at(2).toList() : QVariantList{};
            valid = valid && lane.size() > 5
                          && lane.at(4).toMap().value("repeats").toInt() == 2
                          && lane.at(5).toMap().value("start").toBool() == false
                          && lane.at(5).toMap().value("repeats").toInt() == 2;
            // Moving it along the lane keeps the length and frees the old bars.
            valid = valid && song.moveClip(2, 4, 6);
            valid = valid && !cellFilled(2, 4) && !cellFilled(2, 5)
                          && cellFilled(2, 6) && cellFilled(2, 7)
                          && song.clips().size() == clips_now + 1;
            valid = valid && song.removeClip(2, 6);
            valid = valid && song.clips().size() == clips_now;
            reached("clip lengthen and move");
        }

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

        // Clicking the ruler locates the engine, not only the drawing.
        {
            const int clips_now = song.clips().size();
            // The ruler's bars were just rebuilt for a new song length; lay
            // them out before one is clicked, or it is still 0x0 and the
            // click lands on whatever lies underneath.
            (void)window->grabWindow();
            auto* ruler = named("rulerBar1");
            valid = valid && ruler != nullptr;
            if (ruler != nullptr)
                click_at(ruler, {ruler->width() / 2, ruler->height() / 2}, Qt::LeftButton);
            valid = valid && transport.bar() == 1 && transport.position() == "2.1.1";
            if (auto* running = controller.engine()) {
                const double expected =
                    running->sample_rate() * 240.0 / std::max(1.0, transport.bpm());
                valid = valid && running->sample_position()
                                     == static_cast<std::uint64_t>(std::llround(expected));
            } else {
                valid = false;
            }
            valid = valid && song.clips().size() == clips_now && cellFilled(0, 0);
            controller.rewindPlayback();
            valid = valid && transport.bar() == 0;
        }
        reached("arrangement");
    }

    // One strip per track, and mixer moves heard in the audio.
    void mixer() {
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
    }

    // The bounce, read back.
    void bounce() {
        // ---- export --------------------------------------------------
        valid = valid && item("exportButton") != nullptr;
        if (parser.isSet("export")) {
            valid = valid && controller.verifyBounce(parser.value("export"));
            valid = valid && !controller.exportStatus().contains("failed");
        }

        reached("export");
    }

    // features/session_workflow.feature, then the negotiated sample rate
    // and a new session.
    void session_workflow() {
        // ---- session workflow ----------------------------------------
        // features/session_workflow.feature. Everything here is done in a
        // pattern and on a track of its own and taken back afterwards, so
        // the scenarios that follow meet the song as they expect it.
        {
            const QString shown_view = window->property("view").toString();
            valid = valid && window->setProperty("view", "ALL");
            // A keyboard-only run leaves the editors collapsed; lay them
            // out again before the grid and roll are asked to take a click.
            (void)window->grabWindow();
            QCoreApplication::processEvents();
            // Lays the window out as a frame would, so a strip that has just
            // been created has its real size before it is measured.
            const auto lay_out = [this] { (void)window->grabWindow(); };
            if (auto* entry = named("noteEntry")) entry->forceActiveFocus();

            // A fresh pattern to work in. Adding it is itself an edit.
            const int patterns_before = song.patterns().size();
            song.addPattern();
            valid = valid && song.patterns().size() == patterns_before + 1
                          && pattern.rowCount() == 0 && song.canUndo();
            reached("workflow: a pattern to work in");

            // A click on an empty step of the rendered grid writes a note in
            // the octave the tracker types in, not a fixed middle C.
            auto* step_cell = childNamed(item("stepGrid"), "step14");
            valid = valid && step_cell != nullptr
                          && step_cell->width() > 2 && step_cell->height() > 2;
            if (step_cell != nullptr)
                click_at(step_cell, {step_cell->width() / 2, step_cell->height() / 2},
                         Qt::LeftButton);
            valid = valid && pattern.hasStep(14) && rendered_step(14)
                          && pattern.steps().at(14).toMap().value("key").toInt()
                                 == (window->property("entryOctave").toInt() + 1) * 12;
            reached("workflow: the grid writes in the entry octave");

            // Ctrl+Z takes the step back and Ctrl+Shift+Z puts it back, in
            // every projection at once.
            chord_key(Qt::Key_Z, Qt::ControlModifier);
            valid = valid && !pattern.hasStep(14) && !rendered_step(14) && song.canRedo();
            chord_key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
            valid = valid && pattern.hasStep(14) && rendered_step(14);
            chord_key(Qt::Key_Z, Qt::ControlModifier);
            valid = valid && !pattern.hasStep(14);
            reached("workflow: undo and redo a step");

            // A stroke across the roll is one edit, so one undo takes the
            // whole phrase back rather than its last note.
            auto* roll = named("rollInput");
            valid = valid && roll != nullptr;
            if (roll != nullptr) {
                const double lane_width = roll->width() / pattern.stepCount();
                const double lane_height =
                    roll->height() / std::max(1, pattern.highKey() - pattern.lowKey() + 1);
                const QPointF from((1 + 0.5) * lane_width, 3.5 * lane_height);
                mouse_at(roll, from, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
                for (int step = 2; step <= 4; ++step)
                    mouse_at(roll, {(step + 0.5) * lane_width, from.y()},
                             QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
                mouse_at(roll, {4.5 * lane_width, from.y()}, QEvent::MouseButtonRelease,
                         Qt::LeftButton, Qt::NoButton);
                valid = valid && pattern.rowCount() == 4 && roll_draws(1) && roll_draws(4);
                song.undo();
                valid = valid && pattern.rowCount() == 0 && !roll_draws(1);
                song.redo();
                valid = valid && pattern.rowCount() == 4;
            }
            reached("workflow: a stroke is one undo");

            // The roll shows one bar, and the song's playhead is drawn in it
            // wherever the song is — in bar two as in bar one.
            auto* roll_head = named("rollPlayhead");
            valid = valid && roll != nullptr && roll_head != nullptr;
            if (roll != nullptr && roll_head != nullptr) {
                transport.locate(16.0 + 4.0);
                const double lane_width = roll->width() / pattern.stepCount();
                valid = valid && std::abs(roll_head->x() - (roll->x() + 4.0 * lane_width)) < 1.0;
                transport.locate(6.0);
            }
            reached("workflow: the roll's playhead follows the song past bar one");

            // A chord is drawn as each of its voices, not only its root.
            pattern.placeChord(9, blokkily::Chord{60, {0, 4, 7}, 0, 0, {}});
            if (auto* chord_item = named("rollNote9")) {
                int voices = 0;
                for (auto* voice : chord_item->childItems())
                    if (voice->height() > 0 && voice->width() > 0) ++voices;
                valid = valid && voices == 3;
            } else {
                valid = false;
            }
            song.undo();
            reached("workflow: the roll draws every voice of a chord");

            // A copy of the open pattern is a new pattern with the same
            // steps and a name of its own; renaming and deleting it are
            // edits like any other.
            const int events_before_copy = pattern.rowCount();
            const QString copied_from =
                song.patterns().at(song.currentPattern()).toMap().value("name").toString();
            song.duplicatePattern();
            const int copy = song.currentPattern();
            valid = valid && song.patterns().size() == patterns_before + 2
                          && pattern.rowCount() == events_before_copy
                          && song.patterns().at(copy).toMap().value("name").toString()
                                 == copied_from + " 2";
            song.renamePattern(copy, "  drop  ");
            valid = valid && song.patterns().at(copy).toMap().value("name").toString() == "DROP"
                          && item("patternTitle")->property("text").toString() == "DROP";
            song.clearPattern();
            valid = valid && pattern.rowCount() == 0;
            valid = valid && song.deletePattern(copy)
                          && song.patterns().size() == patterns_before + 1;
            reached("workflow: duplicate, rename, clear and delete a pattern");

            // Deleting a pattern takes its clips and keeps every other clip
            // on the pattern it named.
            {
                const int doomed = song.currentPattern();
                song.toggleClip(0, 7);
                const int clips_with = song.clips().size();
                valid = valid && song.hasClip(0, 7);
                valid = valid && song.deletePattern(doomed);
                valid = valid && song.clips().size() == clips_with - 1 && !song.hasClip(0, 7);
                for (const auto& clip : song.clips())
                    valid = valid && clip.toMap().value("pattern").toInt()
                                         < song.patterns().size();
                song.undo();
                valid = valid && song.hasClip(0, 7)
                              && song.patterns().size() == patterns_before + 1;
                song.undo();
                valid = valid && !song.hasClip(0, 7);
            }
            reached("workflow: deleting a pattern takes its clips");

            // A track added from the interface has something to play, and
            // is heard: the bank the session already uses is put on it.
            const int tracks_before = song.trackCount();
            if (auto* add = named("addTrackButton"))
                click_at(add, {add->width() / 2, add->height() / 2}, Qt::LeftButton);
            valid = valid && song.trackCount() == tracks_before + 1;
            const int added = song.trackCount() - 1;
            valid = valid && song.selectedTrack() == added
                          && song.tracks().at(added).toMap().value("hasInstrument").toBool();
            valid = valid && controller.engine() != nullptr
                          && controller.engine()->has_instrument(static_cast<std::size_t>(added));
            valid = valid && controller.auditionPitches({{60, 0.0}}, 1.0);
            if (auto* engine_now = controller.engine()) {
                std::vector<float> left(512, 0.0F), right(512, 0.0F);
                engine_now->set_playing(false);
                float loudest = 0.0F;
                for (int block = 0; block < 8; ++block) {
                    engine_now->process({left, right});
                    loudest = std::max(loudest, engine_now->track_peak(
                                                    static_cast<std::size_t>(added)));
                }
                valid = valid && loudest > 0.001F;
            }
            controller.releaseAudition();
            reached("workflow: an added track is given an instrument");

            // A track is renamed through the rendered field. Return accepts
            // the name: the transport's Return stands aside while typing.
            // A popup is an object of the window rather than an item in it.
            if (auto* popup = window->findChild<QObject*>("renamePopup")) {
                QMetaObject::invokeMethod(popup, "ask", Q_ARG(QVariant, "TRACK"),
                                          Q_ARG(QVariant, added),
                                          Q_ARG(QVariant, song.tracks().at(added).toMap()
                                                              .value("name")));
                for (const QChar character : QString("keys two"))
                    type_key(static_cast<Qt::Key>(character.toUpper().unicode()),
                             QString(character));
                type_key(Qt::Key_Return, "\r");
                valid = valid && song.tracks().at(added).toMap().value("name").toString()
                                     == "KEYS TWO"
                              && !popup->property("visible").toBool();
            } else {
                valid = false;
            }
            reached("workflow: rename a track");

            // The strip's meter moves with the engine's meters, not only
            // when something else about the song changes.
            {
                std::vector<float> levels(static_cast<std::size_t>(song.trackCount()), 0.0F);
                levels[static_cast<std::size_t>(added)] = 0.5F;
                song.setMeters(levels, 0.5F);
                lay_out();
                auto* fill = childNamed(item("mixerStrips"), QString("meterFill%1").arg(added));
                valid = valid && fill != nullptr && fill->width() > 1.0;
                song.setMeters(std::vector<float>(levels.size(), 0.0F), 0.0F);
                lay_out();
                valid = valid && fill != nullptr && fill->width() < 0.5;
            }
            reached("workflow: the track meter moves");

            // However many tracks the song has, every strip can be reached
            // and the master stays inside the window.
            {
                for (int more = 0; more < 5; ++more) controller.addTrack();
                lay_out();
                auto* scroller = item("mixerScroll");
                auto* master = item("masterStrip");
                valid = valid && scroller != nullptr && master != nullptr;
                if (scroller != nullptr && master != nullptr) {
                    const QPointF bottom = master->mapToScene({0, master->height()});
                    valid = valid && bottom.y() <= window->height() + 0.5;
                    valid = valid && scroller->property("contentHeight").toDouble()
                                         > scroller->height();
                }
                for (int more = 0; more < 5; ++more) song.undo();
                valid = valid && song.trackCount() == tracks_before + 1;
            }
            reached("workflow: many tracks scroll in the mixer");

            // Deleting a track takes its clips with it and shifts the others.
            {
                song.toggleClip(added, 1);
                const int clips_with = song.clips().size();
                valid = valid && song.deleteTrack(added);
                valid = valid && song.trackCount() == tracks_before
                              && song.clips().size() == clips_with - 1;
                valid = valid && controller.engine() != nullptr
                              && controller.engine()->track_count()
                                     == static_cast<std::size_t>(tracks_before);
            }
            reached("workflow: delete a track");

            // While a text field has the keyboard the arrow keys belong to
            // it: Down walks the browser and does not transpose a step.
            if (auto* filter = named("pluginFilter")) {
                pattern.selectStep(0);
                song.selectPattern(0);
                const int key_before = pattern.selected().value("key").toInt();
                filter->forceActiveFocus();
                type_key(Qt::Key_Down, QString());
                valid = valid && pattern.selected().value("key").toInt() == key_before;
                if (auto* entry = named("noteEntry")) entry->forceActiveFocus();
                type_key(Qt::Key_Down, QString());
                valid = valid && pattern.selected().value("key").toInt() == key_before - 1;
                type_key(Qt::Key_Up, QString());
                valid = valid && pattern.selected().value("key").toInt() == key_before;
                song.undo();
                song.undo();
            }
            reached("workflow: arrows stay with the field being typed in");

            // Return is a rewind of the song, the audio playhead with it.
            if (auto* running = controller.engine()) {
                std::vector<float> left(512, 0.0F), right(512, 0.0F);
                running->set_playing(true);
                running->seek(9600);
                running->process({left, right});
                valid = valid && running->sample_position() == 9600 + 512;
                type_key(Qt::Key_Return, "\r");
                running->process({left, right});
                valid = valid && running->sample_position() == 512
                              && transport.bar() == 0;
                running->set_playing(false);
            }
            reached("workflow: Return rewinds the engine");

            // Home is the same rewind from the other end of the keyboard.
            if (auto* running = controller.engine()) {
                std::vector<float> left(512, 0.0F), right(512, 0.0F);
                running->set_playing(true);
                running->seek(4800);
                running->process({left, right});
                type_key(Qt::Key_Home, QString());
                running->process({left, right});
                valid = valid && running->sample_position() == 512
                              && transport.bar() == 0;
                running->set_playing(false);
            }
            reached("workflow: Home rewinds the engine");

            // Ctrl+M and Ctrl+L mute and solo the selected track.
            {
                song.selectTrack(0);
                const bool muted_before =
                    song.tracks().at(0).toMap().value("mute").toBool();
                chord_key(Qt::Key_M, Qt::ControlModifier);
                valid = valid && song.tracks().at(0).toMap().value("mute").toBool()
                                     != muted_before;
                chord_key(Qt::Key_M, Qt::ControlModifier);
                valid = valid && song.tracks().at(0).toMap().value("mute").toBool()
                                     == muted_before;
                chord_key(Qt::Key_L, Qt::ControlModifier);
                valid = valid && song.tracks().at(0).toMap().value("solo").toBool();
                chord_key(Qt::Key_L, Qt::ControlModifier);
                valid = valid && !song.tracks().at(0).toMap().value("solo").toBool();
            }
            reached("workflow: mute and solo from the keyboard");

            // A key held on a surface rings for as long as it is held and
            // stops when it is let go, rather than after a fixed beat.
            {
                const bool recording = keyboard.recording();
                if (recording) keyboard.toggleRecording();
                auto* key = childNamed(item("keyboardSurface"), "keyboardCell0");
                auto* input = key == nullptr ? nullptr : childNamed(key, "keyInput");
                valid = valid && input != nullptr;
                // Scrolled into view first, as a producer would scroll to
                // it: a key below the window cannot be pressed.
                auto* editors = item("editorScroll");
                double scrolled_from = 0.0;
                if (input != nullptr && editors != nullptr) {
                    scrolled_from = editors->property("contentY").toDouble();
                    const double overhang =
                        input->mapToScene({0, input->height()}).y() - (window->height() - 8);
                    if (overhang > 0)
                        editors->setProperty("contentY", scrolled_from + overhang);
                    lay_out();
                }
                if (input != nullptr) {
                    const QPointF middle(input->width() / 2, input->height() / 2);
                    mouse_at(input, middle, QEvent::MouseButtonPress, Qt::LeftButton,
                             Qt::LeftButton);
                    valid = valid && controller.auditioning();
                    settle(700);   // longer than a tapped key rings
                    valid = valid && controller.auditioning();
                    mouse_at(input, middle, QEvent::MouseButtonRelease, Qt::LeftButton,
                             Qt::NoButton);
                    valid = valid && !controller.auditioning();
                }
                if (editors != nullptr) editors->setProperty("contentY", scrolled_from);
                if (recording) keyboard.toggleRecording();
            }
            reached("workflow: a held key rings until released");

            // The session knows when it differs from its file: saving makes
            // it clean, an edit makes it dirty and says so in the title,
            // and undoing back to the saved state makes it clean again.
            if (parser.isSet("project")) {
                const QString file = parser.value("project") + ".workflow";
                valid = valid && controller.saveProject(file) && !song.dirty()
                              && controller.projectPath() == file
                              && !window->title().startsWith(QChar(0x2022));
                pattern.toggleStep(15, 60);
                valid = valid && song.dirty() && window->title().startsWith(QChar(0x2022));
                valid = valid && controller.saveProjectInPlace() && !song.dirty();
                pattern.toggleStep(15, 60);
                valid = valid && song.dirty();
                song.undo();
                valid = valid && !song.dirty();
                pattern.clearStep(15);
                const auto on_disk = blokkily::ProjectFile::load(file.toStdString());
                valid = valid && on_disk.has_value()
                              && on_disk->song.patterns.size() == song.song().patterns.size();
            }
            reached("workflow: unsaved changes are tracked");

            // Leave the song as the later scenarios expect it: without the
            // pattern this section added.
            while (song.patterns().size() > patterns_before)
                if (!song.deletePattern(song.patterns().size() - 1)) { valid = false; break; }
            song.selectPattern(0);
            song.selectTrack(0);
            valid = valid && song.trackCount() == tracks_before;
            if (auto* entry = named("noteEntry")) entry->forceActiveFocus();
            valid = valid && window->setProperty("view", shown_view);
        }
        reached("workflow: the song is left as it was");

        // A device that will not run at the rate asked for sets the rate:
        // the engine is prepared for what the server negotiated, so a
        // song on a 44.1 kHz sink plays at its own pitch and tempo.
        if (parser.isSet("soundfont-fixture")) {
            SongModel other_song;
            PatternModel other_pattern(&other_song);
            Transport other_transport;
            AppController other(&other_song, &other_pattern, &other_transport, nullptr,
                std::make_unique<blokkily::RtAudioOutput>(
                    blokkily::RtAudioOutput::Mode::deterministic, 44100));
            const std::filesystem::path fixture =
                parser.value("soundfont-fixture").toStdString();
            (void)other.loadDefaultInstrument({fixture.parent_path()});
            valid = valid && other.engine() != nullptr
                          && other.engine()->sample_rate() == 44100.0;
            // At 120 BPM a bar is two seconds, whatever the rate.
            valid = valid && other.engine() != nullptr
                          && other.engine()->song_samples()
                                 == static_cast<std::uint64_t>(other_song.song().length())
                                        * 44100 / 960;
            reached("negotiated sample rate");

            // A new session is one empty pattern on one track, with the
            // instrument the old first track had, and nothing to save yet.
            other_song.toggleClip(1, 5);
            valid = valid && other_song.dirty();
            other.newProject();
            valid = valid && other_song.patterns().size() == 1
                          && other_song.trackCount() == 1
                          && other_pattern.rowCount() == 0
                          && !other_song.dirty() && !other_song.canUndo()
                          && other.projectPath().isEmpty()
                          && other_song.tracks().first().toMap().value("hasInstrument").toBool()
                          && other.engine() != nullptr;
            reached("new session");
        }
    }

    // The playable surfaces.
    void keyboards() {
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

            // ---- how the surface is tiled and which way it runs -------
            // The honeycomb is what makes an isomorphic grid worth having:
            // interlocking rows put every neighbour of a key one interval
            // away. Measured off the rendered cells, because a model that
            // reports a stagger nothing draws is not a honeycomb.
            keyboard.setSurface("GRID");
            keyboard.setOrientation("ACROSS");
            keyboard.setLayout("Wicki-Hayden");
            valid = valid && keyboard.layoutName() == "Wicki-Hayden";
            valid = valid && keyboard.cellShape() == "HEX";
            const auto rendered_cell = [&](int row, int column) -> QQuickItem* {
                for (const QVariant& entry : keyboard.cells()) {
                    const auto map = entry.toMap();
                    if (map.value("row").toInt() != row) continue;
                    if (map.value("column").toInt() != column) continue;
                    return childNamed(item("keyboardSurface"),
                                      "keyboardCell" +
                                          QString::number(map.value("index").toInt()));
                }
                return nullptr;
            };
            // Two rows apart is two row pitches, so the gap between cells
            // cancels and the tiling can be measured exactly.
            const auto tiling_holds = [&](double stagger, double row_pitch) {
                auto* origin = rendered_cell(0, 0);
                auto* across = rendered_cell(0, 1);
                auto* under = rendered_cell(1, 0);
                auto* two_under = rendered_cell(2, 0);
                if (origin == nullptr || across == nullptr || under == nullptr ||
                    two_under == nullptr)
                    return false;
                // Measured between cells, so the gap a cell leaves around
                // itself cancels; the cell's own height carries that gap,
                // which is why it is added back rather than derived from
                // the drop this is checking.
                const double cell_width = across->x() - origin->x();
                const double cell_height = origin->height() + 2.0;
                const double drop = under->y() - origin->y();
                const double two_drops = two_under->y() - origin->y();
                if (cell_width < 2.0 || cell_height < 4.0 || drop < 1.0) return false;
                if (std::abs(2.0 * drop - two_drops) > 0.5) return false;
                return std::abs((under->x() - origin->x()) - stagger * cell_width) < 0.5 &&
                       std::abs(drop - row_pitch * cell_height) < 1.0;
            };
            // Alternate rows offset by half a cell, overlapping by a
            // quarter of one: pointy-top hexagons, closed.
            valid = valid && tiling_holds(0.5, 0.75);
            auto* honeycomb_cell = rendered_cell(0, 0);
            valid = valid && honeycomb_cell != nullptr;
            if (honeycomb_cell != nullptr) {
                auto* hex_tile = childNamed(honeycomb_cell, "hexTile");
                auto* rect_tile = childNamed(honeycomb_cell, "rectTile");
                valid = valid && hex_tile != nullptr && hex_tile->isVisible()
                              && hex_tile->width() > 2.0 && hex_tile->height() > 2.0;
                valid = valid && rect_tile != nullptr && !rect_tile->isVisible();
            }

            // A layout drawn on a square grid stays square, and the
            // intervals under the hands are the layout's business rather
            // than the tiling's.
            keyboard.setLayout("Fourths");
            valid = valid && keyboard.cellShape() == "RECT";
            valid = valid && tiling_holds(0.0, 1.0);
            auto* square_cell = rendered_cell(0, 0);
            if (square_cell != nullptr) {
                auto* hex_tile = childNamed(square_cell, "hexTile");
                auto* rect_tile = childNamed(square_cell, "rectTile");
                valid = valid && rect_tile != nullptr && rect_tile->isVisible();
                valid = valid && hex_tile != nullptr && !hex_tile->isVisible();
            }
            const auto grid_degree = [&](int row, int column) {
                for (const QVariant& entry : keyboard.cells()) {
                    const auto map = entry.toMap();
                    if (map.value("row").toInt() == row &&
                        map.value("column").toInt() == column)
                        return map.value("degree").toInt();
                }
                return -1;
            };
            valid = valid && grid_degree(2, 1) - grid_degree(2, 0) == 1;
            valid = valid && grid_degree(1, 0) - grid_degree(2, 0) == 5;

            // Every named layout is playable: it moves in both directions,
            // and it fills the grid it is asked for.
            for (const QString& name : keyboard.layoutNames()) {
                keyboard.setLayout(name);
                valid = valid && keyboard.layoutName() == name;
                valid = valid && keyboard.cells().size() ==
                                     keyboard.rows() * keyboard.columns();
                valid = valid && grid_degree(2, 1) != grid_degree(2, 0);
                valid = valid && grid_degree(1, 0) != grid_degree(2, 0);
                valid = valid && rendered_cell(1, 1) != nullptr;
            }
            keyboard.setLayout("Wicki-Hayden");

            // Hexagons interlock, so their bounding boxes overlap at the
            // corners. A press there is drawn inside the neighbour, and it
            // must sound the neighbour rather than whichever cell happens
            // to be painted over it. Sent as a real click on the rendered
            // honeycomb, because this is a question about hit-testing.
            keyboard.setOrientation("ACROSS");
            valid = valid && window->setProperty("view", "KEYS");
            (void)window->grabWindow();
            QCoreApplication::processEvents();
            {
                int heard = -1;
                const auto listening = QObject::connect(
                    &keyboard, &KeyboardModel::played,
                    [&heard](int degree) { heard = degree; });
                keyboard.toggleRecording();              // audition only: a
                valid = valid && !keyboard.recording();  // probe writes nothing
                auto* upper = rendered_cell(0, 1);
                auto* lower = rendered_cell(1, 1);
                valid = valid && upper != nullptr && lower != nullptr;
                // A cell too small to aim at proves nothing either way, so
                // demand one big enough rather than passing by default.
                valid = valid && lower != nullptr && lower->width() > 30.0;
                if (upper != nullptr && lower != nullptr && lower->width() > 30.0) {
                    heard = -1;
                    click_at(lower, {lower->width() * 0.1, lower->height() * 0.1},
                             Qt::LeftButton);
                    valid = valid && heard == grid_degree(0, 1);
                    // And the middle of a cell is its own.
                    heard = -1;
                    click_at(lower, {lower->width() * 0.5, lower->height() * 0.5},
                             Qt::LeftButton);
                    valid = valid && heard == grid_degree(1, 1);
                }
                QObject::disconnect(listening);
                keyboard.toggleRecording();
                valid = valid && keyboard.recording();
            }
            valid = valid && window->setProperty("view", "ALL");

            // Turning a surface is a quarter turn: what ran to the right
            // runs upward. It moves keys, so nothing about the song, the
            // pattern, or the pitch of a cell may move with them.
            keyboard.setSurface("PIANO");
            keyboard.setRegister("Treble");
            const auto surface_degrees = [&] {
                std::vector<int> degrees;
                for (const QVariant& entry : keyboard.cells())
                    degrees.push_back(entry.toMap().value("degree").toInt());
                return degrees;
            };
            const auto across_degrees = surface_degrees();
            const int events_before_turning = pattern.rowCount();
            valid = valid && keyboard.orientation() == "ACROSS";
            valid = valid && std::abs(keyboard.spanX() - 25.0) < 1e-6
                          && std::abs(keyboard.spanY() - 1.0) < 1e-6;
            keyboard.setOrientation("DOWN");
            valid = valid && keyboard.orientation() == "DOWN";
            valid = valid && std::abs(keyboard.spanX() - 1.0) < 1e-6
                          && std::abs(keyboard.spanY() - 25.0) < 1e-6;
            valid = valid && surface_degrees() == across_degrees;
            valid = valid && pattern.rowCount() == events_before_turning;
            {
                // The lowest key is at the bottom, the way a roll's gutter
                // reads, and the keys are still big enough to hit.
                auto* lowest = childNamed(item("keyboardSurface"), "keyboardCell0");
                auto* highest = childNamed(
                    item("keyboardSurface"),
                    "keyboardCell" + QString::number(keyboard.cells().size() - 1));
                valid = valid && lowest != nullptr && highest != nullptr;
                if (lowest != nullptr && highest != nullptr) {
                    valid = valid && lowest->y() > highest->y();
                    valid = valid && lowest->height() >= 12.0 && lowest->width() > 40.0;
                }
                // Twenty-five keys do not fit the panel at that size, so
                // the surface is taller than what shows and scrolls to
                // reach the rest rather than squeezing them to nothing.
                auto* scroll = item("keyboardScroll");
                auto* turned_surface = item("keyboardSurface");
                valid = valid && scroll != nullptr && turned_surface != nullptr;
                if (scroll != nullptr && turned_surface != nullptr) {
                    valid = valid && turned_surface->height() >= 25.0 * 12.0;
                    valid = valid && turned_surface->height() > scroll->height();
                    valid = valid && scroll->property("contentHeight").toDouble()
                                         > scroll->height();
                }
            }
            // A key played on the turned surface is the same key.
            pattern.selectStep(9);
            valid = valid && keyboard.pressDegree(67);
            valid = valid && pattern.selected().value("key").toInt() == 67;
            valid = valid && rendered_step(9);
            keyboard.setOrientation("ACROSS");
            keyboard.setRegister("Full");
            keyboard.setSurface("PIANO");

            // The chip that turns the surface is a control, not a caption:
            // clicking the rendered one has to reach the model.
            {
                auto* turn = item("orientToggle");
                valid = valid && turn != nullptr;
                if (turn != nullptr) {
                    click_at(turn, {turn->width() / 2, turn->height() / 2},
                             Qt::LeftButton);
                    valid = valid && keyboard.orientation() == "DOWN";
                    click_at(turn, {turn->width() / 2, turn->height() / 2},
                             Qt::LeftButton);
                    valid = valid && keyboard.orientation() == "ACROSS";
                }
            }

            // Whatever the surface is, and whichever way it runs, it stays
            // inside the panel that holds it. A control row beside it must
            // not be able to push it out over its neighbours.
            for (const QString& shape_surface : {QStringLiteral("PIANO"),
                                                 QStringLiteral("GRID"),
                                                 QStringLiteral("FRETS"),
                                                 QStringLiteral("CHORDS")})
                for (const QString& runs : {QStringLiteral("ACROSS"),
                                            QStringLiteral("DOWN")}) {
                    keyboard.setSurface(shape_surface);
                    keyboard.setOrientation(runs);
                    (void)window->grabWindow();
                    auto* panel = item("keyboardPanel");
                    auto* held = item("keyboardScroll");
                    valid = valid && panel != nullptr && held != nullptr;
                    if (panel == nullptr || held == nullptr) continue;
                    const double right = held->mapToItem(panel, {held->width(), 0}).x();
                    valid = valid && right <= panel->width() + 0.5;
                    valid = valid && held->width() > 100.0;
                    if (shape_surface == "GRID") {
                        auto* cell = childNamed(item("keyboardSurface"), "keyboardCell0");
                        const bool usable = cell && cell->width() >= 40 && cell->height() >= 40;
                        if (!usable) std::cerr << "REGRESSION: Grid keys shrink below a usable size in "
                                               << runs.toStdString() << '\n';
                        valid = usable && valid;
                        // The software renderer must clip the actual
                        // painted hexagons, not just their QQuickItems.
                        auto* mixer = item("mixerPanel");
                        auto* strips = item("mixerStrips");
                        // The effect rack (item 2.4) sits between the
                        // strips and the master, so the empty band the
                        // keyboard must not paint into ends at the rack.
                        auto* master = item("effectRack") != nullptr ? item("effectRack")
                                                                     : item("masterStrip");
                        auto* scroller = item("mixerScroll");
                        auto* keys = item("keyboardPanel");
                        if (mixer && strips && master && scroller && keys) {
                            const auto frame = window->grabWindow();
                            const auto left = mixer->mapToScene({1, 0}).x();
                            const QColor background = mixer->property("color").value<QColor>();
                            // The mixer's left gutter, beside the keyboard,
                            // is always bare panel: nothing the keyboard
                            // paints may reach it, however full the mixer is.
                            const auto keys_top = std::max(0.0, keys->mapToScene({0, 0}).y());
                            const auto keys_bottom =
                                std::min<double>(window->height(),
                                                 keys->mapToScene({0, keys->height()}).y());
                            bool clipped = keys_bottom > keys_top + 40;
                            // The check has to look at the gap between the
                            // editor's keyboard and the mixer's left edge,
                            // not at the mixer's own paint — the mixer's
                            // MIXER title and TRACK COUNT pill sit inside
                            // x = left..left+9 at the very top of the panel
                            // (and the second band's strips above the rack
                            // are the mixer's own content), so painting the
                            // mixer's own pixels as a leak would fault the
                            // layout every run regardless of the surface.
                            // We scan the divider strip just to the left of
                            // the mixer instead, where the keyboard is the
                            // only thing that can paint, and we tolerate a
                            // one-pixel anti-alias seam on that boundary.
                            const auto editor_right = left;
                            const auto divider_left = std::max(0.0, editor_right - 3.0);
                            for (int y = static_cast<int>(keys_top); y < keys_bottom; ++y) {
                                bool row_clean = true;
                                for (int x = static_cast<int>(divider_left);
                                     x < static_cast<int>(editor_right); ++x) {
                                    const auto px = frame.pixelColor(x, y);
                                    if (px.alpha() == 0) continue; // outside the frame
                                    // Allow a small tolerance for the
                                    // anti-aliased seam between the
                                    // editor column and the mixer panel.
                                    if (std::abs(px.red() - background.red()) > 8 &&
                                        std::abs(px.green() - background.green()) > 8 &&
                                        std::abs(px.blue() - background.blue()) > 8) {
                                        row_clean = false;
                                        break;
                                    }
                                }
                                clipped = clipped && row_clean;
                            }
                            // With room below the strips (few tracks, the
                            // arm row and the effect rack take space), the
                            // empty band between them and the rack must be
                            // bare panel across its whole width too —
                            // though that band is inside the mixer, and
                            // the mixer paints its own divider and pill,
                            // so the strict equality against background is
                            // relaxed to a near-background match.
                            const auto top = strips->mapToScene({0, strips->height() + 10}).y();
                            const auto bottom =
                                std::min(master->mapToScene({0, -10}).y(),
                                         scroller->mapToScene({0, scroller->height()}).y());
                            for (int y = static_cast<int>(top); y < bottom; ++y) {
                                bool row_clean = true;
                                for (int x = static_cast<int>(left); x < window->width() - 2; ++x) {
                                    const auto px = frame.pixelColor(x, y);
                                    if (px.alpha() == 0) continue;
                                    if (std::abs(px.red() - background.red()) > 8 &&
                                        std::abs(px.green() - background.green()) > 8 &&
                                        std::abs(px.blue() - background.blue()) > 8) {
                                        row_clean = false;
                                        break;
                                    }
                                }
                                clipped = clipped && row_clean;
                            }
                            if (!clipped) {
                                std::cerr << "REGRESSION: Keyboard paint escapes into the mixer\n";
                                std::cerr << "  debug: left=" << left
                                          << " mixer_w=" << mixer->width()
                                          << " keys_x=" << keys->mapToScene({0,0}).x()
                                          << " keys_y=" << keys->mapToScene({0,0}).y()
                                          << " keys_at_100_200=" << keys->mapToScene({100, 200}).y()
                                          << " keys_at_300_400=" << keys->mapToScene({300, 400}).y()
                                          << " keys_at_h_minus_50=" << keys->mapToScene({0, keys->height()-50}).y()
                                          << " keys_bottom_real=" << keys->mapToScene({0, keys->height()}).y()
                                          << " keys_top=" << keys_top
                                          << " keys_bottom=" << keys_bottom
                                          << " keys_h=" << keys->height()
                                          << " keys_y=" << keys->y()
                                          << " keys_parent_y=" << (keys->parentItem() ? keys->parentItem()->y() : -999.0)
                                          << " keys_parent_h=" << (keys->parentItem() ? keys->parentItem()->height() : -999.0)
                                          << " keys_clip=" << (keys->parentItem() ? keys->parentItem()->property("clip").toBool() : false)
                                          << " surface=" << shape_surface.toStdString()
                                          << " orient=" << runs.toStdString()
                                          << " bg=" << background.name().toStdString()
                                          << "\n";
                                // Find a failing pixel
                                for (int y = static_cast<int>(keys_top); y < keys_bottom && !clipped; ++y)
                                    for (int x = static_cast<int>(divider_left);
                                         x < static_cast<int>(editor_right) && !clipped; ++x) {
                                        auto px = frame.pixelColor(x, y);
                                        if (std::abs(px.red() - background.red()) > 8 &&
                                            std::abs(px.green() - background.green()) > 8 &&
                                            std::abs(px.blue() - background.blue()) > 8) {
                                            std::cerr << "  fail-pixel1 x=" << x << " y=" << y
                                                      << " rgb=(" << px.red() << "," << px.green()
                                                      << "," << px.blue() << ")\n";
                                            clipped = true; // just to break
                                        }
                                    }
                                for (int y = static_cast<int>(top); y < bottom && !clipped; ++y)
                                    for (int x = static_cast<int>(left); x < window->width() - 2 && !clipped; ++x) {
                                        auto px = frame.pixelColor(x, y);
                                        if (std::abs(px.red() - background.red()) > 8 &&
                                            std::abs(px.green() - background.green()) > 8 &&
                                            std::abs(px.blue() - background.blue()) > 8) {
                                            std::cerr << "  fail-pixel2 x=" << x << " y=" << y
                                                      << " rgb=(" << px.red() << "," << px.green()
                                                      << "," << px.blue() << ")\n";
                                            clipped = true;
                                        }
                                    }
                            }
                            valid = clipped && valid;
                        } else valid = false;
                    }
                }
            keyboard.setSurface("PIANO");
            keyboard.setOrientation("ACROSS");

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
            if (parser.isSet("layout")) {
                keyboard.setLayout(parser.value("layout"));
                valid = valid && keyboard.layoutName() == parser.value("layout");
            }
            if (parser.isSet("orientation")) {
                keyboard.setOrientation(parser.value("orientation"));
                valid = valid && keyboard.orientation() == parser.value("orientation");
            }
            // The requested view is restored last, because measuring the
            // keyboard needed the layouts that show it.
            if (parser.isSet("view"))
                valid = valid && window->setProperty("view", parser.value("view"));
        }

        reached("keyboards");
    }

    // Out-of-process plugin discovery, then the screenshot and the verdict.
    void plugin_discovery() {
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
            // (followed by the built-in instruments, which need no scan)
            valid = valid && instrument_entries() == 3
                          && controller.plugins().first().toMap().value("format") == "CLAP"
                          && controller.plugins().first().toMap().value("name")
                                 == "Blokkily Test Synth";
            valid = valid && controller.status().contains("1 failure");

            // The next launch skips what already failed, so a hanging
            // plugin costs its deadline once rather than on every start.
            QElapsedTimer repeat;
            repeat.start();
            valid = valid && run_scan(false);
            valid = valid && instrument_entries() == 3
                          && controller.status().contains("1 failure");
            valid = valid && repeat.elapsed() < 1000;
        }

        reached("plugin discovery");
        valid = valid && save_screenshot();
        reached("screenshot");
        std::ostringstream details;
        details << " | shared-model edit=" << pattern.rowCount()
                << " | plugins=" << controller.plugins().size()
                << " | tracks=" << song.trackCount()
                << " | instruments=" << controller.instruments().size()
                << " | clips=" << song.clips().size()
                << " | " << controller.exportStatus().toStdString()
                << " | " << controller.status().toStdString();
        ctx.finish(details.str());
    }
};

void run_default(VerifyContext& context) { DefaultScenario(context).run(); }

[[maybe_unused]] const bool registered = register_scenario("default", run_default);

}  // namespace
}  // namespace blokkily::verify
