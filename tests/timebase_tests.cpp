// Executable scenarios for features/timebase.feature (plan item 1.2, F-A): the
// tempo map, the meter map, the tick clock, and the engine, the bounce and a
// recording playing through them. Each case is its own CTest test,
// timebase_<case>. Audio claims are read off audio rendered through the
// production render callback (the deterministic pump) by the real CLAP
// fixture, which sounds DC while a note is held, so every note is an edge; by
// the real VST3 fixture through the JUCE host adapter; and by FluidSynth
// playing a real SoundFont. Expected sample positions are worked out by hand
// (or by numeric integration for ramps), never by asking the clock under test.

#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/sequencer/take.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

bool near(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance;
}

std::string list(const std::vector<long long>& values) {
    std::ostringstream out;
    out << '{';
    for (std::size_t index = 0; index < values.size(); ++index)
        out << (index ? ", " : "") << values[index];
    out << '}';
    return out.str();
}

// Drives the production render callback, the way a device does.
class Pump {
public:
    explicit Pump(AudioSource& source) : output_(RtAudioOutput::Mode::deterministic) {
        require(output_.open(source) && output_.start(), "the production output must start");
    }
    // The left channel of the next `frames` frames, in callbacks of `block`.
    std::vector<float> left(std::size_t frames, std::size_t block = 1024) {
        std::vector<float> result;
        result.reserve(frames);
        std::vector<float> stereo;
        while (result.size() < frames) {
            const auto now = std::min(block, frames - result.size());
            stereo.assign(now * 2, -1.0F);
            require(output_.pump(stereo), "the production callback must render");
            result.insert(result.end(), stereo.begin(), stereo.begin() + static_cast<long>(now));
        }
        return result;
    }

private:
    RtAudioOutput output_;
};

std::unique_ptr<PluginInstance> clap_fixture() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(instance != nullptr, "the CLAP fixture must load through the production adapter: " + error);
    return instance;
}

// A song of one track, hard left so the left channel is the track at unity,
// playing `pattern` from each of `starts`.
Song song_of(Pattern pattern, std::vector<Tick> starts = {0}) {
    Song song;
    song.patterns = {{"Timebase", std::move(pattern)}};
    song.tracks = {Track{}};
    song.tracks[0].mix.pan = -1.0;
    song.clips.clear();
    for (const auto start : starts) song.clips.push_back({0, 0, start, 1});
    return song;
}

// `count` notes, one every `every` ticks from tick 0, each `length` long.
Pattern beats(Tick pattern_length, Tick every, Tick length, int count) {
    Pattern pattern(pattern_length, 480);
    for (int index = 0; index < count; ++index) {
        Trigger trigger;
        trigger.start = index * every;
        trigger.duration = length;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    return pattern;
}

// Where each note starts: the samples where the signal rises out of silence.
std::vector<long long> onsets(std::span<const float> samples, float threshold = 1e-4F) {
    std::vector<long long> found;
    bool below = true;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const bool now_below = std::abs(samples[index]) <= threshold;
        if (below && !now_below) found.push_back(static_cast<long long>(index));
        below = now_below;
    }
    return found;
}

// Plays `song` on the CLAP fixture through the production callback, from the
// start, for exactly one pass of the song.
std::vector<float> render_clap(const Song& song, std::uint64_t* song_samples = nullptr) {
    SongEngine engine;
    engine.set_instrument(0, clap_fixture());
    std::string error;
    require(engine.prepare(song, 48000.0, 512, 0, &error), "prepare: " + error);
    if (song_samples != nullptr) *song_samples = engine.song_samples();
    engine.set_playing(true);
    Pump pump(engine);
    return pump.left(engine.song_samples());
}

// Seconds to cross ticks [0, tick) of a tempo map, by brute-force numeric
// integration of 60 / (bpm(t) * tpb): the independent check of the closed
// form. bpm(t) is read off the points by linear interpolation here.
double integrate_seconds(const std::vector<TempoPoint>& points, double tick, double tpb) {
    const auto bpm_at = [&points](double at) {
        std::size_t index = 0;
        while (index + 1 < points.size() && static_cast<double>(points[index + 1].at) <= at)
            ++index;
        const auto& point = points[index];
        if (!point.ramp || index + 1 >= points.size()) return point.bpm;
        const auto& next = points[index + 1];
        const double fraction =
            (at - static_cast<double>(point.at)) / static_cast<double>(next.at - point.at);
        return point.bpm + (next.bpm - point.bpm) * fraction;
    };
    constexpr int steps_per_tick = 64;
    const int steps = static_cast<int>(tick * steps_per_tick);
    const double width = tick / steps;
    double seconds = 0.0;
    for (int step = 0; step < steps; ++step) {
        const double middle = (step + 0.5) * width;   // midpoint rule
        seconds += width * 60.0 / (bpm_at(middle) * tpb);
    }
    return seconds;
}

