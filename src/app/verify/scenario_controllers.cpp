// features/continuous_midi.feature, run by the bdd_controller_lanes gate
// (`--scenario controllers`). The piano roll's controller lane in the real
// application: a lane is chosen from its rendered picker, a bend is drawn,
// moved and erased with the mouse on the rendered lane, each gesture one step
// of history in the canonical pattern that leaves the steps, the tracker and
// the roll as they were, and the edit reaches the running engine without a
// rebuild: the song exported through that engine is, sample for sample, the
// CLAP fixture given the drawn bend on the sample its tick falls on.

#include "verify/harness.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <QCoreApplication>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <source_location>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

// A plugin event at an absolute sample, for the reference render.
struct Placed {
    std::uint64_t sample;
    PluginEvent event;
};

// The CLAP fixture alone, through the production adapter, given `events` at
// their samples in 256-sample blocks: what the song should sound like.
std::vector<float> reference(const QString& fixture, double rate, std::size_t frames,
                             std::vector<Placed> events) {
    std::string error;
    auto plugin = ClapPluginInstance::create(fixture.toStdString(), "dev.blokkily.test", &error);
    if (plugin == nullptr || !plugin->activate(rate, 1, 256)) return {};
    std::stable_sort(events.begin(), events.end(),
                     [](const Placed& a, const Placed& b) { return a.sample < b.sample; });
    std::vector<float> left(frames), right(frames);
    std::vector<PluginEvent> due;
    std::size_t next = 0;
    for (std::size_t start = 0; start < frames; start += 256) {
        const auto count = std::min<std::size_t>(256, frames - start);
        due.clear();
        while (next < events.size() && events[next].sample < start + count) {
            auto event = events[next++].event;
            event.sample_offset = static_cast<std::uint32_t>(
                events[next - 1].sample > start ? events[next - 1].sample - start : 0);
            due.push_back(event);
        }
        plugin->process({std::span(left).subspan(start, count), std::span(right).subspan(start, count)},
                        due);
    }
    return left;
}

