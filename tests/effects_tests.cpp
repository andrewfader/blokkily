// Effects (plan item 2.4), each proved from rendered audio through the
// production SongEngine::process(), with the real CLAP and VST3 effect
// fixtures (built by the suite and loaded through their official entry points)
// and the built-in effects, all created through the production processor
// factory. Mixer claims are read off the bus; the bounce is read back from the
// file and compared with the engine's own render.
//
// Run with a case name; each case is its own CTest test (effects_<case>).
// features/effects.feature maps its scenarios to these cases.

#include "audio/engine/test_access.hpp"
#include "engine_graph.hpp"
#include "processor_factory.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/effects/builtin.hpp"
#include "blokkily/plugins/plugin_scan.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;
// One tick per sample: 24000 ticks a beat at 120 BPM and 48 kHz.
constexpr Tick ticks_per_beat = 24000;
// A centred strip's gain on each side (constant-power pan).
const float centre = static_cast<float>(std::cos(std::numbers::pi / 4.0));

PluginSlot clap_effect() {
    return {"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}};
}
PluginSlot vst3_effect() { return {"VST3", BLOKKILY_TEST_VST3_EFFECT_PATH, "", {}}; }
PluginSlot builtin(const char* identifier) {
    return {std::string(builtin_effect_format), "", identifier, {}};
}
EffectSlot insert(PluginSlot plugin) { return {std::move(plugin), false, {}}; }

// The CLAP effect fixture's state: its tag and its gain.
std::vector<std::byte> clap_effect_state(double gain) {
    struct State {
        std::array<char, 4> tag;
        double gain;
    };
    State state{};
    std::memset(&state, 0, sizeof state);
    state.tag = {'B', 'K', 'F', 'E'};
    state.gain = gain;
    std::vector<std::byte> bytes(sizeof state);
    std::memcpy(bytes.data(), &state, sizeof state);
    return bytes;
}

Song song_of(std::size_t tracks, Tick length = 16384) {
    Song song;
    song.patterns = {{"Effects", Pattern(length, ticks_per_beat)}};
    song.tracks.assign(tracks, Track{});
    for (std::size_t track = 0; track < tracks; ++track)
        song.tracks[track].name = "T" + std::to_string(track);
    song.clips = {{0, 0, 0, 1}};
    return song;
}

// Sources standing in for audio clips (chunk stage 5), so a track's input is
// known exactly: a steady level, or single-sample impulses.
void dc_source(void* context, StereoBlock track, std::uint64_t) noexcept {
    const float level = *static_cast<const float*>(context);
    for (auto& sample : track.left) sample += level;
    for (auto& sample : track.right) sample += level;
}
struct Impulses {
    std::vector<std::uint64_t> at;
    float level = 1.0F;
};
void impulse_source(void* context, StereoBlock track, std::uint64_t position) noexcept {
    const auto& impulses = *static_cast<const Impulses*>(context);
    for (const auto at : impulses.at)
        if (at >= position && at < position + track.left.size()) {
            track.left[at - position] += impulses.level;
            track.right[at - position] += impulses.level;
        }
}

// Puts every processor the song names into `engine` through the production
// factory, prepares it, and loads the saved state of what was created.
void build(SongEngine& engine, const Song& song) {
    const auto graph = populate_graph(engine, song, {}, {});
    require(graph.error.empty(), "every processor must load: " + graph.error);
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "the song must prepare: " + error);
    load_fresh_state(engine, song, graph);
}

struct Stereo {
    std::vector<float> left, right;
};
// The production callback, `frames` long, in blocks the size a device asks.
Stereo render(SongEngine& engine, std::size_t frames) {
    Stereo out{std::vector<float>(frames, 0.0F), std::vector<float>(frames, 0.0F)};
    for (std::size_t start = 0; start < frames; start += block) {
        const auto count = std::min<std::size_t>(block, frames - start);
        engine.process({std::span(out.left).subspan(start, count),
                        std::span(out.right).subspan(start, count)});
    }
    return out;
}

bool near(float actual, float expected, float tolerance = 1e-5F) {
    return std::abs(actual - expected) <= tolerance;
}
bool span_is(std::span<const float> samples, float expected, float tolerance = 1e-5F) {
    return std::all_of(samples.begin(), samples.end(),
                       [&](float sample) { return near(sample, expected, tolerance); });
}
std::string show(float value) { return std::to_string(value); }

