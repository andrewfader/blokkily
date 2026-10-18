// features/score_view.feature, run on its own by the bdd_score gate
// (`--verify --scenario score --view SCORE`). Notes are written through the
// pattern model the way the editors write them; the clef and key are chosen
// from the real pickers and CHANT is clicked. What is checked is what the
// view draws - each note's staff position, accidental, stem and spacing from
// scoreModel's layout - and the song it reads its settings from: saved,
// reloaded and undone with the song. Screenshots of the rendered staff are
// written beside the gate's own.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <iostream>
#include <source_location>
#include <utility>

namespace blokkily::verify {
namespace {

QObject* score_model(VerifyContext& ctx) {
    auto* engine = qmlEngine(ctx.window);
    if (engine == nullptr) return nullptr;
    return engine->rootContext()->contextProperty("scoreModel").value<QObject*>();
}

void run_score(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    // Honor --view like the default scenario does, so the gate leaves the
    // interface in the SCORE view and the screenshot shows it full-size.
    if (!ctx.parser.value("view").isEmpty()) (void)window->setProperty("view", ctx.parser.value("view"));
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "score check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto lay_out = [window] {
        QCoreApplication::processEvents();
        (void)window->grabWindow();
    };
    const auto settle = [&lay_out] {
        VerifyContext::settle(300);
        lay_out();
    };
    const auto shot = [&](const char* suffix) {
        const QString path = ctx.parser.value("screenshot");
        if (!path.isEmpty()) check(ctx.save_screenshot_to(path.left(path.size() - 4) + "-" + suffix + ".png"));
    };

    auto* const score = score_model(ctx);
    check(score != nullptr);
    if (score == nullptr) {
        ctx.finish(" no scoreModel in the window");
        return;
    }
    const auto prop = [score](const char* name) { return score->property(name); };
    // The notes as drawn: staff position, whether an accidental prints, and
    // which accidental, in drawing order.
    const auto notes = [&] {
        QVariantList drawn;
        for (const auto& item : prop("objects").toList())
            if (item.toMap().value("kind").toString() == "note") drawn.push_back(item);
        return drawn;
    };
    const auto positions = [&] {
        QList<int> out;
        for (const auto& note : notes()) out << note.toMap().value("pos").toInt();
        return out;
    };
    const auto settings = [&] { return song.notationSettings(song.selectedTrack(), song.currentSection()); };
    // Every step of the open part emptied, as CLEAR would.
    const auto clear = [&] {
        const auto& open = song.editPattern();
        for (int step = 0; step < static_cast<int>(open.length() / 120); ++step) pattern.clearStep(step);
    };

    auto* const view = ctx.named("scoreView");
    auto* const canvas = ctx.named("scoreCanvas");
    auto* const scroller = ctx.named("scoreScroller");

    // ---------------------------------------------------------------------
    ctx.reached("score: the SCORE view draws the pattern's notes as notation");
    settle();
    check(view != nullptr && view->isVisible());
    // Shown is not the same as usable: a panel squeezed to nothing by a
    // neighbour still reports itself visible.
    check(VerifyContext::usable(view, 400, 200));
    check(canvas != nullptr && scroller != nullptr && VerifyContext::usable(scroller, 300, 120));
    check(view != nullptr && view->property("fontOk").toBool()); // the SMuFL font is the feature
    clear();
    pattern.setStepKey(0, 60);
    pattern.setStepKey(4, 64);
    pattern.setStepKey(8, 67);
    settle();
    // Treble: C4 hangs a step below the bottom line, E4 is on it, G4 on the
    // second line; each a sixteenth, with rests between, in one bar.
    check((positions() == QList<int>{-2, 0, 2}));
    {
        const auto drawn = notes();
        bool sixteenths = drawn.size() == 3;
        for (const auto& note : drawn) sixteenths = sixteenths && note.toMap().value("value").toInt() == 4;
        check(sixteenths); // the step grid's short gate is drawn to the end of its step
        check(drawn.size() == 3 && drawn[0].toMap().value("x").toDouble() < drawn[1].toMap().value("x").toDouble() &&
              drawn[1].toMap().value("x").toDouble() < drawn[2].toMap().value("x").toDouble());
        check(drawn.size() == 3 && drawn[0].toMap().value("ledger").toInt() == 1);
        check(prop("objectCount").toInt() > 3); // rests fill the bar
        const auto bars = prop("barlines").toList();
        check(bars.size() == 1 && bars.front().toMap().value("final").toBool());
    }
    check(prop("clefName").toString() == "Treble");
    settle();
    check(ctx.save_screenshot());