void run_controllers(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "controllers: check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto click_named = [&](const QString& name) {
        auto* item = ctx.named(name);
        check(VerifyContext::usable(item, 16, 16));
        if (item == nullptr) return false;
        ctx.click_at(item, {item->width() / 2.0, item->height() / 2.0}, Qt::LeftButton);
        lay_out();
        return true;
    };
    const auto pick = [&](const QString& picker, int row) {
        check(click_named(picker));
        auto* owner = ctx.named(picker);
        auto* menu = owner == nullptr ? nullptr : owner->findChild<QObject*>(picker + "Menu");
        check(menu != nullptr && menu->property("opened").toBool());
        QQuickItem* entry = nullptr;
        if (menu != nullptr)
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, entry),
                                      Q_ARG(int, row));
        check(entry != nullptr);
        if (entry != nullptr)
            ctx.click_at(entry, {entry->width() / 2, entry->height() / 2}, Qt::LeftButton);
        lay_out();
    };
    // The open pattern's movements of one controller, in tick order.
    const auto lane = [&](ContinuousEvent::Kind kind, std::uint8_t number = 0) {
        std::vector<ContinuousEvent> points;
        for (const auto& event : song.editPattern().continuous())
            if (event.kind == kind && event.controller == number) points.push_back(event);
        return points;
    };
    const auto bends = [&] { return lane(ContinuousEvent::Kind::pitch_bend); };

    // --- The session: the CLAP fixture in its tone mode on one track, one
    // long A4 on step 1 of a one-bar pattern. ------------------------------
    check(controller.verifyClap(parser.value("clap-fixture")));
    {
        auto configured = song.song();
        const auto synth = configured.tracks.at(0).instrument;
        configured.tracks.assign(1, Track{});
        configured.tracks[0].name = "LEAD";
        configured.tracks[0].instrument = synth;
        Pattern held(1920, 480);
        Trigger note;
        note.start = 0;
        note.duration = 1900;
        note.musical_data = Note{69, 0.8F, 0.0F};
        note.locks = {{"tone", 1, 1.0, ParameterLock::Kind::automation}};
        (void)held.add(note);
        configured.patterns = {{"LEAD", std::move(held)}};
        configured.clips = {{0, 0, 0, 1}};
        configured.launcher = SceneMatrix{};
        configured.modulators.clear();
        song.replace(std::move(configured));
        controller.flushRecompile();
    }
    controller.setTempo(120.0);
    check(window->setProperty("view", "ALL"));
    lay_out();
    check(controller.engine() != nullptr && controller.engine()->has_instrument(0));
    const int rebuilds = controller.rebuildCount();
    const auto tracker_before = ctx.tracker_note(0);
    check(pattern.rowCount() == 1 && ctx.rendered_step(0) && ctx.roll_draws(0) &&
          !tracker_before.isEmpty());
    auto* lane_input = ctx.named("controllerLaneInput");
    check(VerifyContext::usable(ctx.named("rollControls"), 300, 50) &&
          VerifyContext::usable(ctx.named("controllerLane"), 300, 40) &&
          VerifyContext::usable(lane_input, 280, 36) &&
          VerifyContext::usable(ctx.named("controllerLanePicker"), 40, 16) &&
          VerifyContext::usable(ctx.named("pianoRollView"), 300, 200));
    reached("controllers: the piano roll shows an empty controller lane at a usable size");

    // Where tick `tick` and level `level` of the lane are on screen.
    const auto point = [&](int tick, double level) {
        const double width = lane_input == nullptr ? 0.0 : lane_input->width();
        const double height = lane_input == nullptr ? 0.0 : lane_input->height();
        const double unit = level;   // the bend lane: -1..1, 0 in the middle
        const double y = 2.0 + (1.0 - (unit + 1.0) / 2.0) * (height - 4.0);
        return QPointF(width * tick / 1920.0, y);
    };
    const auto drag = [&](Qt::MouseButton button, std::vector<QPointF> path) {
        if (lane_input == nullptr || path.empty()) return;
        ctx.mouse_at(lane_input, path.front(), QEvent::MouseButtonPress, button, button);
        for (std::size_t at = 1; at < path.size(); ++at)
            ctx.mouse_at(lane_input, path[at], QEvent::MouseMove, Qt::NoButton, button);
        ctx.mouse_at(lane_input, path.back(), QEvent::MouseButtonRelease, button, Qt::NoButton);
        lay_out();
    };

    // --- 1. BEND from the picker, and a stroke drawn along the top. -------
    pick("controllerLanePicker", 0);
    check(ctx.named("controllerLanePicker")->property("value").toString() == "BEND");
    drag(Qt::LeftButton, {point(480, 1.0), point(720, 1.0), point(960, 1.0)});
    {
        const auto drawn = bends();
        bool full = drawn.size() == 33;
        for (std::size_t at = 0; at < drawn.size() && full; ++at)
            full = drawn[at].tick == 480 + static_cast<Tick>(at) * 15 && drawn[at].value == 16383;
        check(full);
        std::cerr << "controllers: the stroke wrote " << drawn.size() << " bend points\n";
        check(ctx.named("controllerPointCount")->property("text").toString() == "33 PTS" &&
              VerifyContext::usable(ctx.named("controllerPoint0"), 2, 2));
    }
    // One gesture, one step of history; the steps, the tracker and the roll
    // are as they were.
    check(song.undo() && bends().empty());
    check(song.redo() && bends().size() == 33);
    lay_out();
    check(pattern.rowCount() == 1 && ctx.rendered_step(0) && ctx.roll_draws(0) &&
          ctx.tracker_note(0) == tracker_before);
    reached("controllers: a stroke drawn on the lane writes the bend, one undo step");

    // --- 2. The last point dragged later and back to the centre; the first
    // one erased with the right button. ---------------------------------------
    drag(Qt::LeftButton, {point(960, 1.0), point(1080, 0.5), point(1200, 0.0)});
    {
        const auto moved = bends();
        check(moved.size() == 33 && moved.back().tick == 1200 && moved.back().value == 8192 &&
              moved[moved.size() - 2].tick == 945);
    }
    drag(Qt::RightButton, {point(480, 1.0)});
    const auto final_bends = bends();
    check(!final_bends.empty() && final_bends.front().tick > 480 && final_bends.size() < 33 &&
          final_bends.back().tick == 1200);
    check(controller.rebuildCount() == rebuilds);
    reached("controllers: a point dragged moves it, a right click erases it");

    // --- 3. The engine plays what was drawn, on its sample. ---------------
    const QString exported = parser.value("export");
    check(!exported.isEmpty() && controller.exportAudioFile(exported, "FLOAT32"));
    check(controller.rebuildCount() == rebuilds);
    {
        std::string error;
        const auto wave = read_wave(exported.toStdString(), &error);
        const double rate = controller.engine()->sample_rate();
        const auto& clock = controller.engine()->published_clock();
        const std::size_t frames = 96000;
        check(wave.has_value() && wave->frames >= frames);
        const auto events = [&](std::int64_t shift) {
            std::vector<Placed> placed{
                {0, {PluginEvent::Type::parameter_value, 0, 1, 1.0}},
                {0, {PluginEvent::Type::note_on, 0, 69, 0.8}},
                {sample_for_tick(clock, 1900.0), {PluginEvent::Type::note_off, 0, 69, 0.0}}};
            for (std::size_t at = 0; at < final_bends.size(); ++at) {
                const auto& bend = final_bends[at];
                auto sample = static_cast<std::int64_t>(
                    sample_for_tick(clock, static_cast<double>(bend.tick)));
                if (at == 0) sample += shift;
                placed.push_back({static_cast<std::uint64_t>(sample),
                                  {PluginEvent::Type::midi_raw, 0,
                                   static_cast<std::int32_t>(midi_raw_of(bend)), 0.0}});
            }
            return placed;
        };
        const float centre = strip_gain(MixerStrip{}, false).left;
        const auto worst = [&](const std::vector<float>& expected) {
            if (!wave || expected.size() < frames) return 1.0;
            double most = 0.0;
            for (std::size_t frame = 0; frame < frames; ++frame)
                most = std::max(most, std::abs(static_cast<double>(wave->interleaved[2 * frame]) -
                                               static_cast<double>(centre * expected[frame])));
            return most;
        };
        const QString fixture = parser.value("clap-fixture");
        const double exact = worst(reference(fixture, rate, frames, events(0)));
        const double late = worst(reference(fixture, rate, frames, events(1)));
        const double early = worst(reference(fixture, rate, frames, events(-1)));
        std::cerr << "controllers: export against the fixture given the bend at tick "
                  << final_bends.front().tick << ": worst " << exact << ", a sample late "
                  << late << ", early " << early << '\n';
        check(exact < 1e-6 && late > 1e-4 && early > 1e-4);
    }
    reached("controllers: the drawn bend reaches the engine on its exact sample");

    // --- 4. The other lanes: MOD from the picker, then a numbered CC. -----
    pick("controllerLanePicker", 1);
    check(ctx.named("controllerLanePicker")->property("value").toString() == "MOD" &&
          ctx.named("controllerPointCount")->property("text").toString() == "0 PTS");
    {
        // Half way up: CC 1 at 64.
        const double height = lane_input == nullptr ? 0.0 : lane_input->height();
        drag(Qt::LeftButton, {QPointF(lane_input->width() * 240.0 / 1920.0, 2.0 + 0.5 * (height - 4.0))});
        const auto mod = lane(ContinuousEvent::Kind::control_change, 1);
        check(mod.size() == 1 && mod.front().tick == 240 && mod.front().value == 64);
    }
    pick("controllerLanePicker", 3);
    check(ctx.named("controllerNumber") != nullptr &&
          ctx.named("controllerNumber")->property("text").toString() == "CC 7");
    check(click_named("controllerNumberUp"));
    check(ctx.named("controllerNumber")->property("text").toString() == "CC 8");
    drag(Qt::LeftButton, {QPointF(lane_input->width() * 600.0 / 1920.0, 2.0)});
    const auto cc8 = lane(ContinuousEvent::Kind::control_change, 8);
    check(cc8.size() == 1 && cc8.front().tick == 600 && cc8.front().value == 127);
    check(bends() == final_bends && pattern.rowCount() == 1);
    reached("controllers: the picker chooses the mod wheel and any numbered CC");

    // --- The picture: the bend lane with the drawn stroke. ----------------
    check(song.undo() && song.undo());
    pick("controllerLanePicker", 0);
    VerifyContext::settle(50);
    lay_out();
    check(VerifyContext::usable(ctx.named("controllerLane"), 300, 40) &&
          ctx.named("controllerPointCount")->property("text").toString() ==
              QString("%1 PTS").arg(final_bends.size()));
    reached("controllers: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | bends=" << final_bends.size() << " | rebuilds=" << controller.rebuildCount()
            << " | " << controller.exportStatus().toStdString();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("controllers", run_controllers);

}  // namespace
}  // namespace blokkily::verify