// Where the samples louder than `threshold` are.
std::vector<std::size_t> loud_samples(const std::vector<float>& samples,
                                      float threshold = 1e-6F) {
    std::vector<std::size_t> found;
    for (std::size_t index = 0; index < samples.size(); ++index)
        if (std::abs(samples[index]) > threshold) found.push_back(index);
    return found;
}

// --- cases -------------------------------------------------------------------

// Scenario: An insert chain processes the track in order
void insert_chain() {
    auto song = song_of(1);
    song.tracks[0].inserts = {insert(clap_effect()), insert(vst3_effect())};
    SongEngine engine;
    float level = 1.0F;
    build(engine, song);
    engine::TestAccess::set_test_source(engine, 0, dc_source, &level);
    require(engine.output_latency() == 96,
            "the chain's latency is the CLAP 64 plus the VST3 32, not " +
                std::to_string(engine.output_latency()));
    engine.set_playing(true);
    const auto out = render(engine, 2048);
    require(span_is(std::span(out.left).first(96), 0.0F), "nothing before the chain's latency");
    require(span_is(std::span(out.left).subspan(96), -0.0625F * centre) &&
                span_is(std::span(out.right).subspan(96), -0.0625F * centre),
            "a level of 1 comes out at 0.25 x -0.25 = -0.0625 through the strip, not " +
                show(out.left.back() / centre));
}

// Scenario: Bypass is live and keeps the chain's latency
void bypass_live() {
    auto song = song_of(1);
    song.tracks[0].inserts = {insert(clap_effect()), insert(vst3_effect())};
    SongEngine engine;
    Impulses impulses{{2000, 6000, 10000}, 1.0F};
    build(engine, song);
    engine::TestAccess::set_test_source(engine, 0, impulse_source, &impulses);
    auto* clap = engine.processor({BusKind::track, 0, 0});
    auto* vst3 = engine.processor({BusKind::track, 0, 1});
    require(clap != nullptr && vst3 != nullptr, "both inserts are addressable");
    engine.set_playing(true);
    auto first = render(engine, 4096);
    // The live move: only the mixer, no prepare, the same instances.
    song.tracks[0].inserts[0].bypass = true;
    engine.apply_mix(song);
    auto second = render(engine, 4096);
    song.tracks[0].inserts[0].bypass = false;
    engine.apply_mix(song);
    auto third = render(engine, 4096);
    require(engine.processor({BusKind::track, 0, 0}) == clap &&
                engine.processor({BusKind::track, 0, 1}) == vst3,
            "bypassing swaps no processor");
    require(engine.output_latency() == 96, "bypass leaves the compensation alone");
    require(loud_samples(first.left) == std::vector<std::size_t>{2096} &&
                near(first.left[2096], -0.0625F * centre),
            "unbypassed, the impulse at 2000 comes out once at 2096 at -0.0625");
    require(loud_samples(second.left) == std::vector<std::size_t>{6096 - 4096} &&
                near(second.left[6096 - 4096], -0.25F * centre),
            "with the CLAP effect bypassed the impulse at 6000 still comes out at 6096, "
            "only the VST3's -0.25 applied");
    require(loud_samples(third.left) == std::vector<std::size_t>{10096 - 8192} &&
                near(third.left[10096 - 8192], -0.0625F * centre),
            "bypass released, the chain is whole again at the same latency");
}

