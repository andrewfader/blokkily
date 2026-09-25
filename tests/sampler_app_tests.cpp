// The sampler as the application uses it (plan item 2.3): created by the
// processor factory from a slot, placed in the production SongEngine by the
// same graph code the application runs, and heard through the production
// render callback pumped deterministically. Every claim is measured in
// rendered audio or in a bounce read back from disk.
//
// Run with a case name; each case is its own CTest test (sampler_app_<case>).
// features/sampler.feature maps its scenarios to these cases:
//   sampler_app_factory        The sampler is found and created like any other instrument
//   sampler_app_live_swap      An edit reaches a playing sampler without a rebuild
//   sampler_app_bounce_parity  A bounce of keyed and kit samplers is what the engine plays

#include "engine_graph.hpp"
#include "processor_factory.hpp"
#include "sampler_processor.hpp"
#include "fixtures/audio_fixture_content.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/instruments/sampler.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;
namespace fx = blokkily::audio_fixtures;

namespace {

const std::filesystem::path fixtures = BLOKKILY_AUDIO_FIXTURES;
const std::filesystem::path artifacts = BLOKKILY_TEST_ARTIFACTS;
constexpr double rate = 48000.0;
constexpr std::uint32_t block = 512;
// 120 BPM and 480 ticks a beat at 48 kHz: one tick is 50 samples.
constexpr Tick ticks_per_beat = 480;
constexpr std::uint64_t samples_per_tick = 50;

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string number(double value) { return std::to_string(value); }

InstrumentSlot sampler_slot(const char* identifier, const SamplerProgram* program = nullptr) {
    InstrumentSlot slot{sampler_format, "", identifier, {}};
    if (program != nullptr) slot.state = serialize_sampler(*program);
    return slot;
}

// A keyed program over the `smpl` fixture: 440 Hz at its own root, key 57,
// with a sustain loop, so a held key keeps sounding.
SamplerProgram keyed_program(AudioAssetCache& cache) {
    SamplerInstrument maker(&cache);
    std::string error;
    auto zone = maker.make_zone(fixtures / "smpl_loop_44k1_pcm16.wav", &error);
    require(zone.has_value(), "the smpl fixture must decode: " + error);
    require(zone->root_key == fx::smpl_root_key && zone->loop == LoopMode::forward,
            "the zone takes its root and loop from the file");
    SamplerProgram program = default_sampler(SamplerProgram::Mode::keyed);
    program.zones = {*zone};
    return program;
}

// tones8 chopped into eight pads from C2: pad i sounds 200 * (i + 1) Hz.
SamplerProgram kit_program(AudioAssetCache& cache) {
    SamplerInstrument maker(&cache);
    std::string error;
    auto zone = maker.make_zone(fixtures / "tones8_48k_pcm16.wav", &error);
    require(zone.has_value(), "the tones8 fixture must decode: " + error);
    SamplerProgram program = default_sampler(SamplerProgram::Mode::kit);
    require(slice_evenly(program, *zone, fx::tones8_frames, 8, 36), "tones8 is chopped");
    return program;
}

Track make_track(const char* name, InstrumentSlot instrument) {
    Track track;
    track.name = name;
    track.instrument = std::move(instrument);
    return track;
}

void add_note(Pattern& pattern, Tick start, Tick duration, int key) {
    Trigger note;
    note.start = start;
    note.duration = duration;
    note.musical_data = Note{static_cast<std::int16_t>(key), 1.0F, 0.0F};
    (void)pattern.add(note);
}

// Rendered stereo, one vector per channel.
struct Rendered {
    std::vector<float> left, right;
    [[nodiscard]] std::span<const float> window(std::size_t from, std::size_t length) const {
        return std::span<const float>(left).subspan(from, length);
    }
};

// A playing engine for `song`, built the way the application builds one: the
// factory through populate_graph, prepare, then the slots' saved state.
struct Rig {
    SongEngine engine;
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};

