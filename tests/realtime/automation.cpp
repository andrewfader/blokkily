// features/automation.feature: Automation keeps the real-time rules.
//
// Item 3.1 in the production SongEngine::process(): a track with gain, pan
// and mute lanes (the strip envelope), a parameter lane on its CLAP
// instrument and on its CLAP insert, a post-fader send into a return whose
// insert is automated too, and a master insert lane; a second track in touch
// mode whose fader is taken hold of, moved and let go between blocks
// (SongEngine::move and its touch bits), with solo moves (the solo gate). A
// thousand process() calls across seeks, loop wraps, a stop and a start, a
// recompile made from the control thread and the plugin's own knob turned
// (its edits held against its lane) must not allocate or free. The audio is
// then checked, so the zero comes from an engine that played.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <dlfcn.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr blokkily::Tick bar = 1920; // 96000 samples at 120 bpm

blokkily::EffectSlot clap_effect() {
    blokkily::EffectSlot slot;
    slot.plugin = {"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}};
    return slot;
}

blokkily::AutomationTarget target(blokkily::AutomationTarget::Kind kind,
                                  blokkily::ProcessorAddress where) {
    blokkily::AutomationTarget made;
    made.kind = kind;
    made.processor = where;
    return made;
}

} // namespace

BLOKKILY_REALTIME_CASE(automation) {
    using namespace blokkily;
    using blokkily::realtime::require_no_allocations;
    using Kind = AutomationTarget::Kind;

    // One bar of keys struck every 24 ticks, so a seek is soon heard again.
    Pattern pattern(bar, 480);
    for (Tick start = 0; start < bar; start += 24) {
        Trigger trigger;
        trigger.start = start;
        trigger.duration = 24;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    Song song;
    song.patterns = {{"Keys", std::move(pattern)}};
    song.tracks = {Track{}, Track{}};
    song.clips = {{0, 0, 0, 1}, {1, 0, 0, 1}};
    song.returns = {ReturnBus{}};
    song.returns[0].inserts = {clap_effect()};
    song.master_inserts = {clap_effect()};
    song.tracks[0].inserts = {clap_effect()};
    song.tracks[0].sends = {{0, -6.0, false}};
    song.tracks[0].automation = {
        {target(Kind::gain, track_instrument(0)), {{0, -30.0}, {bar, 0.0}}},
        {target(Kind::pan, track_instrument(0)), {{0, -1.0}, {bar / 2, 1.0}}},
        {target(Kind::mute, track_instrument(0)), {{0, 0.0}, {1500, 0.0}, {1500, 1.0}}},
        {target(Kind::parameter, track_instrument(0)), {{0, 0.1}, {bar, 0.3}}},
        {target(Kind::parameter, {BusKind::track, 0, 0}), {{0, 1.0}, {bar / 3, 0.5}}},
        {target(Kind::parameter, {BusKind::ret, 0, 0}), {{0, 0.2}, {bar / 2, 0.8}}},
        {target(Kind::parameter, {BusKind::master, 0, 0}), {{0, 1.0}, {bar, 0.7}}},
    };
    song.tracks[1].automation_mode = AutomationMode::touch;
    song.tracks[1].automation = {{target(Kind::gain, track_instrument(1)), {{0, -12.0}}}};

    std::string error;
    SongEngine engine;
    const auto clap = [&error] {
        return ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    };
    const auto effect = [&error] {
        return ClapPluginInstance::create(BLOKKILY_TEST_CLAP_EFFECT_PATH,
                                          "dev.blokkily.test.effect", &error);
    };
    engine.set_instrument(0, clap());
    engine.set_instrument(1, clap());
    engine.set_processor({BusKind::track, 0, 0}, effect());
    engine.set_processor({BusKind::ret, 0, 0}, effect());
    engine.set_processor({BusKind::master, 0, 0}, effect());
    require(engine.has_instrument(0) && engine.processor({BusKind::master, 0, 0}) != nullptr,
            "the CLAP fixtures must load through the production adapter: " + error);
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);

    void* module = dlopen(BLOKKILY_TEST_CLAP_PATH, RTLD_NOW | RTLD_NOLOAD);
    require(module != nullptr, "the CLAP fixture is loaded");
    const auto turn = reinterpret_cast<void (*)(std::uint32_t, double)>(
        dlsym(module, "blokkily_test_gui_turn"));
    dlclose(module);
    require(turn != nullptr, "the CLAP fixture exports blokkily_test_gui_turn");

    std::vector<float> left(calls * block, -1.0F);
    std::vector<float> right(calls * block, -1.0F);
    std::size_t strip_moves = 0;
    std::size_t plugin_edits = 0;
    StripMoveEvent played;
    PluginEditEvent edit;
    engine.set_playing(true);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The control thread's moves, between blocks.
        if (call % 40 == 10) (void)engine.move({1, StripControl::gain, -3.0, true});
        if (call % 40 == 15) (void)engine.move({1, StripControl::gain, -9.0, true});
        if (call % 40 == 20) (void)engine.move({1, StripControl::gain, -9.0, false});
        if (call % 100 == 50) {
            song.tracks[1].mix.solo = !song.tracks[1].mix.solo;
            engine.apply_mix(song);
        }
        if (call == 250) turn(0, 0.2);
        if (call == 300) engine.seek(70000);
        if (call == 500) {
            song.tracks[0].automation[0].points[0].value = -24.0;
            require(engine.recompile(song, 0, &error), "recompile: " + error);
        }
        if (call == 700) engine.set_playing(false);
        if (call == 720) engine.set_playing(true);
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
        while (engine.take_strip_move(played)) ++strip_moves;
        while (engine.take_plugin_edit(edit)) ++plugin_edits;
    }
    require_no_allocations(total, "automation: SongEngine::process with lanes and moves");
    require(blokkily::probe::rms(std::span<const float>(left).subspan(left.size() / 2)) > 1e-4 ||
                blokkily::probe::rms(std::span<const float>(right).subspan(right.size() / 2)) > 1e-4,
            "the automated song still sounds after a thousand blocks");
    require(strip_moves > 0, "the strip moves came back stamped");
    require(plugin_edits >= 3, "the knob turned in the plugin's window came back as a gesture");
}