// Scenario: Sends feed a return post-fader and pre-pan, and follow their level
void sends_and_returns() {
    auto song = song_of(1);
    song.returns = {ReturnBus{}};
    song.tracks[0].mix.pan = -1.0; // hard left: the direct right channel is silent
    song.tracks[0].sends = {{0, 0.0, false}};
    SongEngine engine;
    float level = 1.0F;
    build(engine, song);
    engine::TestAccess::set_test_source(engine, 0, dc_source, &level);
    engine.set_playing(true);
    const auto settle = [&] {
        const auto out = render(engine, 512);
        return std::pair{out.left.back(), out.right.back()};
    };
    auto [left, right] = settle();
    require(near(left, 1.0F + centre) && near(right, centre),
            "a post-fader send is taken before pan: the return hears both sides; got " +
                show(left) + ", " + show(right));
    require(engine.return_peak(0) > 0.0F, "the return's meter moves");

    song.tracks[0].sends[0].level_db = -6.0206;
    engine.apply_mix(song);
    std::tie(left, right) = settle();
    require(near(right, 0.5F * centre, 1e-4F), "the send level is live: -6 dB halves the return");

    song.tracks[0].mix.gain_db = -6.0206;
    engine.apply_mix(song);
    std::tie(left, right) = settle();
    require(near(left, 0.5F + 0.25F * centre, 1e-4F) && near(right, 0.25F * centre, 1e-4F),
            "a post-fader send follows the fader");

    song.tracks[0].sends[0].pre_fader = true;
    engine.apply_mix(song);
    std::tie(left, right) = settle();
    require(near(right, 0.5F * centre, 1e-4F), "a pre-fader send ignores the fader");

    song.tracks[0].mix.mute = true;
    engine.apply_mix(song);
    std::tie(left, right) = settle();
    require(left == 0.0F && right == 0.0F, "a muted track sends nothing");

    song.tracks[0].mix.mute = false;
    song.returns[0].mix.mute = true;
    engine.apply_mix(song);
    std::tie(left, right) = settle();
    require(near(left, 0.5F) && right == 0.0F, "a muted return is silent; the direct path is not");
}

// Scenario: Returns are solo-safe
void solo_safe_returns() {
    auto song = song_of(2);
    song.returns = {ReturnBus{}};
    song.tracks[0].sends = {{0, 0.0, false}};
    song.tracks[1].sends = {{0, 0.0, false}};
    SongEngine engine;
    float loud = 1.0F;
    float quiet = 0.5F;
    build(engine, song);
    engine::TestAccess::set_test_source(engine, 0, dc_source, &loud);
    engine::TestAccess::set_test_source(engine, 1, dc_source, &quiet);
    engine.set_playing(true);
    require(near(render(engine, 512).left.back(), 3.0F * centre),
            "both tracks and the return they feed: (1 + 0.5) x 2");
    song.tracks[1].mix.solo = true;
    engine.apply_mix(song);
    const float soloed = render(engine, 512).left.back();
    require(near(soloed, 1.0F * centre),
            "soloing track 1 silences track 0 and its send, but not the return: "
            "0.5 direct + 0.5 through the return; got " + show(soloed / centre));
    require(engine.track_peak(0) == 0.0F, "the unsoloed track is not heard");
}

// Scenario: Plugin delay compensation aligns every path
void delay_compensation() {
    auto song = song_of(3);
    song.returns = {ReturnBus{}};
    song.returns[0].inserts = {insert(clap_effect())};
    song.tracks[1].inserts = {insert(clap_effect())};
    song.tracks[2].inserts = {insert(vst3_effect())};
    song.tracks[0].sends = {{0, 0.0, false}};
    SongEngine engine;
    Impulses impulses{{1000}, 1.0F};
    build(engine, song);
    for (std::size_t track = 0; track < 3; ++track)
        engine::TestAccess::set_test_source(engine, track, impulse_source, &impulses);
    require(engine.output_latency() == 128,
            "the slowest track (64) plus the slowest return (64), not " +
                std::to_string(engine.output_latency()));
    engine.set_playing(true);
    const auto out = render(engine, 4096);
    // Track 0 direct (1) + track 1 through its CLAP (0.25) + track 2 through
    // its VST3 (-0.25) + track 0's send through the return's CLAP (0.25).
    const auto loud = loud_samples(out.left);
    require(loud == std::vector<std::size_t>{1128},
            "every path's impulse lands on the same sample, 1000 + 128; " +
                std::to_string(loud.size()) + " loud samples, first at " +
                (loud.empty() ? std::string("none") : std::to_string(loud.front())));
    require(near(out.left[1128], 1.25F * centre), "all four paths sum there: 1.25, not " +
                                                     show(out.left[1128] / centre));
}