    Rig(const Song& song, AudioAssetCache& cache) {
        ProcessorContext context;
        context.assets = &cache;
        const auto build = populate_graph(engine, song, {}, context);
        require(build.error.empty() && build.loaded > 0, "the graph must build: " + build.error);
        std::string error;
        require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
        load_fresh_state(engine, song, build);
        require(output.open(engine) && output.start(), "the deterministic device must start");
    }

    // `frames` of the production callback, a block at a time. While the song
    // plays, the playhead must move by exactly what was rendered: nothing may
    // send it back or skip it forward.
    Rendered pump(std::size_t frames, bool continuous = true) {
        Rendered out;
        std::vector<float> stereo(static_cast<std::size_t>(block) * 2);
        for (std::size_t done = 0; done < frames; done += block) {
            const auto before = engine.sample_position();
            std::fill(stereo.begin(), stereo.end(), -3.0F);
            require(output.pump(stereo), "the production callback must render");
            if (continuous)
                require(engine.sample_position() == before + block,
                        "the playhead moved from " + std::to_string(before) + " to " +
                            std::to_string(engine.sample_position()));
            const auto take = std::min<std::size_t>(block, frames - done);
            out.left.insert(out.left.end(), stereo.begin(), stereo.begin() + take);
            out.right.insert(out.right.end(), stereo.begin() + block,
                             stereo.begin() + block + take);
        }
        return out;
    }
};

SamplerInstrument& running_sampler(SongEngine& engine, std::uint32_t track) {
    auto* sampler = dynamic_cast<SamplerInstrument*>(engine.processor(track_instrument(track)));
    require(sampler != nullptr, "the track plays a SamplerInstrument");
    return *sampler;
}

double dominant(std::span<const float> samples) {
    return probe::dominant_frequency(samples, rate, 100.0, 2000.0, 2.0);
}

// Scenario: The sampler is found and created like any other instrument.
void factory() {
    const auto catalog = internal_catalog();
    const auto listed = [&](const char* name, const char* identifier) {
        return std::any_of(catalog.begin(), catalog.end(), [&](const CatalogEntry& entry) {
            return entry.name == name && entry.kind == "instrument" &&
                   entry.slot.format == sampler_format && entry.slot.identifier == identifier;
        });
    };
    require(is_internal_format(sampler_format), "the sampler is an internal processor");
    require(listed("Sampler", sampler_keyed_identifier) &&
                listed("Drum Sampler", sampler_kit_identifier),
            "the browser is offered Sampler and Drum Sampler");

    AudioAssetCache cache;
    ProcessorContext context;
    context.assets = &cache;
    std::string error;
    auto keyed = create_processor(sampler_slot(sampler_keyed_identifier), context, &error);
    auto kit = create_processor(sampler_slot(sampler_kit_identifier), context, &error);
    require(keyed && kit, "both samplers are created: " + error);
    require(keyed->format() == "Sampler" && keyed->accepts_state_while_running(),
            "the sampler takes state while it runs");
    const auto keyed_state = parse_sampler(keyed->save_state());
    const auto kit_state = parse_sampler(kit->save_state());
    require(keyed_state && keyed_state->mode == SamplerProgram::Mode::keyed &&
                keyed_state->zones.empty(),
            "Sampler opens on an empty keyed program");
    require(kit_state && kit_state->mode == SamplerProgram::Mode::kit && kit_state->zones.empty(),
            "Drum Sampler opens on an empty kit");

    // A slot carrying a program, through the application's graph code, plays
    // it: key 69 on a file rooted at 57 is 880 Hz from the production callback.
    const auto program = keyed_program(cache);
    Song song;
    song.patterns = {{"KEYS", Pattern(1920, ticks_per_beat)}};
    song.tracks = {make_track("KEYS", sampler_slot(sampler_keyed_identifier, &program))};
    song.clips = {{0, 0, 0, 1}};
    add_note(song.patterns[0].pattern, 0, 1800, 69);
    const auto decoded = cache.size();
    Rig rig(song, cache);
    require(cache.size() == decoded, "the sampler decodes through the shared asset cache");
    rig.engine.set_playing(true);
    const auto heard = rig.pump(8192);
    const double hz = dominant(heard.window(4096, 4096));
    require(std::abs(hz - 880.0) <= 4.0, "key 69 plays 880 Hz, heard " + number(hz));
}