// ---- The maps ------------------------------------------------------------------

// Scenario: A song with one tempo keeps the arithmetic it always had.
void tempo_map_constant() {
    TempoMap tempo;
    require(tempo.valid() && tempo.points.size() == 1 && tempo.bpm_at(123456) == 120.0,
            "a new tempo map is one point of 120 BPM at tick 0");
    // 480 ticks a beat at 120 BPM: half a second a beat, two seconds a bar.
    require(tempo.seconds_at(480, 480) == 0.5 && tempo.seconds_at(1920, 480) == 2.0,
            "a beat is half a second");
    require(tempo.tick_at_seconds(2.0, 480) == 1920.0, "two seconds is one bar");
    require(tempo.seconds_at(-10, 480) == 0.0 && tempo.tick_at_seconds(-1.0, 480) == 0.0,
            "nothing lies before the song");
    // The default clock is 120 BPM, 480 ticks a beat, 48 kHz: 50 samples a tick.
    const TickClock clock;
    require(clock.sample_rate() == 48000.0 && clock.sample_at(1) == 50.0 &&
                clock.sample_at(1920) == 96000.0 && clock.tick_at(96000.0) == 1920.0,
            "the default clock is 50 samples a tick");
    // tick_at_sample agrees with the single-tempo formula the recorder used
    // before the tempo map, on every sample of two bars at 44.1 kHz too.
    for (const double rate : {48000.0, 44100.0}) {
        const TickClock at_rate(tempo, 480, rate);
        const double per_sample = 120.0 * 480.0 / (60.0 * rate);
        for (std::uint64_t sample = 0; sample < 2 * static_cast<std::uint64_t>(rate) * 2; ++sample) {
            const auto old = static_cast<Tick>(std::floor(static_cast<double>(sample) * per_sample));
            const auto now = tick_at_sample(at_rate, sample);
            if (now != old && !(now == old + 1 && std::abs(sample * per_sample - (old + 1)) < 1e-6))
                throw std::runtime_error("tick_at_sample disagrees with the constant tempo at " +
                                         std::to_string(sample) + ": " + std::to_string(now) +
                                         " vs " + std::to_string(old));
        }
    }
}

// Scenario: A tempo step changes the tempo from its tick on.
void tempo_map_step() {
    TempoMap tempo;
    tempo.set({1920, 60.0, false});
    require(tempo.valid() && tempo.points.size() == 2, "a step is a second point");
    require(tempo.bpm_at(1919) == 120.0 && tempo.bpm_at(1920) == 60.0, "the step is at its tick");
    // Bar 1 at 120 BPM is two seconds; each beat after it at 60 BPM is one.
    require(tempo.seconds_at(1920, 480) == 2.0 && tempo.seconds_at(2400, 480) == 3.0 &&
                tempo.seconds_at(3840, 480) == 6.0,
            "seconds add up across the step");
    require(tempo.tick_at_seconds(3.0, 480) == 2400.0 && tempo.tick_at_seconds(1.0, 480) == 960.0,
            "seconds read back as ticks on both sides of the step");
    const TickClock clock(tempo, 480, 48000.0);
    require(clock.sample_at(1920) == 96000.0 && clock.sample_at(2400) == 144000.0 &&
                clock.tick_at(144000.0) == 2400.0 && tick_at_sample(clock, 143999) == 2399 &&
                tick_at_sample(clock, 144000) == 2400,
            "the clock places ticks across the step");
    require(clock.bpm_at_sample(95999.0) == 120.0 && clock.bpm_at_sample(96000.0) == 60.0,
            "the clock knows the tempo at a sample");
}

