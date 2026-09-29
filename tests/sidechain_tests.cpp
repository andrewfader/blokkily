// Executable scenarios for features/sidechain.feature (phase 2, wave 5.2).
// Each case is its own CTest test, sidechain_<case>.
//
// Every claim is read off rendered audio: the master bus the production
// SongEngine::process() wrote, a bounce read back from disk, or what the
// built-in compressor did to a block. The tracks play test sources (a steady
// level, or bursts placed by song position) through the real clip stage, so
// what keys the compressor and what it compresses are known exactly.

#include "audio/engine/test_access.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/effects/builtin.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string text(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;

EffectSlot compressor_keyed_by(std::uint32_t source) {
    EffectSlot slot;
    slot.plugin = {std::string(builtin_effect_format), "", "compressor", {}};
    slot.sidechain = source;
    return slot;
}

// A steady level, read from the context each block.
void steady(void* context, StereoBlock track, std::uint64_t) noexcept {
    const float level = *static_cast<float*>(context);
    std::fill(track.left.begin(), track.left.end(), level);
    std::fill(track.right.begin(), track.right.end(), level);
}

// Full-scale bursts: on for the first 4800 samples of every 24000, by song
// position.
void bursts(void*, StereoBlock track, std::uint64_t position) noexcept {
    for (std::size_t frame = 0; frame < track.left.size(); ++frame) {
        const bool on = (position + frame) % 24000 < 4800;
        track.left[frame] = track.right[frame] = on ? 1.0F : 0.0F;
    }
}

Song two_tracks() {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}, Track{}};
    song.tracks[0].name = "BASS";
    song.tracks[1].name = "KICK";
    song.clips = {{0, 0, 0, 1}, {1, 0, 0, 1}};
    return song;
}

// The master's level over the last `frames` of `blocks` blocks.
double master_level(SongEngine& engine, int blocks) {
    std::vector<float> left(block), right(block);
    double level = 0.0;
    for (int index = 0; index < blocks; ++index) {
        engine.process({left, right});
        level = probe::peak(left);
    }
    return level;
}

// features/sidechain.feature: The key is taken after the key track's
// inserts and before its fader, and the key track renders first whatever
// its place in the song. Here the key (track 1) comes after the track it
// ducks (track 0), and it is muted and faded right down.
void key_pre_fader_case() {
    auto song = two_tracks();
    song.tracks[0].inserts = {compressor_keyed_by(1)};
    song.tracks[1].mix.mute = true;
    song.tracks[1].mix.gain_db = -60.0;
    require(song.consistent(), "the song is valid");
    SongEngine engine;
    engine.set_processor({BusKind::track, 0, 0}, create_builtin_effect("compressor"));
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    float bass = 0.5F;
    float kick = 0.0F;
    engine::TestAccess::set_test_source(engine, 0, steady, &bass);
    engine::TestAccess::set_test_source(engine, 1, steady, &kick);
    engine.set_playing(true);

    // The bass alone would be compressed by its own level (-6 dB is above the
    // -20 dB threshold); keyed, only the silent kick is listened to.
    const double open = master_level(engine, 20);
    require(std::abs(open - 0.5 * 0.70710678) < 1e-3,
            "a silent key leaves the bass alone, got " + text(open));
    kick = 1.0F;
    const double ducked = master_level(engine, 40);
    // 0 dB against -20 dB at 4:1 is 15 dB of reduction.
    const double expected = 0.5 * 0.70710678 * std::pow(10.0, -15.0 / 20.0);
    require(std::abs(ducked - expected) < 0.01,
            "the muted, faded kick still ducks the bass by 15 dB: got " + text(ducked) +
                ", expected " + text(expected));
    kick = 0.0F;
    const double released = master_level(engine, 200);
    require(std::abs(released - open) < 1e-3, "the bass comes back after the kick stops");
    std::cerr << "sidechain: open " << open << " ducked " << ducked << '\n';
}

