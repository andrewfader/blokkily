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

#include <iostream>
#include <source_location>

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
    check(song.setPatternSteps(64)); // four bars
    pattern.addRollNote(0, 60);       // a C major triad on the first beat...
    pattern.addRollNote(0, 64);
    pattern.addRollNote(0, 67);
    for (const int key : {60, 64, 67}) pattern.resizeRollNote(0, key, 480);
    pattern.addRollNote(16, 48);      // ...a C3 held through bar two under...
    pattern.resizeRollNote(16, 48, 1920);
    for (int beat = 0; beat < 4; ++beat) pattern.addRollNote(16 + beat * 4, 84); // ...C6 quarters
    for (int beat = 0; beat < 4; ++beat) pattern.resizeRollNote(16 + beat * 4, 84, 480);
    settle();
    check(prop("measureCount").toInt() == 4);
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
    shot("long-part");

    ctx.finish(" SCORE view, clefs saved and undone, chant, key signatures, chords and voices");
}

[[maybe_unused]] const bool registered = register_scenario("score", run_score);

}  // namespace
}  // namespace blokkily::verify