// Scenario: A ramp glides the tempo, in closed form.
void tempo_map_ramp() {
    TempoMap tempo;
    tempo.points = {{0, 60.0, true}, {3840, 180.0, false}, {5760, 90.0, true}, {7680, 150.0, false}};
    require(tempo.valid(), "a ramped map is valid");
    require(tempo.bpm_at(1920) == 120.0 && tempo.bpm_at(6720) == 120.0 && tempo.bpm_at(9000) == 150.0,
            "a ramp is linear in ticks and the last point holds");
    for (const Tick tick : {Tick{1}, Tick{480}, Tick{1920}, Tick{3840}, Tick{4000}, Tick{6000},
                            Tick{7680}, Tick{9000}}) {
        const double exact = integrate_seconds(tempo.points, static_cast<double>(tick), 480.0);
        const double closed = tempo.seconds_at(tick, 480);
        require(near(closed, exact, 1e-7),
                "closed-form seconds at tick " + std::to_string(tick) + ": " +
                    std::to_string(closed) + " vs integrated " + std::to_string(exact));
        require(near(tempo.tick_at_seconds(closed, 480), static_cast<double>(tick), 1e-6),
                "seconds read back as the same tick at " + std::to_string(tick));
    }
    const TickClock clock(tempo, 480, 48000.0);
    for (double tick = 0.0; tick < 9000.0; tick += 37.25) {
        const double sample = clock.sample_at_tick(tick);
        require(near(sample, 48000.0 * tempo.seconds_at_tick(tick, 480), 1e-6) &&
                    near(clock.tick_at(sample), tick, 1e-6),
                "the clock inverts itself across the ramp at tick " + std::to_string(tick));
    }
    require(near(clock.bpm_at_sample(clock.sample_at(1920)), 120.0, 1e-6),
            "the clock knows the tempo halfway up the ramp");
    // A ramp down to a point with the same tempo is a constant tempo.
    TempoMap flat;
    flat.points = {{0, 100.0, true}, {960, 100.0, false}};
    require(flat.seconds_at(960, 480) == 1.2, "a flat ramp is a constant tempo");
}

// Scenario: Tempo points are edited, and a map that makes no sense is refused.
void tempo_map_edits() {
    TempoMap tempo;
    tempo.set({960, 90.0, false});
    tempo.set({480, 100.0, true});
    tempo.set({960, 95.0, false});   // replaces the point at 960
    require(tempo.points.size() == 3 && tempo.points[1].at == 480 && tempo.points[2].bpm == 95.0,
            "points stay sorted and a tick holds one point");
    require(!tempo.remove(0) && tempo.points.front().at == 0, "the point at tick 0 stays");
    require(!tempo.remove(123), "removing a missing point changes nothing");
    require(tempo.remove(480) && tempo.points.size() == 2, "a point is removed");
    TempoMap bad = tempo;
    bad.points[1].bpm = 301.0;
    require(!bad.valid(), "a tempo above 300 BPM is refused");
    bad = tempo;
    bad.points[1].bpm = 19.0;
    require(!bad.valid(), "a tempo below 20 BPM is refused");
    bad = tempo;
    bad.points.front().at = 10;
    require(!bad.valid(), "a map must start at tick 0");
    bad = tempo;
    bad.points[1].at = 0;
    require(!bad.valid(), "two points on one tick are refused");
    bad.points.clear();
    require(!bad.valid(), "an empty map is refused");
    // The song refuses an invalid map, and so does the engine.
    Song song = song_of(beats(1920, 480, 240, 4));
    song.tempo.points.front().bpm = std::nan("");
    std::string why;
    require(!song.consistent(&why) && why.find("tempo") != std::string::npos,
            "an inconsistent tempo map makes an inconsistent song");
    SongEngine engine;
    require(!engine.prepare(song, 48000.0, 512), "the engine refuses an invalid tempo map");
}