    // ---------------------------------------------------------------------
    ctx.reached("score: the clef picker reaches the obscure clefs");
    auto* const clef_picker = ctx.named("clefPicker");
    auto* const clef_menu = window->findChild<QObject*>("clefPickerMenu");
    check(clef_picker != nullptr && clef_menu != nullptr && clef_menu->property("count").toInt() == 11);
    if (clef_picker != nullptr)
        QMetaObject::invokeMethod(clef_picker, "picked", Q_ARG(QString, QStringLiteral("French violin")));
    settle();
    check(prop("clefName").toString() == "French violin");
    check(settings().value("clef").toString() == "French violin"); // the song's, not the view's
    // Under French violin the bottom line is G4: everything sits two steps lower.
    check((positions() == QList<int>{-4, -2, 0}));
    check(prop("clefRefPos").toInt() == 0);
    shot("french-violin");
    // The clef is the song's: saved with it, back after a reload, undone with it.
    {
        const QTemporaryDir folder;
        const QString project = folder.filePath("clef.blok");
        check(ctx.controller.saveProject(project));
        if (clef_picker != nullptr)
            QMetaObject::invokeMethod(clef_picker, "picked", Q_ARG(QString, QStringLiteral("Bass")));
        settle();
        check(prop("clefName").toString() == "Bass");
        check(ctx.controller.loadProject(project));
        settle();
        check(prop("clefName").toString() == "French violin");
        if (clef_picker != nullptr)
            QMetaObject::invokeMethod(clef_picker, "picked", Q_ARG(QString, QStringLiteral("Alto")));
        settle();
        check(prop("clefName").toString() == "Alto");
        check(song.undo());
        settle();
        check(prop("clefName").toString() == "French violin");
    }
    if (clef_picker != nullptr)
        QMetaObject::invokeMethod(clef_picker, "picked", Q_ARG(QString, QStringLiteral("Treble")));
    settle();
    check(prop("clefName").toString() == "Treble");

    // ---------------------------------------------------------------------
    ctx.reached("score: CHANT draws an unmetered staff");
    auto* const chant = ctx.named("chantChip");
    check(chant != nullptr && VerifyContext::usable(chant, 30, 16));
    if (chant != nullptr) ctx.click_at(chant, {chant->width() / 2, chant->height() / 2}, Qt::LeftButton);
    settle();
    check(prop("unmetered").toBool() && settings().value("unmetered").toBool());
    check(prop("barlines").toList().isEmpty()); // no barlines, not even a closing one
    {
        bool bare = !notes().isEmpty();
        for (const auto& note : notes())
            bare = bare && note.toMap().value("stemless").toBool() && !note.toMap().value("stem").toBool();
        check(bare);
    }
    shot("chant");
    if (chant != nullptr) ctx.click_at(chant, {chant->width() / 2, chant->height() / 2}, Qt::LeftButton);
    settle();
    check(!prop("unmetered").toBool() && prop("timeBeats").toInt() == 4 && prop("timeBeatUnit").toInt() == 4);

    // ---------------------------------------------------------------------
    ctx.reached("score: the key picker re-spells accidentals");
    auto* const key_picker = ctx.named("keyPicker");
    check(key_picker != nullptr);
    pattern.setStepKey(12, 65); // F4: natural against A major's F#
    if (key_picker != nullptr) QMetaObject::invokeMethod(key_picker, "picked", Q_ARG(QString, QStringLiteral("+3")));
    settle();
    check(prop("keyFifths").toInt() == 3 && prop("keyLabel").toString() == "A major");
    {
        // F#, C#, G# on the top line, the third space and above the staff.
        QList<int> placed;
        for (const auto& accidental : prop("keySignature").toList()) placed << accidental.toMap().value("pos").toInt();
        check((placed == QList<int>{8, 5, 9}));
        const auto drawn = notes();
        check(drawn.size() == 4 && drawn[3].toMap().value("showAcc").toBool() &&
              drawn[3].toMap().value("acc").toInt() == 0); // a natural sign
        check(drawn.size() == 4 && !drawn[1].toMap().value("showAcc").toBool());
    }
    shot("three-sharps");
    if (key_picker != nullptr) QMetaObject::invokeMethod(key_picker, "picked", Q_ARG(QString, QStringLiteral("0")));
    settle();
    check(prop("keySignature").toList().isEmpty());