// Scenario: The master bus has inserts of its own
void master_insert() {
    auto song = song_of(1);
    song.master_inserts = {insert(vst3_effect())};
    SongEngine engine;
    float level = 1.0F;
    build(engine, song);
    engine::TestAccess::set_test_source(engine, 0, dc_source, &level);
    require(engine.output_latency() == 32, "the master insert's latency is the output's");
    engine.set_playing(true);
    auto out = render(engine, 1024);
    require(span_is(std::span(out.left).first(32), 0.0F) &&
                span_is(std::span(out.left).subspan(32), -0.25F * centre),
            "the whole mix passes through the master's VST3 effect");
    song.master_gain_db = -6.0206;
    song.master_inserts[0].bypass = true;
    engine.apply_mix(song);
    out = render(engine, 1024);
    require(span_is(std::span(out.left).subspan(64), 0.5F * centre, 1e-4F),
            "bypassed live, the master insert passes the mix; the master gain follows it");
}

// A song using every effects record: inserts on a track, a return and the
// master, a send, a bypass, and a CLAP effect with state of its own.
Song effects_song() {
    auto song = song_of(2);
    song.returns = {ReturnBus{"ECHO", {insert(builtin("delay"))}, {-3.0, 0.25, false, false}}};
    song.tracks[0].inserts = {insert(clap_effect()), insert(builtin("eq3"))};
    song.tracks[0].inserts[0].plugin.state = clap_effect_state(0.5);
    song.tracks[1].inserts = {insert(vst3_effect())};
    song.tracks[1].inserts[0].bypass = true;
    song.tracks[0].sends = {{0, -6.0, false}};
    song.tracks[1].sends = {{0, -12.0, true}};
    song.master_inserts = {insert(builtin("compressor"))};
    return song;
}

// Scenario: A saved project plays its effects the same after loading
void project_records() {
    const auto original = effects_song();
    Project project;
    project.song = original;
    const auto text = ProjectFile::serialize(project);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project parses back: " + error);
    require(ProjectFile::serialize(*loaded) == text, "the text round-trips byte for byte");

    Impulses impulses{{500, 3000}, 0.5F};
    const auto play = [&](const Song& song) {
        SongEngine engine;
        build(engine, song);
        for (std::size_t track = 0; track < song.tracks.size(); ++track)
            engine::TestAccess::set_test_source(engine, track, impulse_source, &impulses);
        engine.set_playing(true);
        return render(engine, 16384);
    };
    const auto before = play(original);
    const auto after = play(loaded->song);
    require(probe::rms(before.left) > 1e-3, "the effects song is heard");
    require(before.left == after.left && before.right == after.right,
            "the loaded song renders exactly what the saved one did");
    // The saved CLAP gain is the one heard: 0.5, not the default 0.25.
    auto only_clap = song_of(1);
    only_clap.tracks[0].inserts = {loaded->song.tracks[0].inserts[0]};
    float level = 1.0F;
    SongEngine engine;
    build(engine, only_clap);
    engine::TestAccess::set_test_source(engine, 0, dc_source, &level);
    engine.set_playing(true);
    require(near(render(engine, 512).left.back(), 0.5F * centre),
            "the effect's saved state is loaded into it");
}

// Scenario: A bounce drops the compensation and keeps the tail
void bounce_trims_latency() {
    auto song = song_of(1, 8000);
    song.tracks[0].inserts = {insert(clap_effect()), insert(builtin("delay"))};
    Impulses impulses{{0, 4000}, 1.0F};
    SongEngine engine;
    build(engine, song);
    engine::TestAccess::set_test_source(engine, 0, impulse_source, &impulses);
    const auto latency = engine.output_latency();
    const auto tail = engine.effect_tail_samples();
    require(latency == 64, "the chain's latency is the CLAP effect's 64");
    require(tail > 48000, "the delay reports a tail of seconds");

    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "effects-bounce.wav";
    std::filesystem::create_directories(file.parent_path());
    std::string error;
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value() && wave->channels == 2, "the bounce reads back: " + error);
    require(wave->frames == engine.song_samples() + tail,
            "the file is the song plus the effect tail, with no compensation in front");

    // The same song rendered live through a fresh engine, the compensation
    // dropped afterwards: the file must be exactly that.
    SongEngine live;
    build(live, song);
    engine::TestAccess::set_test_source(live, 0, impulse_source, &impulses);
    live.set_playing(true);
    auto played = render(live, static_cast<std::size_t>(live.song_samples()));
    live.set_playing(false);
    const auto after = render(live, static_cast<std::size_t>(latency + tail));
    played.left.insert(played.left.end(), after.left.begin(), after.left.end());
    played.right.insert(played.right.end(), after.right.begin(), after.right.end());
    bool same = true;
    for (std::size_t frame = 0; frame < wave->frames && same; ++frame)
        same = wave->interleaved[2 * frame] == played.left[frame + latency] &&
               wave->interleaved[2 * frame + 1] == played.right[frame + latency];
    require(same, "the bounce is the live render with the latency trimmed, sample for sample");

    std::vector<float> left(wave->frames);
    for (std::size_t frame = 0; frame < wave->frames; ++frame)
        left[frame] = wave->interleaved[2 * frame];
    const auto loud = loud_samples(left);
    require(!loud.empty() && loud.front() == 0,
            "the first impulse is at sample 0 of the file, not at the latency");
    const auto song_end = static_cast<std::size_t>(engine.song_samples());
    require(probe::peak(std::span(left).subspan(song_end)) > 0.01F,
            "the echo past the song's end is in the file");
    // The echo of the impulse at 4000, 250 ms later, lies in the tail.
    require(std::abs(left[4000 + 12000]) > 0.01F, "the delay's echo is where it belongs");
}