// Scenario: A meter map measures bars of different lengths.
void meter_map() {
    MeterMap meter;
    require(meter.valid() && meter.bar_length(0) == 1920 && meter.bar_start(3) == 5760,
            "the default meter is 4/4");
    meter.set({1, 7, 8});
    meter.set({3, 3, 4});
    require(meter.valid() && meter.changes.size() == 3, "meters take effect at bars");
    require(meter.bar_length(0) == 1920 && meter.bar_length(1) == 1680 &&
                meter.bar_length(2) == 1680 && meter.bar_length(3) == 1440,
            "each bar lasts its meter");
    require(meter.bar_start(0) == 0 && meter.bar_start(1) == 1920 && meter.bar_start(2) == 3600 &&
                meter.bar_start(3) == 5280 && meter.bar_start(4) == 6720,
            "bars start where the ones before them end");
    require(meter.bar_at(0) == 0 && meter.bar_at(1919) == 0 && meter.bar_at(1920) == 1 &&
                meter.bar_at(3599) == 1 && meter.bar_at(3600) == 2 && meter.bar_at(5280) == 3 &&
                meter.bar_at(6720) == 4 && meter.bar_at(-5) == 0,
            "a tick is in the bar that covers it");
    require(meter.beat_length(100) == 480 && meter.beat_length(2000) == 240 &&
                meter.beat_length(5300) == 480,
            "a beat is the meter's denominator");
    // "3.1.1" is the downbeat of bar 3, the first 7/8 bar after the change.
    require(meter.position_at(3600) == MeterMap::Position{2, 0, 0}, "the downbeat of bar 3");
    // Tick 3600 + 3 eighths + a sixteenth: beat 4, second sixteenth.
    require(meter.position_at(3600 + 720 + 120) == MeterMap::Position{2, 3, 1},
            "beats in 7/8 are eighths");
    require(meter.position_at(720) == MeterMap::Position{0, 1, 2}, "beats in 4/4 are quarters");
    MeterMap bad = meter;
    bad.changes[1].denominator = 6;
    require(!bad.valid(), "a denominator must be a power of two");
    bad = meter;
    bad.changes[1].numerator = 0;
    require(!bad.valid(), "a bar has at least one beat");
    bad = meter;
    bad.changes.front().bar = 1;
    require(!bad.valid(), "the map starts at bar 0");
}

// Scenario: Changing a meter keeps clips and tempo points on their bars.
void rebar_case() {
    Song song = song_of(beats(960, 480, 240, 2), {0, 1920, 3840, 3840 + 480});
    song.tracks.push_back(Track{});
    song.audio_files = {{"/a.wav", 48000, 48000, 1}};
    song.audio_clips = {{1, 1, 0, 5760 + 1800, 0, 1000, 0.0, 0, 0}};
    song.tempo.points = {{0, 120.0, false}, {3840, 90.0, false}, {5760 + 240, 100.0, false}};
    const MeterMap before = song.meter;
    song.meter.set({1, 7, 8});
    rebar(song, before);
    // Bar 1 keeps 1920; bar 2 moves from 3840 to 3600; the offset into bar 2
    // (480) is kept; bar 3 was 5760, is now 5280.
    require(song.clips[0].start == 0 && song.clips[1].start == 1920 &&
                song.clips[2].start == 3600 && song.clips[3].start == 4080,
            "clips keep their bar numbers and their place in the bar");
    require(song.tempo.points ==
                std::vector<TempoPoint>{{0, 120.0, false}, {3600, 90.0, false}, {5280 + 240, 100.0, false}},
            "tempo points keep their bars");
    // 1800 ticks into bar 3 does not fit a bar of 1680: it lands on its last tick.
    require(song.audio_clips[0].start == 5280 + 1679,
            "an offset past the end of the shorter bar is clamped to the bar");
    require(song.consistent(), "the rebarred song is consistent");
    // And back: a 4/4 song again puts every clip where it was.
    const MeterMap seven_eight = song.meter;
    song.meter = MeterMap{};
    rebar(song, seven_eight);
    require(song.clips[2].start == 3840 && song.clips[3].start == 4320 &&
                song.tempo.points[1].at == 3840,
            "rebarring back restores bar positions");
}

// Scenario: An audio clip's end is placed in seconds through the tempo map.
void song_length_audio() {
    Song song = song_of(beats(1920, 480, 240, 1));
    song.tempo.points = {{0, 120.0, false}, {1920, 60.0, false}};
    // A two-second clip at 48 kHz starting on beat 3 of bar 1 (tick 960, one
    // second in): one second at 120 BPM reaches tick 1920, then one second at
    // 60 BPM is another 480 ticks.
    song.audio_files = {{"/two-seconds.wav", 96000, 48000, 2}};
    song.audio_clips = {{1, 0, 0, 960, 0, 96000, 0.0, 0, 0}};
    require(song.length() == 2400,
            "the clip ends at tick 2400, got " + std::to_string(song.length()));
    // Halve the tempo of the first bar too: it now ends a second earlier.
    song.tempo.points.front().bpm = 60.0;
    require(song.length() == 960 + 960, "the clip follows the tempo it plays through");
}

// ---- The engine ------------------------------------------------------------------