    // ---------------------------------------------------------------------
    ctx.reached("score: chords, held notes and long parts are drawn whole");
    clear();
    check(song.setPatternSteps(128)); // eight bars
    pattern.addRollNote(0, 60);       // a C major triad on the first beat...
    pattern.addRollNote(0, 64);
    pattern.addRollNote(0, 67);
    for (const int key : {60, 64, 67}) pattern.resizeRollNote(0, key, 480);
    pattern.addRollNote(16, 48);      // ...a C3 held through bar two under...
    pattern.resizeRollNote(16, 48, 1920);
    for (int beat = 0; beat < 4; ++beat) pattern.addRollNote(16 + beat * 4, 84); // ...C6 quarters
    for (int beat = 0; beat < 4; ++beat) pattern.resizeRollNote(16 + beat * 4, 84, 480);
    settle();
    check(prop("measureCount").toInt() == 8);
    {
        const auto drawn = notes();
        int triad_stems = 0;
        int held_stem_down = 0;
        int high_stems_up = 0;
        for (const auto& item : drawn) {
            const auto note = item.toMap();
            if (note.value("x").toDouble() == drawn.front().toMap().value("x").toDouble())
                triad_stems += note.value("stem").toBool() ? 1 : 0;
            if (note.value("pos").toInt() < -4 && !note.value("stemUp").toBool()) ++held_stem_down;
            if (note.value("pos").toInt() > 10 && note.value("stemUp").toBool()) ++high_stems_up;
        }
        check(triad_stems == 1);   // a chord has one stem
        check(held_stem_down == 1 && high_stems_up == 4); // two voices: stems apart
    }
    // The staff fits what it draws, and scrolls sideways rather than cutting
    // the part off.
    if (canvas != nullptr && scroller != nullptr) {
        const double sp = canvas->property("sp").toDouble();
        const double bottom = canvas->property("yBottom").toDouble();
        check(bottom - prop("highestPos").toInt() * sp / 2 >= 0.0);
        check(bottom - prop("lowestPos").toInt() * sp / 2 <= canvas->height());
        check(sp >= 6.0);
        check(scroller->property("contentWidth").toDouble() > scroller->width());
        scroller->setProperty("contentX", scroller->property("contentWidth").toDouble() - scroller->width());
    }
    settle();
    // The canvas is the visible window onto the part, never the part's whole
    // width (a long part drawn whole outgrew what a GPU texture can hold),
    // and it follows the scroll.
    if (canvas != nullptr && scroller != nullptr) {
        check(canvas->width() <= scroller->width() + 0.5);
        check(std::abs(canvas->x() - scroller->property("contentX").toDouble()) < 0.5 && canvas->x() > 0.0);
    }
    shot("long-part");

    // ---------------------------------------------------------------------
    ctx.reached("score: a tuplet is bracketed over the notes it counts");
    if (scroller != nullptr) scroller->setProperty("contentX", 0.0);
    clear();
    check(song.setPatternSteps(16)); // one bar
    // Two groups of eighth-note triplets, the second climbing above the
    // staff, written into the part the way a recorded take or an import
    // writes them (steps hold sixteenths, not triplets).
    {
        const int triplet_keys[] = {67, 71, 74, 79, 84, 88};
        auto& part = song.editPattern();
        for (int index = 0; index < 6; ++index) {
            Trigger note;
            note.start = 160 * index;
            note.duration = 160;
            note.musical_data = Note{static_cast<std::int16_t>(triplet_keys[index]), 0.8F, 0.0F};
            (void)part.add(note);
        }
        pattern.notifyRecorded();
    }
    settle();
    {
        const auto drawn = notes();
        int groups = 0;
        bool clear_of_members = drawn.size() == 6;
        for (int first = 0; first < drawn.size(); ++first) {
            const auto opener = drawn[first].toMap();
            if (opener.value("tuplet").toInt() != 3) continue;
            ++groups;
            const double from = opener.value("x").toDouble();
            const double to = opener.value("tupletEndX").toDouble();
            const int bracket = opener.value("tupletPos").toInt();
            int members = 0;
            for (const auto& item : drawn) {
                const auto note = item.toMap();
                const double x = note.value("x").toDouble();
                if (x < from - 1e-9 || x > to + 1e-9) continue;
                ++members;
                const int top = note.value("stem").toBool() && note.value("stemUp").toBool()
                                    ? note.value("stemTo").toInt() : note.value("pos").toInt();
                clear_of_members = clear_of_members && bracket > top && bracket > 8;
            }
            check(to > from && members == 3);
        }
        check(groups == 2 && clear_of_members);
        if (!(groups == 2 && clear_of_members))
            for (const auto& item : drawn) {
                const auto note = item.toMap();
                std::cerr << "triplets: x " << note.value("x").toDouble() << " pos " << note.value("pos").toInt()
                          << " value " << note.value("value").toInt() << " tuplet " << note.value("tuplet").toInt()
                          << " end " << note.value("tupletEndX").toDouble() << " bracket "
                          << note.value("tupletPos").toInt() << " stemTo " << note.value("stemTo").toInt() << '\n';
            }
    }
    shot("triplets");

