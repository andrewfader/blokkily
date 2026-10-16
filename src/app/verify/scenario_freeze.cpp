// features/track_freeze.feature, run by bdd_freeze: FRZ on a mixer strip
// renders the track and unloads its instrument; the export is unchanged; one
// undo or FRZ again brings the instrument back; it survives a save and load.
#include "verify/harness.hpp"
#include "song_model.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QFileInfo>

#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_freeze(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "freeze failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto click = [&](QQuickItem* item) {
        if (item) ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        layout();
    };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    s.selectTrack(0);
    layout();
    const QString base = QFileInfo(ctx.parser.value("project")).path() + "/freeze";
    const auto exported = [&](const QString& name) {
        const QString file = base + "-" + name + ".wav";
        check(c.exportAudioFile(file, "FLOAT32"));
        auto wave = blokkily::read_wave(file.toStdString());
        check(wave.has_value());
        return wave ? wave->interleaved : std::vector<float>{};
    };
    // The largest difference over the live song's length; a frozen song
    // runs on to its render's end, and that must be silence.
    const auto worst = [](const std::vector<float>& a, const std::vector<float>& b) {
        if (b.size() < a.size()) return 1.0;
        double most = 0.0;
        for (std::size_t i = 0; i < b.size(); ++i)
            most = std::max(most, static_cast<double>(std::abs((i < a.size() ? a[i] : 0.0F) - b[i])));
        return most;
    };
    const auto live = exported("live");
    auto* engine = c.engine();
    check(engine && engine->processor(blokkily::track_instrument(0)) != nullptr);

    // FRZ on the strip.
    auto* chip = ctx.named("freeze0");
    check(VerifyContext::usable(chip, 30, 18));
    click(chip);
    check(s.song().tracks[0].frozen);
    check(chip && chip->property("on").toBool());
    check(c.status().endsWith("frozen"));
    engine = c.engine();
    check(engine && engine->processor(blokkily::track_instrument(0)) == nullptr);
    const auto frozen = exported("frozen");
    std::cerr << "frozen export differs by " << worst(live, frozen) << '\n';
    check(worst(live, frozen) < 1e-6);
    ctx.reached("freeze: FRZ unloads the instrument and the export is unchanged");

    // One undo thaws it; FRZ again freezes, and FRZ once more unfreezes.
    check(s.undo() && !s.song().tracks[0].frozen);
    engine = c.engine();
    check(engine && engine->processor(blokkily::track_instrument(0)) != nullptr);
    check(s.redo() && s.song().tracks[0].frozen);
    // Saved and loaded frozen.
    check(c.saveProject(ctx.parser.value("project")));
    check(c.loadProject(ctx.parser.value("project")));
    layout();
    check(s.song().tracks[0].frozen && worst(live, exported("reloaded")) < 1e-6);
    chip = ctx.named("freeze0");
    click(chip);
    check(!s.song().tracks[0].frozen);
    engine = c.engine();
    check(engine && engine->processor(blokkily::track_instrument(0)) != nullptr);
    check(worst(live, exported("thawed")) < 1e-6);
    ctx.reached("freeze: undo, a save and load, and FRZ again bring it back");
    click(ctx.named("freeze0"));
    check(ctx.save_screenshot());
    ctx.finish("a track frozen and unfrozen in the window, heard the same in the export");
}

[[maybe_unused]] const bool registered = register_scenario("freeze", run_freeze);

} // namespace
} // namespace blokkily::verify