// Scenario: Notes after a tempo step sound at the new tempo.
void engine_step() {
    Song song = song_of(beats(3840, 480, 240, 8));
    song.tempo.points = {{0, 120.0, false}, {1920, 60.0, false}};
    std::uint64_t length = 0;
    const auto audio = render_clap(song, &length);
    // 50 samples a tick for the first bar, 100 after it.
    const std::vector<long long> expected{0, 24000, 48000, 72000, 96000, 144000, 192000, 240000};
    const auto found = onsets(audio);
    require(length == 96000 + 192000, "the song lasts 2 s + 4 s: " + std::to_string(length));
    require(found == expected, "beats sound at " + list(found) + ", wanted " + list(expected));
    // And each note lasts its own tempo: 240 ticks is 12000 then 24000 samples.
    require(audio[11999] > 0.1F && audio[12000] == 0.0F && audio[144000 + 23999] > 0.1F &&
                audio[144000 + 24000] == 0.0F,
            "note lengths follow the tempo");
}

// Scenario: Notes on a ramp sound where the ramp puts them.
void engine_ramp() {
    Song song = song_of(beats(3840, 240, 120, 16));
    song.tempo.points = {{0, 60.0, true}, {3840, 180.0, false}};
    const auto audio = render_clap(song);
    const auto found = onsets(audio);
    require(found.size() == 16, "sixteen notes sound on the ramp: " + list(found));
    for (std::size_t index = 0; index < found.size(); ++index) {
        const double seconds =
            integrate_seconds(song.tempo.points, static_cast<double>(index * 240), 480.0);
        const auto expected = static_cast<long long>(std::floor(seconds * 48000.0));
        require(std::llabs(found[index] - expected) <= 1,
                "note " + std::to_string(index) + " sounds at " + std::to_string(found[index]) +
                    ", wanted " + std::to_string(expected));
    }
    // The gaps shrink as the tempo rises.
    require(found[1] - found[0] > found[15] - found[14] + 10000, "the ramp accelerates");
}

// Scenario: A 7/8 bar is shorter, and clips placed on bars follow it.
void engine_seven_eight() {
    Song song = song_of(beats(480, 480, 120, 1));
    song.meter.set({1, 7, 8});
    song.clips.clear();
    for (std::int32_t bar = 0; bar < 4; ++bar)
        song.clips.push_back({0, 0, song.meter.bar_start(bar), 1});
    const auto audio = render_clap(song);
    // Bars start at ticks 0, 1920, 3600, 5280: 50 samples a tick.
    const std::vector<long long> expected{0, 96000, 180000, 264000};
    const auto found = onsets(audio);
    require(found == expected, "downbeats sound at " + list(found) + ", wanted " + list(expected));
}

// Scenario: A seek taken with a tempo change lands where it was sent, and a
// stopped playhead keeps its tick across one.
void engine_playhead() {
    Song song = song_of(beats(3840, 480, 240, 8));
    SongEngine engine;
    engine.set_instrument(0, clap_fixture());
    std::string error;
    require(engine.prepare(song, 48000.0, 256, 0, &error), "prepare: " + error);
    std::vector<float> left(256), right(256);
    engine.set_playing(true);
    for (int call = 0; call < 10; ++call) engine.process({left, right});
    require(engine.sample_position() == 2560, "the engine played");
    // A new tempo and a seek in the same block: the seek was placed with the
    // new clock (tick 960 at 60 BPM is sample 96000) and is not moved again.
    song.tempo.points.front().bpm = 60.0;
    require(engine.recompile(song, 0, &error), "recompile: " + error);
    require(engine.published_clock().sample_at(960) == 96000.0,
            "the published clock is the new one");
    engine.seek(96000);
    engine.process({left, right});
    require(engine.sample_position() == 96000 + 256,
            "the seek is kept: " + std::to_string(engine.sample_position()));
    // Stopped, resting on tick 962.56: a tempo change to 240 BPM (25 samples a
    // tick) keeps the playhead there.
    engine.set_playing(false);
    engine.process({left, right});
    const auto resting = engine.sample_position();
    require(resting == 96256, "the playhead rests where the seek put it");
    song.tempo.points.front().bpm = 240.0;
    require(engine.recompile(song, 0, &error), "recompile: " + error);
    engine.process({left, right});
    // Tick 962.56 at 25 samples a tick.
    require(engine.sample_position() == 24064,
            "a stopped playhead keeps its tick: " + std::to_string(engine.sample_position()));
    // A recompile that leaves the tempo alone leaves the playhead alone.
    engine.set_playing(true);
    engine.process({left, right});
    const auto playing = engine.sample_position();
    (void)song.patterns[0].pattern.remove(song.patterns[0].pattern.events().back().id);
    require(engine.recompile(song, 0, &error), "recompile: " + error);
    engine.process({left, right});
    require(engine.sample_position() == playing + 256, "an edit does not move the playhead");
}