// Scenario: An edit reaches a playing sampler without a rebuild (design 2,
// test 9). The same engine and the same sampler instance play on; the new
// program arrives through update_processor_state alone, with no prepare and
// no recompile; the playhead never jumps; the held note keeps the old sound
// and the next one plays the new; the old program is freed afterwards.
void live_swap() {
    AudioAssetCache cache;
    const auto program = keyed_program(cache);
    Song song;
    song.patterns = {{"KEYS", Pattern(1920, ticks_per_beat)}};
    song.tracks = {make_track("KEYS", sampler_slot(sampler_keyed_identifier, &program))};
    song.clips = {{0, 0, 0, 1}};
    // Key 69 on every beat, 400 ticks long: notes at 0, 24000, 48000, 72000.
    for (Tick beat = 0; beat < 4; ++beat)
        add_note(song.patterns[0].pattern, beat * ticks_per_beat, 400, 69);
    Rig rig(song, cache);
    auto& sampler = running_sampler(rig.engine, 0);
    const auto* engine_before = &rig.engine;
    const auto clock_before = rig.engine.published_clock().sample_at(ticks_per_beat);

    rig.engine.set_playing(true);
    const auto opening = rig.pump(6144);
    const double before = dominant(opening.window(2048, 4096));
    require(std::abs(before - 880.0) <= 4.0, "before the edit key 69 is 880 Hz: " + number(before));

    // The root key goes to 69: the same key now plays the file at its pitch.
    auto retuned = program;
    retuned.zones[0].root_key = 69;
    const auto state = serialize_sampler(retuned);
    require(rig.engine.update_processor_state(track_instrument(0), state),
            "the running sampler takes the new program");
    require(&running_sampler(rig.engine, 0) == &sampler && &rig.engine == engine_before,
            "the same engine plays the same sampler instance");
    require(rig.engine.published_clock().sample_at(ticks_per_beat) == clock_before,
            "nothing was recompiled or re-prepared");

    // From 6144 up to the next beat (24000), continuously.
    const auto held = rig.pump(24000 - 6144 + 8192);
    // The note that was sounding (0 .. 20000) keeps the kit it started with.
    const double kept = dominant(held.window(8192 - 6144, 8192));
    require(std::abs(kept - 880.0) <= 4.0, "the held note keeps its old sound: " + number(kept));
    // The note on the next beat plays the new program.
    const double next = dominant(held.window(24000 - 6144 + 2048, 4096));
    require(std::abs(next - 440.0) <= 4.0, "the next note plays the new program: " + number(next));
    require(parse_sampler(sampler.save_state()) == retuned,
            "the sampler reports the program it now plays");

    // Once no voice reads the old kit, it is freed on the control thread.
    sampler.collect();
    require(sampler.kit_count() == 1, "the old program is freed once its note ended, kits: " +
                                          std::to_string(sampler.kit_count()));
}