// Scenario: A tempo-synced delay follows a tempo change
void tempo_synced_delay() {
    auto song = song_of(1, 96000);
    song.tempo.points = {{0, 120.0, false}, {48000, 90.0, false}};
    // The built-in delay, synced to half a beat, one clean echo at half level.
    auto echo = create_builtin_effect("delay");
    require(echo && echo->activate(rate, 1, block), "the built-in delay activates");
    const std::array<PluginEvent, 4> settings{{
        {PluginEvent::Type::parameter_value, 0, delay::sync, 1.0},
        {PluginEvent::Type::parameter_value, 0, delay::beats, 0.5},
        {PluginEvent::Type::parameter_value, 0, delay::feedback, 0.0},
        {PluginEvent::Type::parameter_value, 0, delay::mix, 0.5},
    }};
    std::vector<float> scratch(block, 0.0F), scratch_right(block, 0.0F);
    echo->process({scratch, scratch_right}, settings);
    auto slot = builtin("delay");
    slot.state = echo->save_state();
    song.tracks[0].inserts = {insert(slot)};

    SongEngine engine;
    Impulses impulses{{1000, 60000}, 1.0F};
    build(engine, song);
    engine::TestAccess::set_test_source(engine, 0, impulse_source, &impulses);
    engine.set_playing(true);
    const auto out = render(engine, 90000);
    const auto loud = loud_samples(out.left);
    // Half a beat is 12000 samples at 120 BPM and 16000 at 90.
    const std::vector<std::size_t> expected{1000, 13000, 60000, 76000};
    require(loud == expected,
            "echoes at 1000 + 12000 before the tempo change and 60000 + 16000 after it; got " +
                std::to_string(loud.size()) + " loud samples");
    for (const auto at : expected)
        require(near(out.left[at], 0.5F * centre), "each dry and echo is at half level");
}

// A probe insert: records the transport it is told before each block.
class TransportProbe final : public PluginInstance {
public:
    struct Seen {
        TransportInfo transport;
        std::size_t frames;
    };
    std::array<Seen, 64> seen{};
    std::size_t count = 0;
    bool activate(double, std::uint32_t, std::uint32_t) override { return true; }
    void set_transport(const TransportInfo& transport) noexcept override { last_ = transport; }
    void process(StereoBlock audio, std::span<const PluginEvent>) noexcept override {
        if (count < seen.size()) seen[count++] = {last_, audio.left.size()};
        edit_pending_ = true;
    }
    // A knob on the effect's own window turned during every block.
    std::size_t take_parameter_edits(std::span<ParameterEdit> out) noexcept override {
        if (!edit_pending_ || out.empty()) return 0;
        edit_pending_ = false;
        out[0] = {ParameterEdit::Kind::value, 7, 0.25, 5};
        return 1;
    }
    std::vector<std::byte> save_state() override { return {}; }
    bool load_state(std::span<const std::byte>) override { return true; }
    std::string format() const override { return "Probe"; }
    PluginPorts ports() const override { return {2, false}; }

private:
    TransportInfo last_{};
    bool edit_pending_ = false;
};

