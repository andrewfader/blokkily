// features/audio_reliability.feature: Playback does not allocate in the render
// callback.
//
// The engine after its seams were cut (plan F-D): every chunk runs the fixed
// stage order, events come from preallocated per-track scratch, a track with
// no instrument renders through its strip, and a processor's own parameter
// edits are stamped into the edit ring. A thousand process() calls, playing
// and stopped, across a seek and many loop wraps, must not allocate or free.
// The audio is then checked, so the zero comes from an engine that played.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "audio/engine/test_access.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;

constexpr std::size_t calls = 1000;
constexpr std::size_t stopped_calls = 100;
constexpr std::size_t block = 256;
constexpr blokkily::Tick song_length = 3000;   // one tick per sample
constexpr std::size_t seek_before_call = 500;
constexpr std::uint64_t seek_target = 1234;
constexpr float source_level = 0.5F;

void dc_source(void* context, blokkily::StereoBlock track, std::uint64_t) noexcept {
    const float level = *static_cast<const float*>(context);
    for (auto& sample : track.left) sample += level;
    for (auto& sample : track.right) sample += level;
}

// Reports a knob turn every block, the way a plugin's window does.
class EditingInstrument final : public blokkily::PluginInstance {
public:
    bool activate(double, std::uint32_t, std::uint32_t) override { return true; }
    void process(blokkily::StereoBlock audio,
                 std::span<const blokkily::PluginEvent>) noexcept override {
        std::fill(audio.left.begin(), audio.left.end(), 0.0F);
        std::fill(audio.right.begin(), audio.right.end(), 0.0F);
        pending_ = true;
    }
    std::size_t take_parameter_edits(std::span<blokkily::ParameterEdit> edits) noexcept override {
        if (!pending_ || edits.empty()) return 0;
        pending_ = false;
        edits[0] = {blokkily::ParameterEdit::Kind::value, 1, 0.5, 3};
        return 1;
    }
    std::vector<std::byte> save_state() override { return {}; }
    bool load_state(std::span<const std::byte>) override { return true; }
    std::string format() const override { return "Test"; }

private:
    bool pending_ = false;
};

} // namespace

BLOKKILY_REALTIME_CASE(engine_seams) {
    using namespace blokkily;
    using blokkily::realtime::require_no_allocations;

    Pattern pattern(song_length, 24000);
    Trigger trigger;
    trigger.start = 100;
    trigger.duration = 900;
    trigger.musical_data = Note{60, 1.0F, 0.0F};
    (void)pattern.add(trigger);
    Song song;
    song.patterns = {{"Seams", std::move(pattern)}};
    // Track 0: the CLAP fixture, hard left. Track 1: no instrument, a source
    // on its clip stage, hard right. Track 2: an instrument that reports
    // edits, silent.
    song.tracks = {Track{}, Track{}, Track{}};
    song.tracks[0].mix.pan = -1.0;
    song.tracks[1].mix.pan = 1.0;
    song.clips = {{0, 0, 0, 1}};

    std::string error;
    SongEngine engine;
    engine.set_processor(track_instrument(0), ClapPluginInstance::create(
                                                  BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test",
                                                  &error));
    engine.set_processor(track_instrument(2), std::make_unique<EditingInstrument>());
    require(engine.has_instrument(0) && !engine.has_instrument(1) && engine.has_instrument(2),
            "the CLAP fixture must load through the production adapter: " + error);
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);
    float level = source_level;
    engine::TestAccess::set_test_source(engine, 1, &dc_source, &level);
    const float right_gain = strip_gain(song.tracks[1].mix, false).right;

    std::vector<float> left((calls + stopped_calls) * block, -1.0F);
    std::vector<float> right((calls + stopped_calls) * block, -1.0F);
    std::size_t rolling_edits = 0;
    std::size_t resting_edits = 0;
    PluginEditEvent edit;

    engine.set_playing(true);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls + stopped_calls; ++call) {
        if (call == seek_before_call) engine.seek(seek_target);
        if (call == calls) engine.set_playing(false);
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
        // The control side drains the ring, outside the armed region.
        while (engine.take_plugin_edit(edit)) {
            require(edit.where == track_instrument(2), "edits carry their processor's address");
            ++(edit.rolling ? rolling_edits : resting_edits);
        }
    }
    require_no_allocations(total, "1100 SongEngine::process calls through every chunk stage");

    // Rolling, every chunk reported one edit; a block split by a wrap or a
    // timeline window is more than one chunk.
    require(rolling_edits >= calls, "every rolling chunk's edit reached the ring");
    require(resting_edits >= stopped_calls, "every stopped chunk's edit reached the ring");

    // The bare track sounded through its strip on every rolling sample and on
    // none of the stopped ones.
    const float expected = source_level * right_gain;
    for (std::size_t index = 0; index < right.size(); ++index) {
        const bool rolling = index < calls * block;
        const float want = rolling ? expected : 0.0F;
        if (std::abs(right[index] - want) > 1e-5F)
            throw std::runtime_error("bare track audio is wrong at sample " +
                                     std::to_string(index) + ": " + std::to_string(right[index]));
    }
    // The instrument track played its arrangement across the wraps.
    const std::span<const float> played{left.data(), calls * block};
    require(probe::rising_edges(played, 0.1F) >= (calls * block) / song_length - 1,
            "the arrangement note sounded on every pass");
}
