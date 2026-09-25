// features/audio_reliability.feature, run on its own by the bdd_engine_seams
// gate (`--scenario engine_seams`). The engine seams in the real application:
// a burst of edits in one turn of the event loop is one recompile that plays
// the last of them, and rebuilding the graph keeps every instrument whose
// identity did not change as the same instance, following a deleted track.

#include "verify/harness.hpp"

#include <QCoreApplication>

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

struct FixtureLog {
    long created = -1;
    long destroyed = -1;
    friend bool operator==(const FixtureLog&, const FixtureLog&) = default;
};

// The CLAP fixture's lifecycle log, read from the module the application
// itself loaded; RTLD_NOLOAD refuses to load a second copy.
FixtureLog fixture_log(const QString& path) {
    FixtureLog log;
    void* module = dlopen(path.toStdString().c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (module == nullptr) return log;
    using Counts = void (*)(long*, long*);
    if (auto counts = reinterpret_cast<Counts>(dlsym(module, "blokkily_test_instance_counts")))
        counts(&log.created, &log.destroyed);
    dlclose(module);
    return log;
}

void run_engine_seams(VerifyContext& ctx) {
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    const QString clap = ctx.parser.value("clap-fixture");
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [&ctx] {
        (void)ctx.window->grabWindow();
        QCoreApplication::processEvents();
    };
    // The level a track's CLAP fixture plays at, heard on the bus through the
    // production callback: one key, through the track's own strip.
    const auto level_heard = [&](int track) {
        song.selectTrack(track);
        (void)ctx.pump();
        if (!controller.auditionKey(60, true)) return -1.0F;
        const float peak = ctx.pump();
        controller.releaseAudition();
        (void)ctx.pump();
        const auto& mix = song.song().tracks.at(static_cast<std::size_t>(track)).mix;
        const auto gain = blokkily::strip_gain(mix, song.song().any_solo());
        return peak / std::max(gain.left, gain.right);
    };
    const auto instrument_at = [&controller](std::uint32_t track) -> const void* {
        return controller.engine() == nullptr
                   ? nullptr
                   : controller.engine()->processor(blokkily::track_instrument(track));
    };

    // The CLAP fixture on track 0, behind the production adapter.
    check(controller.verifyClap(clap));
    check(controller.engine() != nullptr && song.song().tracks.at(0).instrument.format == "CLAP");
    controller.setTempo(120.0);
    song.selectTrack(0);
    song.selectPattern(0);
    check(ctx.window->setProperty("view", "ALL"));
    lay_out();
    VerifyContext::settle(50);
    reached("engine seams: the CLAP fixture plays track 0");

    // Fifty edits in one turn of the event loop are one recompile, of the
    // song as the last edit left it.
    {
        song.clearPattern();
        VerifyContext::settle(50);
        const int before = controller.recompileCount();
        const int rebuilds = controller.rebuildCount();
        for (int edit = 0; edit < 50; ++edit) pattern.toggleStep(edit % 16);
        check(controller.recompileCount() == before);
        VerifyContext::settle(50);
        check(controller.recompileCount() == before + 1 && controller.rebuildCount() == rebuilds);
        // Steps 0 and 1 were toggled four times, the rest three: 2..15 sound.
        bool pattern_right = !pattern.hasStep(0) && !pattern.hasStep(1);
        for (int step = 2; step < 16; ++step) pattern_right = pattern_right && pattern.hasStep(step);
        check(pattern_right);
        reached("engine seams: fifty edits in one turn are one recompile");

        // Heard through the production callback: a block in the middle of
        // each step is loud exactly where the last edit left a note.
        controller.rewindPlayback();
        controller.togglePlayback();
        constexpr std::size_t step_samples = 6000;   // a sixteenth at 120 BPM, 48 kHz
        constexpr std::size_t block = 512;   // frames per pump (1024 samples, stereo)
        std::vector<float> peaks;
        for (std::size_t index = 0; index < 16 * step_samples / block + 1; ++index)
            peaks.push_back(ctx.pump());
        controller.togglePlayback();
        bool heard_right = true;
        for (std::size_t step = 0; step < 16; ++step) {
            const auto middle = (step * step_samples + 2500) / block;
            const bool loud = middle < peaks.size() && peaks[middle] > 0.1F;
            heard_right = heard_right && loud == (step >= 2);
        }
        if (!heard_right) {
            std::cerr << "engine seams: block peaks";
            for (const float peak : peaks) std::cerr << ' ' << peak;
            std::cerr << '\n';
        }
        check(heard_right);
        reached("engine seams: the engine plays the last edit");
    }

    // A second instrument is a new graph: a rebuild that keeps track 0's
    // instance and creates only the new one. The new one is the same plugin
    // dialled to another level: the fixture's state is its level.
    {
        const auto* first = instrument_at(0);
        const auto log = fixture_log(clap);
        const int rebuilds = controller.rebuildCount();
        check(first != nullptr && log.created > 0);
        constexpr float dialled = 0.6F;
        blokkily::InstrumentSlot second_slot{"CLAP", clap.toStdString(), "dev.blokkily.test", {}};
        second_slot.state.resize(sizeof dialled);
        std::memcpy(second_slot.state.data(), &dialled, sizeof dialled);
        song.setInstrument(1, second_slot);
        const auto after = fixture_log(clap);
        check(controller.rebuildCount() == rebuilds + 1 && controller.adoptedProcessors() == 1);
        check(instrument_at(0) == first && instrument_at(1) != nullptr);
        check(after.created == log.created + 1 && after.destroyed == log.destroyed);
        check(std::abs(level_heard(0) - 0.25F) < 1e-3F);
        check(std::abs(level_heard(1) - dialled) < 1e-3F);
        reached("engine seams: an unchanged instrument survives a rebuild");

        // Deleting track 0 moves track 1 into its place with its own instance
        // and its own level; track 0's instance is the only one destroyed.
        // Matched by index instead, the survivor would be handed track 0's
        // instance and state, because both name the same plugin.
        const auto* second = instrument_at(1);
        check(song.deleteTrack(0));
        const auto deleted = fixture_log(clap);
        check(controller.rebuildCount() == rebuilds + 2 && controller.adoptedProcessors() == 1);
        check(instrument_at(0) == second);
        check(deleted.created == after.created && deleted.destroyed == after.destroyed + 1);
        const float survivor = level_heard(0);
        if (std::abs(survivor - dialled) >= 1e-3F)
            std::cerr << "REGRESSION: the track left after a delete plays at " << survivor
                      << " instead of its own " << dialled << '\n';
        check(std::abs(survivor - dialled) < 1e-3F);
        // The fixture saves its level first: alone in the old 4-byte stream, or
        // after the "BKS2" tag in the tagged stream (level, tone, velocity mode)
        // the plugin boundary (item 1.4) introduced.
        const auto& kept_state = song.song().tracks.at(0).instrument.state;
        float saved = 0.0F;
        constexpr std::size_t tagged_size = 4 + 3 * sizeof saved;
        const bool tagged = kept_state.size() == tagged_size &&
                            std::memcmp(kept_state.data(), "BKS2", 4) == 0;
        check(kept_state.size() == sizeof saved || tagged);
        if (kept_state.size() == sizeof saved)
            std::memcpy(&saved, kept_state.data(), sizeof saved);
        else if (tagged)
            std::memcpy(&saved, kept_state.data() + 4, sizeof saved);
        check(saved == dialled);
        reached("engine seams: a deleted track keeps its neighbours' instances");
    }

    lay_out();
    reached("engine seams: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | recompiles=" << controller.recompileCount()
            << " | rebuilds=" << controller.rebuildCount()
            << " | adopted=" << controller.adoptedProcessors();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("engine_seams", run_engine_seams);

}  // namespace
}  // namespace blokkily::verify