// Scenario: The bounce of a song with a tempo step and a ramp is what plays.
void bounce_across_tempo() {
    Song song = song_of(beats(3840, 480, 240, 8), {0, 3840});
    song.tempo.points = {{0, 120.0, false}, {1920, 60.0, true}, {5760, 150.0, false}};
    std::uint64_t length = 0;
    const auto live = render_clap(song, &length);

    SongEngine engine;
    engine.set_instrument(0, clap_fixture());
    std::string error;
    require(engine.prepare(song, 48000.0, 512, 0, &error), "prepare: " + error);
    const auto file = std::filesystem::temp_directory_path() / "blokkily-timebase-bounce.wav";
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "bounce: " + error);
    const auto read = read_wave(file, &error);
    std::filesystem::remove(file);
    require(read.has_value() && read->channels == 2 && read->sample_rate == 48000,
            "the bounce reads back: " + error);
    require(read->frames == length && live.size() == length,
            "the bounce is the song's length: " + std::to_string(read->frames) + " vs " +
                std::to_string(length));
    std::vector<float> bounced(read->frames);
    for (std::size_t frame = 0; frame < bounced.size(); ++frame)
        bounced[frame] = read->interleaved[frame * 2];
    for (std::size_t frame = 0; frame < bounced.size(); ++frame)
        if (bounced[frame] != live[frame])
            throw std::runtime_error("the bounce differs from the live render at frame " +
                                     std::to_string(frame));
    // And it is not two silent files agreeing: sixteen beats, the fifth on the
    // step (tick 1920 is 96000), the rest where the tempo puts them.
    const auto found = onsets(bounced);
    require(found.size() == 16 && found[4] == 96000,
            "the bounce has every beat across the change: " + list(found));
    for (std::size_t index = 5; index < found.size(); ++index) {
        const double seconds =
            integrate_seconds(song.tempo.points, static_cast<double>(index * 480), 480.0);
        require(std::llabs(found[index] - static_cast<long long>(std::floor(seconds * 48000.0))) <= 1,
                "bounced beat " + std::to_string(index) + " is where the tempo map puts it");
    }
}

// Scenario: A take recorded across a tempo change lands on the ticks played.
void recording_across_tempo() {
    Song song = song_of(Pattern(3840, 480));
    song.tempo.points = {{0, 120.0, false}, {1920, 60.0, false}};
    SongEngine engine;
    engine.set_instrument(0, clap_fixture());
    std::string error;
    require(engine.prepare(song, 48000.0, 1024, 0, &error), "prepare: " + error);
    InputQueue input;
    engine.connect_input(&input);
    engine.set_recording(true);
    engine.set_playing(true);
    Pump pump(engine);
    // Well into bar 2 at 60 BPM: past the step, where the old single-tempo
    // conversion would put the note several steps late.
    (void)pump.left(96000 + 64 * 1024);
    const auto pressed_at = engine.sample_position();
    require(input.push({0, {PluginEvent::Type::note_on, 0, 64, 0.9, 0.0}}), "key down");
    (void)pump.left(1024);
    (void)pump.left(24 * 1024);
    const auto released_at = engine.sample_position();
    require(input.push({0, {PluginEvent::Type::note_off, 0, 64, 0.0, 0.0}}), "key up");
    (void)pump.left(1024);

    std::vector<CapturedEvent> heard;
    CapturedEvent captured;
    while (engine.take_captured(captured)) heard.push_back(captured);
    require(heard.size() == 2 && heard[0].sample == pressed_at && heard[1].sample == released_at,
            "the engine captured the key where it was heard");
    const auto& clock = engine.published_clock();
    const Tick on = tick_at_sample(clock, heard[0].sample);
    const Tick off = tick_at_sample(clock, heard[1].sample);
    // By hand: bar 1 is 96000 samples at 50 a tick, then 100 a tick.
    const auto by_hand = [](std::uint64_t sample) {
        return static_cast<Tick>(1920 + (sample - 96000) / 100);
    };
    require(on == by_hand(pressed_at) && off == by_hand(released_at),
            "the take is read through the tempo map: " + std::to_string(on) + ".." +
                std::to_string(off));
    const auto single_tempo = static_cast<Tick>(static_cast<double>(pressed_at) / 50.0);
    require(single_tempo - on > 4 * 120,
            "the single-tempo reading would have put the note steps away from where it was heard");

    TakeRecorder take(song.length());
    take.note_on(on, 64, 0.9F, 0.0);
    const auto note = take.note_off(off, 64);
    require(note.has_value(), "the key is paired into a note");
    const auto target = take_target(song, 0, note->start, 0);
    PlayedNote placed = *note;
    placed.start = target.offset;
    const int step = write_played(song.patterns[target.pattern].pattern, placed, 120);
    require(step == static_cast<int>(std::lround(static_cast<double>(on) / 120.0)),
            "the note is written on the step it was played in: " + std::to_string(step));

    // Played back, it sounds where it was played, to within the block the key
    // was heard in and the tick it rounded down to.
    require(engine.recompile(song, 0, &error), "recompile: " + error);
    engine.set_recording(false);
    engine.connect_input(nullptr);
    engine.seek(0);
    const auto audio = pump.left(engine.song_samples());
    const auto found = onsets(audio);
    require(found.size() == 1 && found[0] <= static_cast<long long>(pressed_at) &&
                found[0] > static_cast<long long>(pressed_at) - 100,
            "the take plays back where it was heard: " + list(found) + " vs " +
                std::to_string(pressed_at));
}