    // ---------------------------------------------------------------------
    ctx.reached("score: eighths and shorter are beamed by the beat");
    clear();
    {
        // Beat 1: two eighths. Beat 2: four sixteenths. Beat 3: an eighth
        // and two sixteenths. Beat 4: a quarter.
        const int steps[] = {0, 2, 4, 5, 6, 7, 8, 10, 11, 12};
        const int lengths[] = {240, 240, 120, 120, 120, 120, 240, 120, 120, 480};
        const int keys[] = {72, 74, 72, 74, 76, 77, 79, 77, 76, 74};
        for (int index = 0; index < 10; ++index) {
            pattern.addRollNote(steps[index], keys[index]);
            pattern.resizeRollNote(steps[index], keys[index], lengths[index]);
        }
    }
    settle();
    {
        const auto beams = prop("beams").toList();
        int main_beams = 0, second_beams = 0, hooks = 0;
        for (const auto& item : beams) {
            const auto beam = item.toMap();
            if (beam.value("level").toInt() == 0) ++main_beams;
            if (beam.value("level").toInt() == 1 && beam.value("hook").toInt() == 0) ++second_beams;
            if (beam.value("hook").toInt() != 0) ++hooks;
        }
        check(main_beams == 3 && second_beams == 2 && hooks == 0);
        const auto drawn = notes();
        check(drawn.size() == 10);
        bool flags_right = drawn.size() == 10;
        for (int index = 0; index < drawn.size(); ++index)
            flags_right = flags_right && drawn[index].toMap().value("beamed").toBool() == (index < 9);
        check(flags_right);
        // A group's stems point one way and end on its beam.
        bool stems_meet = true;
        for (const auto& item : beams) {
            const auto beam = item.toMap();
            if (beam.value("level").toInt() != 0) continue;
            for (const auto& note_item : drawn) {
                const auto note = note_item.toMap();
                const double x = note.value("x").toDouble();
                if (x < beam.value("fromX").toDouble() - 1e-9 || x > beam.value("toX").toDouble() + 1e-9) continue;
                stems_meet = stems_meet && note.value("stemUp").toBool() == beam.value("up").toBool() &&
                             note.value("stemTo").toInt() == beam.value("pos").toInt();
            }
        }
        check(stems_meet);
    }
    shot("beams");