// Regression: a compressor whose block was split by a parameter event read
// the key from the block's start for every segment, so the key heard in the
// second half was the first half's. Here the key is silent for the first
// half and full scale for the second, and a threshold event splits the block
// at the middle: the reduction must start in the second half.
void key_aligned_case() {
    auto effect = create_builtin_effect("compressor");
    require(effect != nullptr && effect->activate(rate, 1, 512), "the compressor activates");
    std::vector<float> key_left(512, 0.0F), key_right(512, 0.0F);
    std::fill(key_left.begin() + 256, key_left.end(), 1.0F);
    std::fill(key_right.begin() + 256, key_right.end(), 1.0F);
    std::vector<float> left(512, 0.5F), right(512, 0.5F);
    const PluginEvent events[]{
        {PluginEvent::Type::parameter_value, 0, compressor::attack_ms, 0.1},
        {PluginEvent::Type::parameter_value, 0, compressor::ratio, 20.0},
        {PluginEvent::Type::parameter_value, 256, compressor::threshold_db, -20.0}};
    effect->set_sidechain({key_left, key_right});
    effect->process({left, right}, events);
    const float first = probe::peak(std::span<const float>(left).subspan(0, 256));
    const float second = probe::peak(std::span<const float>(left).subspan(320, 192));
    require(std::abs(first - 0.5F) < 1e-4, "the silent half of the key compresses nothing, got " +
                                               text(first));
    // 20 dB over at 20:1 is 19 dB of reduction: 0.5 -> 0.056.
    require(second < 0.07F, "the loud half of the key ducks its own samples, got " + text(second));
}

// features/sidechain.feature: An export is ducked as playback was.
void bounce_includes_sidechain_case() {
    auto song = two_tracks();
    song.tracks[0].inserts = {compressor_keyed_by(1)};
    song.tracks[1].mix.mute = true;
    SongEngine engine;
    engine.set_processor({BusKind::track, 0, 0}, create_builtin_effect("compressor"));
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    float bass = 0.5F;
    engine::TestAccess::set_test_source(engine, 0, steady, &bass);
    engine::TestAccess::set_test_source(engine, 1, bursts, nullptr);
    std::filesystem::create_directories(BLOKKILY_TEST_ARTIFACTS);
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "sidechain.wav";
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value() && wave->frames == engine.song_samples(), "the bounce reads back");
    std::vector<float> live(static_cast<std::size_t>(wave->frames));
    std::vector<float> live_right(live.size());
    engine.seek(0);
    engine.set_playing(true);
    for (std::size_t done = 0; done < live.size(); done += block) {
        const auto now = std::min<std::size_t>(block, live.size() - done);
        engine.process({std::span(live).subspan(done, now), std::span(live_right).subspan(done, now)});
    }
    double worst = 0.0;
    for (std::size_t frame = 0; frame < live.size(); ++frame)
        worst = std::max(worst, std::abs(static_cast<double>(wave->interleaved[2 * frame] - live[frame])));
    require(worst < 1e-6, "the bounce equals the live render, worst " + text(worst));
    // And the ducking is in it: quiet while a burst plays, open between.
    const auto frames = std::span<const float>(live);
    const float during = probe::peak(frames.subspan(2000, 2000));
    const float between = probe::peak(frames.subspan(21000, 2000));
    require(during < 0.2F && between > 0.3F,
            "the export pumps with the key: " + text(during) + " / " + text(between));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: blokkily_sidechain_tests <case>\n";
        return 2;
    }
    const std::string name = argv[1];
    try {
        if (name == "key_pre_fader") key_pre_fader_case();
        else if (name == "key_aligned") key_aligned_case();
        else if (name == "bounce_includes_sidechain") bounce_includes_sidechain_case();
        else {
            std::cerr << "Unknown case: " << name << "\n";
            return 2;
        }
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "PASS " << name << '\n';
    return 0;
}