// Plays one note at tick 1440 of a song whose tempo steps down at tick 960
// on `instrument`, and returns the left channel of the whole pass.
std::vector<float> render_after_step(std::unique_ptr<PluginInstance> instrument) {
    Pattern pattern(1920, 480);
    Trigger trigger;
    trigger.start = 1440;
    trigger.duration = 240;
    trigger.musical_data = Note{60, 1.0F, 0.0F};
    (void)pattern.add(trigger);
    Song song = song_of(std::move(pattern));
    song.tempo.points = {{0, 120.0, false}, {960, 60.0, false}};
    SongEngine engine;
    engine.set_instrument(0, std::move(instrument));
    std::string error;
    require(engine.prepare(song, 48000.0, 512, 0, &error), "prepare: " + error);
    // 960 ticks at 50 samples, 960 at 100: three seconds.
    require(engine.song_samples() == 48000 + 96000, "the pass lasts three seconds");
    engine.set_playing(true);
    Pump pump(engine);
    return pump.left(engine.song_samples());
}

// Scenario: A SoundFont note after a tempo change is heard where it falls.
void soundfont_across_tempo() {
    auto synth = std::make_unique<SoundFontSynth>();
    require(synth->load(BLOKKILY_TEST_SF2_PATH), "the SoundFont must load");
    const auto audio = render_after_step(std::move(synth));
    // Tick 1440: 960 * 50 + 480 * 100 = 96000. At the old constant 120 BPM
    // it would have been 72000.
    const auto first = probe::first_nonzero(audio, 1e-4F);
    require(first.has_value(), "the SoundFont note is not silent");
    require(*first >= 96000 && *first < 96000 + 2048,
            "the SoundFont note starts at the tempo map's sample: " + std::to_string(*first));
    const std::span<const float> held{audio.data() + 96000, 12000};
    require(probe::rms(held) > 1e-3 && probe::peak(held) > 1e-2F,
            "the SoundFont note is heard: rms " + std::to_string(probe::rms(held)));
}

// Scenario: A VST3 note after a tempo change is heard where it falls.
void vst3_across_tempo() {
    auto vst3 = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0);
    require(vst3 != nullptr, "the VST3 fixture must load through the JUCE host adapter");
    const auto audio = render_after_step(std::move(vst3));
    const auto found = onsets(audio);
    require(!found.empty() && found.front() == 96000,
            "the VST3 note starts at the tempo map's sample: " + list(found));
    const std::span<const float> held{audio.data() + 96000, 24000};
    require(probe::rms(held) > 0.1 && audio[96000 + 24000 + 10] == 0.0F,
            "the VST3 note sounds for its 240 ticks at 60 BPM");
}

// ---- The project file ----------------------------------------------------------

