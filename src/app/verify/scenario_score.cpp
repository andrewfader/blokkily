// features/score_view.feature, run on its own by the bdd_score gate
// (`--verify --scenario score --view SCORE`). Notes are written through the
// pattern model the way the editors write them; what is checked is the
// scoreModel projection (object counts, clef names, key labels) and the score
// canvas on screen at a usable size, plus screenshots of the rendered staff.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
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
    // Honor --view like the default scenario does, so the gate leaves the
    // interface in the SCORE view and the screenshot shows it full-size.
    if (!ctx.parser.value("view").isEmpty())
        (void)window->setProperty("view", ctx.parser.value("view"));
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "score check failed at line " << at.line() << '\n';
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

    auto* const score = score_model(ctx);
    check(score != nullptr);
    if (score == nullptr) { ctx.finish(" no scoreModel in the window"); return; }
    const auto prop = [score](const char* name) { return score->property(name); };
    const auto set = [score](const char* name, const QVariant& value) {
        score->setProperty(name, value);
    };

    auto* const view = ctx.named("scoreView");
    auto* const canvas = ctx.named("scoreCanvas");

    // ---------------------------------------------------------------------
    ctx.reached("score: the SCORE view draws the pattern's notes as notation");
    check(view != nullptr && view->isVisible());
    // Shown is not the same as usable: a panel squeezed to nothing by a
    // neighbour still reports itself visible.
    check(VerifyContext::usable(view, 200, 120));
    check(canvas != nullptr && canvas->isVisible());
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 0), Q_ARG(int, 60));
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 4), Q_ARG(int, 64));
    (void)QMetaObject::invokeMethod(&ctx.pattern, "setStepKey", Q_ARG(int, 8), Q_ARG(int, 67));
    settle();
    check(prop("objectCount").toInt() >= 3);
    check(prop("clefName").toString() == "Treble");
    settle();
    check(ctx.save_screenshot());

    // ---------------------------------------------------------------------
    ctx.reached("score: the clef picker reaches the obscure clefs");
    set("clefKind", 0); // French violin
    settle();
    check(prop("clefName").toString() == "French violin");
    check(prop("objectCount").toInt() >= 3); // re-spelled, nothing lost
    check(prop("clefRefPos").toInt() == 0);  // G line is the bottom line
    settle();
    {
        const QString shot = ctx.parser.value("screenshot");
        if (!shot.isEmpty())
            check(ctx.save_screenshot_to(shot + ".french-violin.png"));
    }
    set("clefKind", 1); // back to treble
    settle();
    check(prop("clefName").toString() == "Treble");

    // ---------------------------------------------------------------------
    ctx.reached("score: CHANT draws an unmetered staff");
    set("unmetered", true);
    settle();
    check(prop("unmetered").toBool());
    {
        const QVariantList objects = prop("objects").toList();
        check(!objects.isEmpty());
        // Chant draws bare noteheads: every note is stemless.
        bool all_stemless = true;
        for (const auto& item : objects) {
            const QVariantMap map = item.toMap();
            if (map.value("kind").toString() == "note" && !map.value("stemless").toBool())
                all_stemless = false;
        }
        check(all_stemless);
    }
    settle();
    {
        const QString shot = ctx.parser.value("screenshot");
        if (!shot.isEmpty())
            check(ctx.save_screenshot_to(shot + ".chant.png"));
    }
    set("unmetered", false);
    settle();

    // ---------------------------------------------------------------------
    ctx.reached("score: the key picker re-spells accidentals");
    set("keyFifths", 3);
    settle();
    check(prop("keyFifths").toInt() == 3);
    check(prop("keyLabel").toString() == "A major");
    check(prop("keySignature").toList().size() == 3);
    settle();
    {
        const QString shot = ctx.parser.value("screenshot");
        if (!shot.isEmpty())
            check(ctx.save_screenshot_to(shot + ".three-sharps.png"));
    }
    set("keyFifths", 0);
    settle();
    check(prop("keySignature").toList().isEmpty());

    ctx.finish(" SCORE view, obscure clefs, chant staff, key signatures");
}

[[maybe_unused]] const bool registered = register_scenario("score", run_score);

}  // namespace
}  // namespace blokkily::verify