// Scenario: Every processor is told where the song is before each block
void transport_per_chunk() {
    auto song = song_of(1, 192000);
    song.tempo.points = {{0, 120.0, false}, {96000, 60.0, false}};
    song.meter.changes = {{0, 4, 4}, {1, 7, 8}};
    song.tracks[0].inserts = {insert(builtin("eq3"))}; // replaced by the probe below
    SongEngine engine;
    auto probe_owner = std::make_unique<TransportProbe>();
    auto* probe = probe_owner.get();
    engine.set_processor({BusKind::track, 0, 0}, std::move(probe_owner));
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepares: " + error);
    // Beat 1.5 of the song: tick 36000, sample 36000 at 120 BPM.
    engine.seek(36000);
    engine.set_playing(true);
    (void)render(engine, block);
    // Past the tempo change: tick 108000 is 96000 + 12000 ticks at 60 BPM,
    // 2 samples a tick, so sample 96000 + 24000.
    engine.seek(120000);
    (void)render(engine, block);
    engine.set_playing(false);
    (void)render(engine, block);
    require(probe->count == 3, "one transport per block");
    const auto& first = probe->seen[0].transport;
    // Bars are counted by the meter map (whole notes of 1920 ticks), so
    // with this song's 24000 ticks a beat tick 36000 lies well into the 7/8.
    const auto first_bar = song.meter.bar_at(36000);
    require(std::abs(first.bpm - 120.0) < 1e-9 && std::abs(first.beat - 1.5) < 1e-9 &&
                first.bar == first_bar && first.bar > 0 && first.numerator == 7 &&
                first.denominator == 8 && first.playing,
            "at sample 36000: 120 BPM, beat 1.5, the meter map's bar in 7/8, playing; got bar " +
                std::to_string(first.bar) + " of " + std::to_string(first.numerator) + "/" +
                std::to_string(first.denominator));
    const auto& second = probe->seen[1].transport;
    require(std::abs(second.bpm - 60.0) < 1e-9 && std::abs(second.beat - 4.5) < 1e-9,
            "past the change: 60 BPM, beat 4.5 (tick 108000)");
    require(second.bar == song.meter.bar_at(108000) &&
                second.numerator == song.meter.meter_in(second.bar).numerator,
            "the bar and meter come from the song's meter map");
    require(!probe->seen[2].transport.playing, "a stopped song says so");
    // What the insert reported about its own parameters reaches the edit
    // ring, addressed to its slot and stamped where it happened.
    PluginEditEvent edit;
    std::vector<PluginEditEvent> edits;
    while (engine.take_plugin_edit(edit)) edits.push_back(edit);
    require(edits.size() == 3 && edits[0].where == ProcessorAddress{BusKind::track, 0, 0} &&
                edits[0].song_sample == 36000 + 5 && edits[0].rolling &&
                edits[0].edit.parameter == 7 && !edits[2].rolling,
            "each block's edit is in the ring, from track 0 slot 0, at its sample");
}

// Scenario: Inserts and returns are part of the graph; mixer moves are not
void graph_signature_rules() {
    auto song = song_of(1);
    song.returns = {ReturnBus{}};
    song.tracks[0].inserts = {insert(clap_effect())};
    song.returns[0].inserts = {insert(builtin("reverb"))};
    song.master_inserts = {insert(builtin("eq3"))};
    const auto base = graph_signature(song);
    require(base.processors.size() == 4 && base.returns == 1,
            "the instrument, the track insert, the return insert and the master insert");
    require(base.at({BusKind::track, 0, 0}) != nullptr &&
                *base.at({BusKind::track, 0, 0}) == identity_of(clap_effect()) &&
                base.at({BusKind::ret, 0, 0}) != nullptr && base.at({BusKind::master, 0, 0}),
            "each insert is found at its processor address");
    auto moved = song;
    moved.tracks[0].inserts[0].bypass = true;
    moved.tracks[0].sends = {{0, -3.0, true}};
    moved.returns[0].mix.gain_db = -10.0;
    moved.tracks[0].inserts[0].plugin.state = clap_effect_state(0.9);
    require(graph_signature(moved) == base, "bypass, sends, levels and state are live moves");
    auto added = song;
    added.tracks[0].inserts.push_back(insert(builtin("delay")));
    require(!(graph_signature(added) == base), "an insert added changes the graph");
    auto swapped = song;
    swapped.master_inserts[0].plugin.identifier = "compressor";
    require(!(graph_signature(swapped) == base), "an insert swapped changes the graph");

    // The engine holds every address the signature names.
    SongEngine engine;
    build(engine, song);
    for (const auto& [address, identity] : base.processors)
        require(identity.empty() || engine.processor(address) != nullptr,
                "the engine holds a processor at every named address");
}