// Scenario: Tempo points and meters are saved and loaded.
void project_records() {
    Project project;
    project.name = "Timebase";
    project.song.tempo.points = {{0, 97.5, false}, {1920, 60.0, true}, {5760, 142.25, false}};
    project.song.meter.changes = {{0, 4, 4}, {2, 7, 8}, {5, 3, 4}};
    const auto text = ProjectFile::serialize(project);
    const auto has_line = [&text](const std::string& line) {
        return text.find('\n' + line + '\n') != std::string::npos;
    };
    require(has_line("tempo_point 0 97.5 step") && has_line("tempo_point 1920 60 ramp") &&
                has_line("tempo_point 5760 142.25 step") && has_line("meter 2 7 8") &&
                has_line("meter 0 4 4") && has_line("meter 5 3 4"),
            "the timebase is written:\n" + text);
    require(text.find("\ntempo ") == std::string::npos, "the legacy tempo line is never written");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project reloads: " + error);
    require(loaded->song.tempo == project.song.tempo && loaded->song.meter == project.song.meter,
            "the timebase reloads as it was");
    require(ProjectFile::serialize(*loaded) == text, "a reloaded project saves byte-identically");

    const std::string body = "pattern P 1920 480\ntrack T 0 0 0 0 CLAP /a.clap ~ ~\n";
    const auto parse = [&](const std::string& records) {
        error.clear();
        return ProjectFile::parse("blokkily-project 5\n" + body + records, &error);
    };
    // A file with no timebase records plays at 120 BPM in 4/4.
    const auto bare = parse("");
    require(bare.has_value() && bare->song.tempo == TempoMap{} && bare->song.meter == MeterMap{},
            "a file without timebase records has the default timebase");
    // The legacy tempo line is one point at tick 0, in format 4 and 5.
    for (const char* version : {"4", "5"}) {
        const auto legacy = ProjectFile::parse(std::string("blokkily-project ") + version +
                                                   "\ntempo 133\n" + body,
                                               &error);
        require(legacy.has_value() &&
                    legacy->song.tempo.points == std::vector<TempoPoint>{{0, 133.0, false}},
                std::string("legacy tempo in format ") + version + ": " + error);
    }
    // Refusals, each loud.
    const std::map<std::string, std::string> refused{
        {"tempo 120\ntempo_point 0 120 step\n", "cannot be mixed"},
        {"tempo_point 0 120 step\ntempo 120\n", "cannot be mixed"},
        {"tempo 120\ntempo 90\n", "more than one tempo"},
        {"tempo 500\n", "outside 20 to 300"},
        {"tempo_point 480 120 step\n", "not at tick 0"},
        {"tempo_point 0 120 step\ntempo_point 0 90 step\n", "share a tick"},
        {"tempo_point 0 120 glide\n", "unknown tempo_point shape"},
        {"tempo_point 0 350 step\n", "outside 20 to 300"},
        {"tempo_point -1 120 step\n", "malformed tempo_point"},
        {"tempo_point 0 120\n", "malformed tempo_point"},
        {"meter 1 7 8\n", "not at bar 0"},
        {"meter 0 7 6\n", "power of two"},
        {"meter 0 0 4\n", "malformed meter"},
        {"meter 0 4 4\nmeter 0 3 4\n", "share a bar"},
        {"meter 0 4\n", "malformed meter"},
    };
    for (const auto& [records, reason] : refused) {
        const auto result = parse(records);
        require(!result.has_value() && error.find(reason) != std::string::npos,
                "\"" + records + "\" must be refused with \"" + reason + "\", got \"" + error + "\"");
    }
    // Records may come in any order; the maps are sorted.
    const auto shuffled = parse("tempo_point 960 90 step\ntempo_point 0 120 ramp\nmeter 3 5 4\nmeter 0 4 4\n");
    require(shuffled.has_value() && shuffled->song.tempo.points.front().ramp &&
                shuffled->song.tempo.points.back().at == 960 &&
                shuffled->song.meter.changes.back().bar == 3,
            "records in any order load sorted: " + error);
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> cases{
        {"tempo_map_constant", tempo_map_constant},
        {"tempo_map_step", tempo_map_step},
        {"tempo_map_ramp", tempo_map_ramp},
        {"tempo_map_edits", tempo_map_edits},
        {"meter_map", meter_map},
        {"rebar", rebar_case},
        {"song_length_audio", song_length_audio},
        {"engine_step", engine_step},
        {"engine_ramp", engine_ramp},
        {"engine_seven_eight", engine_seven_eight},
        {"engine_playhead", engine_playhead},
        {"bounce_across_tempo", bounce_across_tempo},
        {"recording_across_tempo", recording_across_tempo},
        {"soundfont_across_tempo", soundfont_across_tempo},
        {"vst3_across_tempo", vst3_across_tempo},
        {"project_records", project_records},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_timebase_tests <case>\n";
        for (const auto& [name, run] : cases) std::cerr << "  " << name << '\n';
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& failure) {
        std::cerr << "TIMEBASE FAIL " << argv[1] << ": " << failure.what() << '\n';
        return 1;
    }
    std::cout << "TIMEBASE PASS " << argv[1] << '\n';
    return 0;
}
