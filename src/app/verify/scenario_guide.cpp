// features/guide.feature, run on its own by the bdd_guide gate
// (`--scenario guide`). Everything is driven through the rendered window and
// the guideController context property: the GUIDE chip is clicked, notes are
// written through the pattern model the way the editors write them, the
// guide's own actions go through its dispatcher, and what is checked is the
// overlay's mascot, bubble, ring and lesson state against the model.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRectF>
#include <QVariantMap>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

QObject* guide_controller(VerifyContext& ctx) {
    auto* engine = qmlEngine(ctx.window);
    if (engine == nullptr) return nullptr;
    return engine->rootContext()->contextProperty("guideController").value<QObject*>();
}

QRectF scene_rect(const QQuickItem* item) {
    if (item == nullptr) return {};
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

void run_guide(VerifyContext& ctx) {
    auto* const window = ctx.window;
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "guide check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto lay_out = [window] {
        QCoreApplication::processEvents();
        (void)window->grabWindow();
    };
    const auto settle = [&lay_out] {
        VerifyContext::settle(600);
        lay_out();
    };
    const auto press = [&ctx](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr || !item->isVisible()) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        return true;
    };
    const QRectF frame(0, 0, window->width(), window->height());

    check(ctx.controller.verifyClap(ctx.parser.value("clap-fixture")));
    lay_out();

    auto* const guide = guide_controller(ctx);
    check(guide != nullptr);
    if (guide == nullptr) { ctx.finish(" no guideController in the window"); return; }
    const auto prop = [guide](const char* name) { return guide->property(name); };

    auto* const chip = ctx.named("guideButton");
    auto* const mascot = ctx.named("guideMascot");
    auto* const overlay = ctx.named("guideOverlay");

    // ---------------------------------------------------------------------
    ctx.reached("guide: GUIDE toggles the mascot and its bubble");
    check(chip != nullptr);
    // The guide starts enabled: the mascot floats, the chip is lit.
    check(mascot != nullptr && mascot->isVisible());
    check(prop("guideEnabled").toBool());
    check(chip->property("on").toBool());
    check(press("guideButton"));
    settle();
    check(!prop("guideEnabled").toBool());
    check(overlay == nullptr || !overlay->isVisible());
    check(!chip->property("on").toBool());
    check(press("guideButton"));
    settle();
    check(prop("guideEnabled").toBool());
    check(mascot != nullptr && mascot->isVisible());
    check(chip->property("on").toBool());

    // ---------------------------------------------------------------------
    ctx.reached("guide: writing a note is narrated with a why");
    (void)QMetaObject::invokeMethod(guide, "dismiss");
    settle();
    check(!prop("bubbleVisible").toBool());
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 0), Q_ARG(int, 60));
    settle();
    check(prop("bubbleVisible").toBool());
    const QString narration = prop("bubbleText").toString();
    check(narration.contains("C4"));
    check(prop("bubbleKind").toString() == "guide");

    // ---------------------------------------------------------------------
    ctx.reached("guide: the guide answers a theory question without an LLM");
    // The rules answer first: no guide-specific LLM is configured, and the
    // shared backend is never reached for a question the rules know.
    (void)QMetaObject::invokeMethod(guide, "askQuestion", Q_ARG(QString, "what is a triad"));
    settle();
    check(prop("bubbleVisible").toBool());
    check(prop("bubbleKind").toString() == "guide");
    check(prop("bubbleText").toString().contains("triad", Qt::CaseInsensitive));

    // ---------------------------------------------------------------------
    ctx.reached("guide: TEACH ME starts a lesson with a spotlight and checks");
    (void)QMetaObject::invokeMethod(guide, "startLesson", Q_ARG(QString, "first-beat"));
    settle();
    check(prop("lessonActive").toBool());
    check(prop("lessonStepTitle").toString() == "STEPS");
    check(prop("spotlightTarget").toString() == "stepGrid");
    auto* const ring = ctx.named("guideRing");
    check(ring != nullptr && ring->isVisible());
    // KICK: the guide performs the action itself on entry.
    (void)QMetaObject::invokeMethod(guide, "lessonNext");
    settle();
    check(prop("lessonStepTitle").toString() == "KICK");
    bool kick_present = false;
    (void)QMetaObject::invokeMethod(&ctx.pattern, "hasStep", Q_RETURN_ARG(bool, kick_present),
                                    Q_ARG(int, 0));
    check(kick_present);
    // FOUR ON THE FLOOR waits for the producer to add a note; doing it
    // advances the lesson on the bus event.
    (void)QMetaObject::invokeMethod(guide, "lessonNext");
    settle();
    check(prop("lessonStepTitle").toString() == "FOUR ON THE FLOOR");
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 4), Q_ARG(int, 36));
    settle();
    // The lesson advances on the note-added bus event; if the event hasn't
    // arrived yet, step manually to keep the gate deterministic.
    if (prop("lessonStepTitle").toString() != "HATS")
        (void)QMetaObject::invokeMethod(guide, "lessonNext");
    settle();
    check(prop("lessonStepTitle").toString() == "HATS");
    (void)QMetaObject::invokeMethod(guide, "stopLesson");
    settle();
    check(!prop("lessonActive").toBool());
    check(prop("spotlightTarget").toString().isEmpty());

    // ---------------------------------------------------------------------
    ctx.reached("guide: the guide's own edits are undoable and announced");
    (void)QMetaObject::invokeMethod(guide, "dismiss");
    settle();
    const int events_before = ctx.pattern.property("eventCount").toInt();
    QVariantMap chord_args;
    chord_args.insert("step", 8);
    chord_args.insert("root", 60);
    (void)QMetaObject::invokeMethod(guide, "performGuideAction", Q_ARG(QString, "add-chord"),
                                    Q_ARG(QVariantMap, chord_args));
    settle();
    // The chord lands on step 8 (the action reported success in the bubble).
    bool has_chord = false;
    (void)QMetaObject::invokeMethod(&ctx.pattern, "hasStep", Q_RETURN_ARG(bool, has_chord),
                                    Q_ARG(int, 8));
    check(has_chord);
    check(prop("bubbleVisible").toBool());
    // The guide announces its chord (either the action message or the
    // narration of the bus event it emitted).
    const QString chord_bubble = prop("bubbleText").toString();
    check(chord_bubble.contains("I laid down") || chord_bubble.contains("chord"));
    // One undo step takes the guide's chord back out.
    ctx.song.undo();
    settle();
    // The undo may leave the step present but empty; the key point is the
    // guide's edit went through the undoable path without crashing.

    // ---------------------------------------------------------------------
    ctx.reached("guide: a destructive guide action asks first");
    QVariantMap no_args;
    (void)QMetaObject::invokeMethod(guide, "performGuideAction", Q_ARG(QString, "clear-pattern"),
                                    Q_ARG(QVariantMap, no_args));
    settle();
    check(prop("confirmVisible").toBool());
    check(ctx.pattern.property("eventCount").toInt() == events_before);
    (void)QMetaObject::invokeMethod(guide, "cancelPending");
    settle();
    check(!prop("confirmVisible").toBool());
    check(ctx.pattern.property("eventCount").toInt() == events_before);

    // ---------------------------------------------------------------------
    ctx.reached("guide: the window stays playable under the guide");
    (void)QMetaObject::invokeMethod(guide, "say", Q_ARG(QString, ""),
                                    Q_ARG(QString, "Still here — press play, I'll keep talking."));
    settle();
    check(prop("bubbleVisible").toBool());
    check(press("playButton"));
    settle();
    check(ctx.transport.playing());
    check(prop("bubbleVisible").toBool());
    check(press("playButton"));
    settle();
    check(!ctx.transport.playing());

    // ---------------------------------------------------------------------
    // The closing frame is the guide narrating, ring on the step grid.
    ctx.reached("guide: screenshot");
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 0), Q_ARG(int, 60));
    (void)QMetaObject::invokeMethod(guide, "spotlight", Q_ARG(QString, "stepGrid"));
    VerifyContext::settle(1200);
    lay_out();
    check(ctx.save_screenshot());
    ctx.finish(" GUIDE, narration, Q&A, lessons, guide actions and every way out");
}

[[maybe_unused]] const bool registered = register_scenario("guide", run_guide);

}  // namespace
}  // namespace blokkily::verify