// Scenario: Rebuilding keeps each running effect, following its chain
void insert_adoption() {
    auto song = song_of(2);
    song.tracks[0].inserts = {insert(builtin("eq3")), insert(builtin("delay"))};
    song.tracks[1].inserts = {insert(builtin("reverb"))};
    SongEngine engine;
    build(engine, song);
    auto* eq = engine.processor({BusKind::track, 0, 0});
    auto* delay = engine.processor({BusKind::track, 0, 1});
    auto* reverb = engine.processor({BusKind::track, 1, 0});
    const auto built = graph_signature(song);

    // The EQ in front is removed: the delay moves up a slot as itself.
    auto edited = song;
    edited.tracks[0].inserts.erase(edited.tracks[0].inserts.begin());
    auto adopted = adopt_processors(engine.release_processors(), built, graph_signature(edited),
                                    nullptr);
    const auto find = [&adopted](ProcessorAddress where) -> PluginInstance* {
        for (const auto& kept : adopted)
            if (kept.where == where) return kept.instance.get();
        return nullptr;
    };
    require(adopted.size() == 2 && find({BusKind::track, 0, 0}) == delay &&
                find({BusKind::track, 1, 0}) == reverb,
            "the delay moves to slot 0 as the same instance; the reverb stays put");
    (void)eq;
    SongEngine next;
    const auto graph = populate_graph(next, edited, std::move(adopted), {});
    require(graph.adopted == 2 && graph.fresh.empty(), "nothing is created again");
    std::string error;
    require(next.prepare(edited, rate, block, 0, &error), "the edited song prepares");
    require(next.processor({BusKind::track, 0, 0}) == delay, "the new engine plays the delay");

    // Track 0 deleted: track 1's reverb follows its track to index 0.
    const auto built_next = graph_signature(edited);
    auto shrunk = edited;
    const auto remap = shrunk.remove_track(0);
    auto kept = adopt_processors(next.release_processors(), built_next, graph_signature(shrunk),
                                 &remap);
    require(kept.size() == 1 && kept.front().where == ProcessorAddress{BusKind::track, 0, 0} &&
                kept.front().instance.get() == reverb,
            "a track's inserts follow the track when one before it is deleted");
}

// Scenario: What an effect was dialled to is saved on rebuild and on save
void state_capture() {
    auto song = song_of(1);
    song.tracks[0].inserts = {insert(clap_effect())};
    SongEngine engine;
    build(engine, song);
    // The plugin's own window turns its gain: the running instance changes,
    // the song does not know yet.
    auto* effect = engine.processor({BusKind::track, 0, 0});
    require(effect != nullptr && effect->load_state(clap_effect_state(0.75)),
            "the running effect takes a new gain");
    require(song.tracks[0].inserts[0].plugin.state.empty(), "the song has no state yet");
    const auto built = graph_signature(song);
    require(capture_processor_states(engine, song, built) == 1,
            "the effect's state is copied into the song");
    require(song.tracks[0].inserts[0].plugin.state == effect->save_state(),
            "the song holds exactly what the effect saved");
    // A slot swapped for another plugin keeps the new plugin's state.
    auto swapped = song;
    swapped.tracks[0].inserts[0].plugin = builtin("delay");
    require(capture_processor_states(engine, swapped, built) == 0 &&
                swapped.tracks[0].inserts[0].plugin.state.empty(),
            "a state is never written into a slot that now holds another plugin");

    // Saved, loaded and built again, the effect plays at the dialled gain.
    Project project;
    project.song = song;
    std::string error;
    const auto loaded = ProjectFile::parse(ProjectFile::serialize(project), &error);
    require(loaded.has_value(), "the project parses: " + error);
    SongEngine reloaded;
    float level = 1.0F;
    build(reloaded, loaded->song);
    engine::TestAccess::set_test_source(reloaded, 0, dc_source, &level);
    reloaded.set_playing(true);
    require(near(render(reloaded, 512).left.back(), 0.75F * centre),
            "the reloaded effect is heard at the gain it was turned to");
}

