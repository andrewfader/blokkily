// features/guide.feature, run on its own by the bdd_guide gate
// (`--scenario guide`). Everything is driven through the rendered window and
// the guideController context property: the GUIDE chip is clicked, notes are
// written through the pattern model the way the editors write them, the
// guide's own actions go through its dispatcher, and what is checked is the
// overlay's mascot, bubble, ring and lesson state against the model.

#include "verify/harness.hpp"

#include "blokkily/guide/event_bus.hpp"

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
    // An empty pattern to start from, whatever the fixture left in it.
    for (int step = 0; step < ctx.pattern.stepCount(); ++step) ctx.pattern.clearStep(step);
    lay_out();

    auto* const guide = guide_controller(ctx);
    check(guide != nullptr);
    if (guide == nullptr) {
        ctx.finish(" no guideController in the window");
        return;
    }
    const auto prop = [guide](const char* name) { return guide->property(name); };
    const auto has_step = [&ctx](int step) {
        bool present = false;
        (void)QMetaObject::invokeMethod(&ctx.pattern, "hasStep", Q_RETURN_ARG(bool, present), Q_ARG(int, step));
        return present;
    };
    // The narration feed's last event: who did it, and what.
    // Who did the latest edit: the last note event on the narration feed,
    // with the narration it got.
    const auto last_event = [&ctx] {
        const auto tail = ctx.controller.eventBus().tail(16);
        for (auto it = tail.rbegin(); it != tail.rend(); ++it)
            if (it->kind == blokkily::guide::EventKind::note_added || it->kind == blokkily::guide::EventKind::guide_action)
                return QVariantMap{{"source", QString::fromStdString(blokkily::guide::to_string(it->source))},
                                   {"kind", QString::fromStdString(blokkily::guide::to_string(it->kind))}};
        return QVariantMap{};
    };
    const auto contains = [](const QRectF& outer, const QRectF& inner) {
        return outer.adjusted(-0.5, -0.5, 0.5, 0.5).contains(inner);
    };

    auto* const chip = ctx.named("guideButton");
    auto* const mascot = ctx.named("guideMascot");
    auto* const overlay = ctx.named("guideOverlay");

    // ---------------------------------------------------------------------
    ctx.reached("guide: GUIDE toggles the mascot and its bubble");
    // A verify run never reads the user's settings: the guide starts off,
    // drawing nothing.
    check(chip != nullptr && VerifyContext::usable(chip, 40, 16) && contains(frame, scene_rect(chip)));
    check(!prop("guideEnabled").toBool() && !chip->property("on").toBool());
    check(overlay == nullptr || !overlay->isVisible());
    // GUIDE turns it on and opens its bubble, with the mascot beside it.
    check(press("guideButton"));
    settle();
    check(prop("guideEnabled").toBool() && chip->property("on").toBool() && prop("bubbleVisible").toBool());
    check(mascot != nullptr && mascot->isVisible() && VerifyContext::usable(mascot, 40, 40) &&
          contains(frame, scene_rect(mascot)));
    // Closed, it draws nothing that takes a click until it talks again.
    check(press("guideButton"));
    settle();
    check(prop("guideEnabled").toBool() && !prop("bubbleVisible").toBool());
    check(mascot != nullptr && !mascot->isVisible());
    // Turned off from its bubble, it is gone.
    check(press("guideButton"));
    settle();
    check(press("guideOffButton"));
    settle();
    check(!prop("guideEnabled").toBool() && !chip->property("on").toBool());
    check(overlay == nullptr || !overlay->isVisible());
    check(press("guideButton"));
    settle();
    check(prop("guideEnabled").toBool());

    // ---------------------------------------------------------------------
    ctx.reached("guide: writing a note is narrated with a why");
    (void)QMetaObject::invokeMethod(guide, "dismiss");
    settle();
    check(!prop("bubbleVisible").toBool());
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 0), Q_ARG(int, 60));
    settle();
    check(prop("bubbleVisible").toBool());
    const QString narration = prop("bubbleText").toString();
    // Steps are named as the grid numbers them, from 1.
    check(narration.startsWith("You added C4 at step 1.") && narration.contains("loop"));
    check(prop("bubbleKind").toString() == "guide");
    check(last_event().value("source").toString() == "user");

    // ---------------------------------------------------------------------
    ctx.reached("guide: the guide answers a theory question without an LLM");
    // Asking a model is off until the producer switches it on.
    check(!prop("askModel").toBool() && !prop("llmEnabled").toBool());
    (void)QMetaObject::invokeMethod(guide, "askQuestion", Q_ARG(QString, "what is a triad"));
    settle();
    check(prop("bubbleVisible").toBool() && prop("bubbleKind").toString() == "guide");
    check(prop("bubbleText").toString().contains("triad", Qt::CaseInsensitive));
    // A question the rules miss goes nowhere without the producer's say-so.
    (void)QMetaObject::invokeMethod(guide, "askQuestion", Q_ARG(QString, "who wrote the first fugue"));
    settle();
    check(!prop("asking").toBool() && prop("bubbleText").toString().contains("ASK MODEL"));

    // ---------------------------------------------------------------------
    ctx.reached("guide: TEACH ME starts a lesson with a spotlight and checks");
    if (!prop("bubbleVisible").toBool()) check(press("guideButton"));
    settle();
    check(press("guideTeachButton"));
    settle();
    check(press("guideLesson-first-beat"));
    settle();
    check(prop("lessonActive").toBool() && prop("lessonStepTitle").toString() == "STEPS");
    check(prop("spotlightTarget").toString() == "stepGrid");
    auto* const ring = ctx.named("guideRing");
    auto* const grid = ctx.named("stepGrid");
    check(ring != nullptr && ring->isVisible() && grid != nullptr && contains(scene_rect(ring), scene_rect(grid)));
    // KICK: the guide would write C2 on step 1, but the producer's C4 is
    // there - it leaves it as it is and says so.
    check(press("guideLessonNext"));
    settle();
    check(prop("lessonStepTitle").toString() == "KICK");
    check(ctx.pattern.stepKey(0) == 60 && prop("bubbleText").toString().contains("left it alone"));
    // Back and forward again does not run the step's action twice.
    const int before_revisit = ctx.pattern.property("eventCount").toInt();
    check(press("guideLessonBack"));
    settle();
    check(press("guideLessonNext"));
    settle();
    check(ctx.pattern.property("eventCount").toInt() == before_revisit);
    // FOUR ON THE FLOOR asks for three kicks: one counts, the step's words
    // stay on screen, and the third moves the lesson on by itself.
    check(press("guideLessonNext"));
    settle();
    check(prop("lessonStepTitle").toString() == "FOUR ON THE FLOOR");
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 4), Q_ARG(int, 36));
    settle();
    check(prop("lessonStepTitle").toString() == "FOUR ON THE FLOOR" && prop("lessonProgress").toString() == "1 of 3");
    check(prop("bubbleText").toString().contains("steps 5, 9 and 13"));
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 8), Q_ARG(int, 36));
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 12), Q_ARG(int, 36));
    settle();
    check(prop("lessonStepTitle").toString() == "HATS");
    for (const int step : {2, 6, 10, 14}) (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, step), Q_ARG(int, 78));
    settle();
    // PLAY: the ring follows its new target.
    check(prop("lessonStepTitle").toString() == "PLAY" && prop("spotlightTarget").toString() == "playButton");
    auto* const play = ctx.named("playButton");
    check(play != nullptr && contains(scene_rect(ring), scene_rect(play)));
    {
        const QString shot = ctx.parser.value("screenshot");
        if (!shot.isEmpty()) check(ctx.save_screenshot_to(shot.left(shot.size() - 4) + "-lesson.png"));
    }
    check(press("guideLessonEnd"));
    settle();
    check(!prop("lessonActive").toBool() && prop("spotlightTarget").toString().isEmpty());

    // ---------------------------------------------------------------------
    ctx.reached("guide: the guide's own edits are undoable and announced");
    (void)QMetaObject::invokeMethod(guide, "dismiss");
    settle();
    const int before_chord = ctx.pattern.property("eventCount").toInt();
    QVariantMap chord_args;
    chord_args.insert("step", 1);
    chord_args.insert("root", 60);
    (void)QMetaObject::invokeMethod(guide, "performGuideAction", Q_ARG(QString, "add-chord"),
                                    Q_ARG(QVariantMap, chord_args));
    settle();
    check(has_step(1) && ctx.pattern.property("eventCount").toInt() == before_chord + 1);
    check(prop("bubbleVisible").toBool() && prop("bubbleText").toString().contains("I laid down"));
    check(last_event().value("source").toString() == "guide");
    // One undo takes it back, and the producer's next edit is theirs again.
    check(ctx.song.undo());
    settle();
    check(!has_step(1) && ctx.pattern.property("eventCount").toInt() == before_chord);
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 3), Q_ARG(int, 64));
    settle();
    check(last_event().value("source").toString() == "user");

    // ---------------------------------------------------------------------
    ctx.reached("guide: a destructive guide action asks first");
    const int events_before = ctx.pattern.property("eventCount").toInt();
    check(events_before > 1);
    (void)QMetaObject::invokeMethod(guide, "performGuideAction", Q_ARG(QString, "clear-pattern"),
                                    Q_ARG(QVariantMap, QVariantMap{}));
    settle();
    check(prop("confirmVisible").toBool() && ctx.pattern.property("eventCount").toInt() == events_before);
    check(press("guideConfirmCancel"));
    settle();
    check(!prop("confirmVisible").toBool() && ctx.pattern.property("eventCount").toInt() == events_before);
    (void)QMetaObject::invokeMethod(guide, "performGuideAction", Q_ARG(QString, "clear-pattern"),
                                    Q_ARG(QVariantMap, QVariantMap{}));
    settle();
    check(press("guideConfirmYes"));
    settle();
    check(ctx.pattern.property("eventCount").toInt() == 0);
    // The whole clear is one step of undo.
    check(ctx.song.undo());
    settle();
    check(ctx.pattern.property("eventCount").toInt() == events_before);

    // ---------------------------------------------------------------------
    ctx.reached("guide: the window stays playable under the guide");
    (void)QMetaObject::invokeMethod(guide, "say", Q_ARG(QString, ""),
                                    Q_ARG(QString, "Still here - press play, I'll keep talking."));
    settle();
    check(prop("bubbleVisible").toBool());
    check(press("playButton"));
    settle();
    check(ctx.transport.playing() && prop("bubbleVisible").toBool());
    check(press("playButton"));
    settle();
    check(!ctx.transport.playing());
    // The mixer's master strip is clickable with the guide on and talking.
    check(press("masterRack"));
    settle();
    check(ctx.song.masterRacked());

    // ---------------------------------------------------------------------
    // The closing frame is the guide talking, ring on the step grid.
    ctx.reached("guide: screenshot");
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 5), Q_ARG(int, 67));
    (void)QMetaObject::invokeMethod(guide, "spotlight", Q_ARG(QString, "stepGrid"));
    VerifyContext::settle(1200);
    lay_out();
    check(ctx.save_screenshot());
    ctx.finish(" GUIDE, narration, Q&A, lessons, guide actions and every way out");
}

[[maybe_unused]] const bool registered = register_scenario("guide", run_guide);

}  // namespace
}  // namespace blokkily::verify
