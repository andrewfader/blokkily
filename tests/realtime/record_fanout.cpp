// features/record_everything.feature: One key reaches every armed track -
// without the render callback allocating.
//
// Sixty-four tracks, each playing the real CLAP fixture, are all armed. A MIDI
// keyboard (the deterministic input, through the production decode and route
// path) and the on-screen perform queue each play keys while the transport
// runs and records. Every key fans out to all sixty-four tracks and is
// captured once per track. Not one process() call may allocate or free, every
// track must really sound the keys, and every capture must arrive.

#include "realtime_case.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/midi/input_routes.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

BLOKKILY_REALTIME_CASE(record_fanout) {
    using namespace blokkily;
    using blokkily::realtime::require;
    using blokkily::realtime::require_no_allocations;

    constexpr std::size_t tracks = routable_tracks;
    constexpr std::size_t block = 256;
    constexpr std::size_t calls = 400;

    Song song;
    song.patterns = {{"Take", Pattern(1920, 480)}};
    song.tracks.resize(tracks);
    for (std::size_t track = 0; track < tracks; ++track) {
        song.tracks[track].input.armed = true;
        song.clips.push_back({track, 0, 0, 1});
    }

    std::string error;
    SongEngine engine;
    for (std::size_t track = 0; track < tracks; ++track) {
        engine.set_instrument(track, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                                "dev.blokkily.test", &error));
        require(engine.has_instrument(track), "the CLAP fixture must load: " + error);
    }
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);

    MidiInput keyboard(MidiInput::Mode::deterministic);
    require(keyboard.open(std::size_t{0}, &error), "open the deterministic input: " + error);
    keyboard.set_routes(midi_routes(song, 0));
    const auto surfaces = surface_routes(song, 0);
    require(surfaces == ~TrackMask{0}, "the surfaces reach all sixty-four armed tracks");
    engine.connect_input(&keyboard.queue());
    engine.set_recording(true);
    engine.set_playing(true);

    // Everything the loop touches is made before the first armed call.
    std::vector<float> left(block, 0.0F);
    std::vector<float> right(block, 0.0F);
    std::array<std::uint8_t, 3> key_down{0x90, 60, 100};
    std::array<std::uint8_t, 3> key_up{0x80, 60, 0};
    std::size_t captured_on = 0;
    std::size_t captured_off = 0;
    std::size_t keys = 0;
    std::size_t quiet_tracks = 0;
    CapturedEvent event;

    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The control side: the keyboard's port thread and the interface.
        // They run outside the armed region, as they do in the application.
        const auto phase = call % 100;
        if (phase == 10) {
            // A different channel each time; the routes are omni.
            key_down[0] = static_cast<std::uint8_t>(0x90 | (call / 100));
            key_up[0] = static_cast<std::uint8_t>(0x80 | (call / 100));
            require(keyboard.inject(key_down), "inject key down");
            ++keys;
        }
        if (phase == 40) require(keyboard.inject(key_up), "inject key up");
        if (phase == 50 || phase == 80) {
            const auto type = phase == 50 ? PluginEvent::Type::note_on
                                          : PluginEvent::Type::note_off;
            for (std::size_t track = 0; track < tracks; ++track)
                require(engine.perform(track, {type, 0, 72, 0.8, 0.0}), "perform");
            keys += phase == 50 ? 1 : 0;
        }

        {
            test::AllocationGuard guard;
            engine.process({std::span<float>{left.data(), block},
                            std::span<float>{right.data(), block}});
            total += guard.count();
        }
        // Every track sounds while a key is down: a route that missed one
        // would leave it silent.
        if (phase == 20 || phase == 60)
            for (std::size_t track = 0; track < tracks; ++track)
                if (engine.track_peak(track) < 0.1F) ++quiet_tracks;
        while (engine.take_captured(event)) {
            if (event.event.type == PluginEvent::Type::note_on) ++captured_on;
            if (event.event.type == PluginEvent::Type::note_off) ++captured_off;
        }
    }
    require_no_allocations(total, "400 SongEngine::process calls into 64 armed tracks");
    require(quiet_tracks == 0,
            std::to_string(quiet_tracks) + " track-blocks were silent while a key was down");
    require(captured_on == keys * tracks && captured_off == keys * tracks,
            "every key captured on every armed track: " + std::to_string(captured_on) + " on, " +
                std::to_string(captured_off) + " off, wanted " +
                std::to_string(keys * tracks) + " of each");
}