// Scenario: The scanner tells instruments from effects
void scan_kind() {
    std::string error;
    const auto clap_effect_records =
        scan_candidate({"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH}, &error);
    require(clap_effect_records.size() == 1 && clap_effect_records[0].kind == effect_kind,
            "the CLAP effect declares audio-effect and is scanned as an effect: " + error);
    const auto clap_synth = scan_candidate({"CLAP", BLOKKILY_TEST_CLAP_PATH}, &error);
    require(clap_synth.size() == 1 && clap_synth[0].kind == instrument_kind,
            "the CLAP synth is an instrument");
    const auto vst3_fx = scan_candidate({"VST3", BLOKKILY_TEST_VST3_EFFECT_PATH}, &error);
    require(vst3_fx.size() == 1 && vst3_fx[0].kind == effect_kind,
            "the VST3 effect bundle is an effect");
    const auto vst3_synth = scan_candidate({"VST3", BLOKKILY_TEST_VST3_PATH}, &error);
    require(vst3_synth.size() == 1 && vst3_synth[0].kind == instrument_kind,
            "the VST3 synth bundle is an instrument");

    // The kind survives the helper's answer and the cache on disk.
    const auto reread = read_scan_records(write_scan_records(clap_effect_records));
    require(reread.size() == 1 && reread[0].kind == effect_kind, "records carry the kind");
    std::vector<ScanCacheEntry> cache{
        {{"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH}, 1, 2, true, {}, clap_effect_records}};
    const auto text = write_scan_cache(cache);
    require(text.rfind("blokkily-scan-cache 2\n", 0) == 0, "the cache is version 2");
    const auto cached = read_scan_cache(text);
    require(cached.size() == 1 && cached[0].records.size() == 1 &&
                cached[0].records[0].kind == effect_kind,
            "the cache keeps the kind");
    // A version 1 cache knew no kinds; it is scanned again rather than read.
    std::string old = text;
    old.replace(0, std::string("blokkily-scan-cache 2").size(), "blokkily-scan-cache 1");
    require(read_scan_cache(old).empty(), "a version 1 cache is not trusted");
}

// Scenario: The built-in effects are in the browser without a scan
void builtin_catalog() {
    const auto catalog = internal_catalog();
    std::vector<std::string> effects;
    for (const auto& entry : catalog)
        if (entry.kind == effect_kind) {
            require(entry.slot.format == builtin_effect_format, "built-ins use their own format");
            effects.push_back(entry.slot.identifier);
        }
    require(effects == std::vector<std::string>{"eq3", "delay", "reverb", "compressor"},
            "EQ, delay, reverb and compressor are listed as effects, in order");
    require(is_internal_format(std::string(builtin_effect_format)), "the format is registered");
    for (const auto& identifier : effects) {
        std::string error;
        auto effect = create_processor(builtin(identifier.c_str()), {}, &error);
        require(effect != nullptr && effect->ports().audio_inputs == 2 &&
                    !effect->ports().note_input,
                identifier + " is created by the factory as an effect: " + error);
    }
    std::string error;
    require(create_processor(builtin("flanger"), {}, &error) == nullptr && !error.empty(),
            "an unknown built-in is refused with a reason");
}

const std::map<std::string, std::function<void()>> cases{
    {"insert_chain", insert_chain},
    {"bypass_live", bypass_live},
    {"sends_and_returns", sends_and_returns},
    {"solo_safe_returns", solo_safe_returns},
    {"delay_compensation", delay_compensation},
    {"master_insert", master_insert},
    {"project_records", project_records},
    {"bounce_trims_latency", bounce_trims_latency},
    {"tempo_synced_delay", tempo_synced_delay},
    {"transport_per_chunk", transport_per_chunk},
    {"graph_signature", graph_signature_rules},
    {"insert_adoption", insert_adoption},
    {"state_capture", state_capture},
    {"scan_kind", scan_kind},
    {"builtin_catalog", builtin_catalog},
};

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const auto found = cases.find(argv[1]);
        require(found != cases.end(), std::string("unknown case ") + argv[1]);
        found->second();
        std::cout << argv[1] << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << (argc > 1 ? argv[1] : "") << ": " << error.what() << '\n';
        return 1;
    }
}