// Scenario: A bounce of keyed and kit samplers is what the engine plays
// (design 2, test 12). The bounce is read back from disk and compared, sample
// for sample, with the same song rendered by the production callback; the
// kit's pads sound their slices at their steps.
void bounce_parity() {
    AudioAssetCache cache;
    const auto keys = keyed_program(cache);
    const auto pads = kit_program(cache);
    Song song;
    song.patterns = {{"KEYS", Pattern(1920, ticks_per_beat)},
                     {"PADS", Pattern(1920, ticks_per_beat)}};
    add_note(song.patterns[0].pattern, 0, 400, 69);
    add_note(song.patterns[0].pattern, 960, 400, 57);
    add_note(song.patterns[1].pattern, 0, 120, 36);     // pad 0: 200 Hz
    add_note(song.patterns[1].pattern, 480, 120, 40);   // pad 4: 1000 Hz
    song.tracks = {make_track("KEYS", sampler_slot(sampler_keyed_identifier, &keys)),
                   make_track("PADS", sampler_slot(sampler_kit_identifier, &pads))};
    song.clips = {{0, 0, 0, 1}, {1, 1, 1920, 1}};
    Rig rig(song, cache);
    const std::uint64_t song_frames = rig.engine.song_samples();
    require(song_frames == 2 * 1920 * samples_per_tick, "two bars: " + std::to_string(song_frames));
    constexpr std::uint64_t tail = 24000;

    std::filesystem::create_directories(artifacts);
    const auto file = artifacts / "sampler-bounce-parity.wav";
    std::string error;
    const auto report = bounce_song(rig.engine, file, WaveFormat::float32, tail, &error);
    require(report.has_value(), "bounce: " + error);
    const auto written = read_wave(file, &error);
    require(written && written->channels == 2 && written->sample_rate == 48000 &&
                written->frames == song_frames + tail,
            "the bounce reads back as the song and its tail: " + error);

    // The same song through the production callback: the arrangement, then
    // the tail with the transport stopped, as the bounce renders it.
    rig.engine.seek(0);
    rig.engine.set_playing(true);
    auto played = rig.pump(song_frames, false);
    rig.engine.set_playing(false);
    const auto ringing = rig.pump(tail, false);
    played.left.insert(played.left.end(), ringing.left.begin(), ringing.left.end());
    played.right.insert(played.right.end(), ringing.right.begin(), ringing.right.end());

    std::size_t differing = 0;
    float worst = 0.0F;
    for (std::uint64_t frame = 0; frame < written->frames; ++frame) {
        const float left = written->interleaved[frame * 2];
        const float right = written->interleaved[frame * 2 + 1];
        const float delta = std::max(std::abs(left - played.left[frame]),
                                     std::abs(right - played.right[frame]));
        worst = std::max(worst, delta);
        if (delta > 1e-6F) ++differing;
    }
    require(differing == 0, std::to_string(differing) + " frames differ, worst " + number(worst));

    Rendered bounced;
    for (std::uint64_t frame = 0; frame < written->frames; ++frame)
        bounced.left.push_back(written->interleaved[frame * 2]);
    require(probe::rms(bounced.window(0, 16000)) > 0.01, "the keys sound");
    const double first = dominant(bounced.window(2048, 8192));
    const double second = dominant(bounced.window(48000 + 2048, 8192));
    require(std::abs(first - 880.0) <= 4.0 && std::abs(second - 440.0) <= 4.0,
            "the keys play 880 then 440 Hz: " + number(first) + ", " + number(second));
    const std::size_t bar = 1920 * samples_per_tick;
    const std::size_t step4 = bar + 480 * samples_per_tick;
    const auto pad0 = bounced.window(bar, fx::tones8_hit_length);
    const auto pad4 = bounced.window(step4, fx::tones8_hit_length);
    const double low = probe::dominant_frequency(pad0, rate, 100.0, 1800.0, 10.0);
    const double high = probe::dominant_frequency(pad4, rate, 100.0, 1800.0, 10.0);
    require(std::abs(low - 200.0) <= 20.0 && std::abs(high - 1000.0) <= 20.0,
            "pads 36 and 40 play their slices: " + number(low) + ", " + number(high));
    // Each pad is silent before its step and sounding within its first
    // milliseconds (a slice starts at a sine's zero crossing, under the
    // zone's 2 ms attack).
    const auto starts_at = [&](std::size_t at) {
        const auto found = probe::first_nonzero(bounced.window(at - 2000, 4000), 1e-4F);
        return found.has_value() && *found >= 2000 && *found < 2000 + 100;
    };
    require(starts_at(bar), "pad 36 starts on the downbeat of bar 2");
    require(starts_at(step4), "pad 40 starts on its step");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> cases{
        {"factory", factory},
        {"live_swap", live_swap},
        {"bounce_parity", bounce_parity},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_sampler_app_tests <case>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& failure) {
        std::cerr << "FAIL " << argv[1] << ": " << failure.what() << '\n';
        return 1;
    }
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
}
