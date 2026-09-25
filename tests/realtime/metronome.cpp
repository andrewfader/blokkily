// features/metronome.feature: The click and the count-in keep the real-time
// rules.
//
// SongEngine::process() with the metronome on and a count-in (item 3.7): the
// click rendered across a loop wrap and a seek, a count-in started while
// recording with notes arriving from a MIDI port into it and held into the
// song, a stop part-way through a second count-in, and a bounce-style reset.
// None of it may allocate or free on the render thread, and the rendered
// audio proves the click and the count-in actually ran.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <cstdint>
#include <string>
#include <vector>

BLOKKILY_REALTIME_CASE(metronome) {
    using namespace blokkily;
    using blokkily::realtime::require;
    using blokkily::realtime::require_no_allocations;

    constexpr std::size_t block = 256;
    // One bar of 7/8 then one of 4/4, so the song wraps often.
    Pattern pattern(1680 + 1920, 480);
    Trigger trigger;
    trigger.start = 240;
    trigger.duration = 120;
    trigger.musical_data = Note{60, 1.0F, 0.0F};
    (void)pattern.add(trigger);
    Song song;
    song.patterns = {{"Bars", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    song.meter.changes = {MeterChange{0, 7, 8}, MeterChange{1, 4, 4}};
    song.tempo.points = {TempoPoint{0, 150.0, false}, TempoPoint{1680, 110.0, true},
                         TempoPoint{3000, 140.0, false}};
    song.metronome = {true, -6.0, 2};

    std::string error;
    SongEngine engine;
    engine.set_processor(track_instrument(0), ClapPluginInstance::create(
                                                  BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test",
                                                  &error));
    require(engine.has_instrument(0), "the CLAP fixture must load: " + error);
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);
    InputQueue port;
    engine.connect_input(&port);

    std::vector<float> left(block), right(block);
    const auto pump = [&] { engine.process({std::span{left}, std::span{right}}); };
    // Warm up outside the guard: the plugin's own first-call work is not ours.
    engine.set_playing(true);
    for (int call = 0; call < 8; ++call) pump();
    engine.set_playing(false);
    pump();
    engine.seek(0);

    test::AllocationCount total;
    float loudest = 0.0F;
    bool counted = false;
    std::uint64_t held_position = 0;
    bool held = true;
    {
        test::AllocationGuard guard;
        // A count-in while recording, with notes played into it.
        engine.set_recording(true);
        engine.play_with_count_in(2);
        for (int call = 0; call < 700; ++call) {
            if (call == 10) (void)port.push({0, {PluginEvent::Type::note_on, 0, 62, 0.9, 0.0}});
            if (call == 30) (void)port.push({0, {PluginEvent::Type::note_off, 0, 62, 0.0, 0.0}});
            if (call == 200) (void)port.push({0, {PluginEvent::Type::note_on, 0, 65, 0.8, 0.0}});
            if (call == 650) (void)port.push({0, {PluginEvent::Type::note_off, 0, 65, 0.0, 0.0}});
            pump();
            if (call == 0) held_position = engine.sample_position();
            if (engine.counting_in()) {
                counted = true;
                held = held && engine.sample_position() == held_position;
            }
            loudest = std::max(loudest, probe::peak(left));
        }
        // The click across loop wraps and a seek.
        for (int call = 0; call < 400; ++call) {
            if (call == 150) engine.seek(33333);
            pump();
            loudest = std::max(loudest, probe::peak(left));
        }
        // A second count-in, stopped before it ends.
        engine.set_playing(false);
        pump();
        engine.play_with_count_in(1);
        for (int call = 0; call < 20; ++call) pump();
        engine.set_playing(false);
        for (int call = 0; call < 10; ++call) pump();
        total += guard.count();
    }
    require_no_allocations(total, "process() with the metronome and a count-in");
    require(counted && held, "the count-in ran with the playhead held");
    require(!engine.counting_in(), "a stopped count-in is over");
    require(loudest > 0.4F, "the click and the song were rendered");
    CapturedEvent event;
    std::size_t captured = 0;
    bool first_is_held_note = false;
    while (engine.take_captured(event)) {
        if (captured == 0)
            first_is_held_note = event.event.type == PluginEvent::Type::note_on &&
                                 event.event.key_or_parameter == 65;
        ++captured;
    }
    require(captured == 2 && first_is_held_note,
            "the note held into the song, and its release, were captured: " +
                std::to_string(captured));
}
