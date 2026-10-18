// features/language.feature, run by bdd_language: LANGUAGE · DEUTSCH in
// SETTINGS redraws the window in German - the view tabs, the panels, the
// settings themselves - with no label cut short that read whole in English,
// remembered and left alone by RESET; ENGLISH puts back every label as it was.
#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QQuickItem>
#include <QStringList>

#include <functional>
#include <iostream>
#include <map>
#include <source_location>

namespace blokkily::verify {
namespace {

struct Shown {
    QString text;
    bool cut = false;
};

// Every visible text in the window, and whether it is drawn cut short:
// elided, laid out narrower than a line it does not wrap, or pushed past
// the window's right edge.
std::map<const QQuickItem*, Shown> texts(QQuickItem* root) {
    const double right = root->width() + 0.5;
    std::map<const QQuickItem*, Shown> found;
    const std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        if (item == nullptr || !item->isVisible() || item->opacity() <= 0.0) return;
        if (item->inherits("QQuickText")) {
            const QString text = item->property("text").toString();
            if (!text.trimmed().isEmpty() && item->width() > 0.0) {
                const bool wraps = item->property("wrapMode").toInt() != 0;
                const bool cut = item->property("truncated").toBool() ||
                                 (!wraps && item->width() + 0.5 < item->implicitWidth()) ||
                                 item->mapToScene(QPointF(item->width(), 0.0)).x() > right;
                found[item] = {text, cut};
            }
        }
        for (auto* child : item->childItems()) walk(child);
    };
    walk(root);
    return found;
}

void run_language(VerifyContext& ctx) {
    auto& c = ctx.controller;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "language failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] {
        for (int pass = 0; pass < 3; ++pass) {
            QCoreApplication::processEvents();
            (void)ctx.window->grabWindow();
        }
    };
    const auto click = [&](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr || !item->isVisible() || !item->isEnabled()) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        layout();
        return true;
    };
    const auto text_of = [&](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        return item ? item->property("text").toString() : QString();
    };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    check(ctx.window->setProperty("view", "ALL"));
    layout();
    // The window in English, as a gate opens it: the editors, then SETTINGS.
    const auto editors_en = texts(ctx.window->contentItem());
    check(click("viewSETTINGS"));
    const auto settings_en = texts(ctx.window->contentItem());
    for (const char* name : {"languageEN", "languageDE", "languageSYSTEM"})
        check(VerifyContext::usable(ctx.named(QString::fromLatin1(name)), 40, 18));
    check(ctx.named("languageEN") && ctx.named("languageEN")->property("on").toBool());
    check(text_of("resetLayout") == QStringLiteral("RESET"));
    ctx.reached("language: the window is in English, LANGUAGE offers ENGLISH, DEUTSCH and SYSTEM");

    // DEUTSCH: the labels change, and none that read whole is cut short.
    check(click("languageDE"));
    check(c.uiSettings().value("ui/language").toString() == QStringLiteral("de"));
    check(ctx.named("languageDE") && ctx.named("languageDE")->property("on").toBool());
    const auto compare = [&](const std::map<const QQuickItem*, Shown>& english, const char* where) {
        int changed = 0, cut = 0;
        // A label laid out anew has no English twin; it may be cut only
        // where some English label was.
        QStringList cut_in_english;
        for (const auto& [item, shown] : english)
            if (shown.cut) cut_in_english << shown.text;
        for (const auto& [item, shown] : texts(ctx.window->contentItem())) {
            const auto before = english.find(item);
            if (before == english.end()) {
                if (shown.cut && !cut_in_english.contains(shown.text)) {
                    ++cut;
                    std::cerr << "language: new label \"" << shown.text.toStdString() << "\" is cut short in "
                              << where << '\n';
                }
                continue;
            }
            if (before->second.text == shown.text) continue;
            ++changed;
            if (shown.cut && !before->second.cut) {
                ++cut;
                std::cerr << "language: \"" << shown.text.toStdString() << "\" (\""
                          << before->second.text.toStdString() << "\") is cut short in " << where << '\n';
            }
        }
        std::cerr << "language: " << changed << " labels in German in " << where << ", " << cut << " cut short\n";
        return std::pair{changed, cut};
    };
    const auto [settings_changed, settings_cut] = compare(settings_en, "SETTINGS");
    check(settings_changed >= 25 && settings_cut == 0);
    check(text_of("resetLayout") == QStringLiteral("ZURÜCKSETZEN"));
    if (ctx.parser.isSet("screenshot")) {
        QString path = ctx.parser.value("screenshot");
        check(ctx.save_screenshot_to(path.left(path.size() - 4) + QStringLiteral("-settings-de.png")));
    }
    check(click("viewSETTINGS"));
    const auto [editors_changed, editors_cut] = compare(editors_en, "the editors");
    check(editors_changed >= 60 && editors_cut == 0);
    check(text_of("helpButton") == QStringLiteral("HILFE") && text_of("viewSCORE") == QStringLiteral("NOTEN"));
    if (ctx.parser.isSet("screenshot")) {
        QString path = ctx.parser.value("screenshot");
        check(ctx.save_screenshot_to(path.left(path.size() - 4) + QStringLiteral("-de.png")));
    }
    ctx.reached("language: DEUTSCH redraws the window in German, every label whole");

    // Remembered, and RESET is the layout's, not the language's.
    check(click("viewSETTINGS") && click("resetLayout"));
    check(c.uiSettings().value("ui/language").toString() == QStringLiteral("de") &&
          text_of("resetLayout") == QStringLiteral("ZURÜCKSETZEN"));
    ctx.reached("language: RESET keeps the language");

    // ENGLISH: every label is what it was.
    check(click("languageEN") && click("viewSETTINGS"));
    check(c.uiSettings().value("ui/language").toString() == QStringLiteral("en"));
    int differs = 0;
    for (const auto& [item, shown] : texts(ctx.window->contentItem())) {
        const auto before = editors_en.find(item);
        if (before != editors_en.end() && before->second.text != shown.text) {
            ++differs;
            std::cerr << "language: still \"" << shown.text.toStdString() << "\"\n";
        }
    }
    check(differs == 0 && text_of("viewSTEP") == QStringLiteral("STEP"));
    ctx.reached("language: ENGLISH puts every label back");
    check(ctx.save_screenshot());
    ctx.finish("the window in another language");
}

[[maybe_unused]] const bool registered = register_scenario("language", run_language);

} // namespace
} // namespace blokkily::verify