    // ---------------------------------------------------------------------
    ctx.reached("score: notes are written and edited on the staff");
    if (scroller != nullptr) scroller->setProperty("contentX", 0.0);
    clear();
    check(song.setPatternSteps(16)); // one bar
    settle();
    // A press on the staff where the model says tick `tick` and staff
    // position `pos` are, with the real mouse.
    const auto press_staff = [&](int tick, int pos) {
        if (canvas == nullptr) return;
        const double sp = canvas->property("sp").toDouble();
        double units = 0.0;
        QMetaObject::invokeMethod(score, "xForTick", Q_RETURN_ARG(double, units), Q_ARG(int, tick));
        const double x = canvas->property("headerWidth").toDouble() + (units + 0.59) * sp;
        const double y = canvas->property("yBottom").toDouble() - pos * sp / 2;
        auto* input = ctx.named("staffInput");
        check(input != nullptr);
        // The input spans the whole part, so (x, y) are its own coordinates.
        if (input != nullptr) ctx.click_at(input, QPointF(x, y), Qt::LeftButton);
        settle();
    };
    const auto press_chip = [&](const char* name) {
        auto* chip = ctx.named(QString::fromLatin1(name));
        check(VerifyContext::usable(chip, 16, 14));
        if (chip != nullptr) ctx.click_at(chip, {chip->width() / 2, chip->height() / 2}, Qt::LeftButton);
        settle();
    };
    const auto selected = [&] { return std::make_pair(prop("selectedStep").toInt(), prop("selectedKey").toInt()); };
    check(VerifyContext::usable(ctx.named("staffInput"), 300, 100));
    press_chip("noteValue4");
    check(prop("noteValue").toInt() == 4);
    // A quarter-note G4 on beat two: the second line of the treble staff.
    press_staff(480, 2);
    check(pattern.rollNoteDuration(4, 67) == 480);
    check((selected() == std::make_pair(4, 67)));
    // One click is one step of undo.
    check(song.undo());
    settle();
    check(pattern.rollNoteDuration(4, 67) == 0 && prop("selectedStep").toInt() == -1);
    check(song.redo());
    settle();
    check(pattern.rollNoteDuration(4, 67) == 480);
    press_staff(480, 2); // its notehead: selected again, nothing added
    check((selected() == std::make_pair(4, 67)) && pattern.rollNoteDuration(4, 67) == 480);
    // The arrows move it a line or space, shift an octave; the sharp chip
    // raises it a semitone.
    ctx.chord_key(Qt::Key_Up, Qt::NoModifier);
    settle();
    check(pattern.rollNoteDuration(4, 69) == 480 && pattern.rollNoteDuration(4, 67) == 0);
    ctx.chord_key(Qt::Key_Up, Qt::ShiftModifier);
    settle();
    check(pattern.rollNoteDuration(4, 81) == 480 && (selected() == std::make_pair(4, 81)));
    press_chip("noteSharpen");
    check(pattern.rollNoteDuration(4, 82) == 480);
    {
        bool drawn_selected = false;
        for (const auto& item : notes())
            drawn_selected = drawn_selected || (item.toMap().value("selected").toBool() &&
                                                item.toMap().value("key").toInt() == 82 &&
                                                item.toMap().value("showAcc").toBool());
        check(drawn_selected); // drawn with its sharp, in the accent colour
    }
    // A click below it at the same time writes a chord tone: C4 under it.
    press_staff(480, -2);
    check(pattern.rollNoteDuration(4, 60) == 480 && pattern.rollNoteDuration(4, 82) == 480);
    check((selected() == std::make_pair(4, 60)));
    // Delete takes the selected note and leaves its chord-mate.
    ctx.chord_key(Qt::Key_Delete, Qt::NoModifier);
    settle();
    check(pattern.rollNoteDuration(4, 60) == 0 && pattern.rollNoteDuration(4, 82) == 480);
    // A dotted half on the downbeat, on the middle line: B4.
    press_chip("noteValue2");
    press_chip("noteDotted");
    press_staff(0, 4);
    check(pattern.rollNoteDuration(0, 71) == 1440);
    {
        bool dotted_half = false;
        for (const auto& item : notes())
            dotted_half = dotted_half || (item.toMap().value("key").toInt() == 71 &&
                                          item.toMap().value("value").toInt() == 1 &&
                                          item.toMap().value("dots").toInt() == 1);
        check(dotted_half);
    }
    press_chip("noteDotted");
    // The key signature gives the written note its accidental: F5 in D major
    // is F#5.
    if (key_picker != nullptr) QMetaObject::invokeMethod(key_picker, "picked", Q_ARG(QString, QStringLiteral("+2")));
    settle();
    press_chip("noteValue4");
    press_staff(1440, 8);
    check(pattern.rollNoteDuration(12, 78) == 480);
    // Escape lets the note go.
    ctx.chord_key(Qt::Key_Escape, Qt::NoModifier);
    settle();
    check(prop("selectedStep").toInt() == -1);
    // Every editor reads the same pattern: the piano roll's lookups see the
    // notes the staff wrote.
    check(pattern.stepKey(12) == 78 && pattern.hasStep(0) && pattern.hasStep(4));
    press_staff(1440, 8); // selected again, for the record
    shot("entry");
    if (key_picker != nullptr) QMetaObject::invokeMethod(key_picker, "picked", Q_ARG(QString, QStringLiteral("0")));
    settle();

    // ---------------------------------------------------------------------
    ctx.reached("score: a hidden score is laid out when it is shown again");
    const auto drawn_key = [&](int key) {
        for (const auto& item : notes())
            if (item.toMap().value("key").toInt() == key) return true;
        return false;
    };
    check(window->setProperty("view", "ROLL"));
    settle();
    check(!prop("active").toBool());
    pattern.addRollNote(8, 72); // written in the piano roll while the score is away
    settle();
    check(!drawn_key(72));      // not laid out for nobody
    check(window->setProperty("view", "SCORE"));
    settle();
    check(prop("active").toBool() && drawn_key(72)); // and up to date when it comes back
    pattern.eraseRollNote(8, 72);
    settle();
    check(!drawn_key(72));

    ctx.finish(" SCORE view, clefs saved and undone, chant, key signatures, chords and voices, note entry");
}

[[maybe_unused]] const bool registered = register_scenario("score", run_score);

}  // namespace
}  // namespace blokkily::verify
