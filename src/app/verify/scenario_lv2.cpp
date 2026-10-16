// features/lv2_hosting.feature, run by bdd_lv2: the suite's LV2 bundle in the
// real window - listed by the browser with an LV2 badge, chosen like any
// instrument, heard through the production callback, and saved and loaded.
#include "verify/harness.hpp"
#include "song_model.hpp"

#include <QCoreApplication>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_lv2(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "lv2 failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    c.scanPluginPaths({}, {}, {}, {ctx.parser.value("lv2-fixture").toStdString()});
    layout();
    check(c.status().contains("1 LV2"));
    s.selectTrack(0);
    layout();
    int row = -1;
    const auto rows = c.browserPlugins();
    for (int index = 0; index < rows.size(); ++index)
        if (rows.at(index).toMap().value("name").toString() == "Blokkily Test LV2") row = index;
    check(row >= 0 && rows.at(row).toMap().value("format").toString() == "LV2");
    auto* item = row >= 0 ? ctx.named(QString("browserRow%1").arg(row)) : nullptr;
    check(VerifyContext::usable(item, 100, 24));
    if (item) ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
    layout();
    check(s.song().tracks.at(0).instrument.format == "LV2" &&
          s.song().tracks.at(0).instrument.identifier == "urn:blokkily:test-lv2");
    ctx.reached("lv2: listed with its badge and chosen in the browser");

    // Heard through the production callback: Level 0.25 x velocity.
    (void)ctx.pump();
    check(c.auditionKey(60, true));
    (void)ctx.pump();
    const float heard = ctx.pump();
    c.releaseAudition();
    std::cerr << "lv2 heard at " << heard << '\n';
    check(heard > 0.05F);
    ctx.reached("lv2: it plays through the callback");

    check(c.saveProject(ctx.parser.value("project")));
    check(c.loadProject(ctx.parser.value("project")));
    layout();
    check(s.song().tracks.at(0).instrument.format == "LV2");
    check(c.engine() && c.engine()->processor(blokkily::track_instrument(0)) &&
          c.engine()->processor(blokkily::track_instrument(0))->format() == "LV2");
    ctx.reached("lv2: it survives a save and a load");
    check(ctx.save_screenshot());
    ctx.finish("an LV2 bundle scanned, chosen, played, saved and loaded in the window");
}

[[maybe_unused]] const bool registered = register_scenario("lv2", run_lv2);

} // namespace
} // namespace blokkily::verify
