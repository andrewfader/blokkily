#pragma once

// The scene launcher (phase 2, wave 6.1): a grid of scenes (rows) by tracks
// (columns) whose cells name a pattern of the song. It is part of the song and
// holds no music of its own: a cell refers to a pattern by index, exactly as a
// clip does, so editing the pattern edits what the cell plays.

#include "blokkily/model/timebase.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blokkily {

// Where a launch or a stop waits for: nowhere (at once), the next sixteenth,
// beat, bar, or pair or four of bars, counted by the song's meter.
enum class LaunchQuantization : std::uint8_t {
    none = 0,
    quarter_beat = 1,
    beat = 2,
    bar = 3,
    two_bars = 4,
    four_bars = 5
};
inline constexpr std::uint8_t launch_quantization_count = 6;

// What a cell does after it has played its repeats. `none` loops for ever;
// `stop` stops the track; `next`, `previous`, `first`, `last` and `random`
// launch this track's cell in that scene (random: another scene with a cell
// on this track, chosen at random); `again` plays the cell again from its
// start. A follow action that lands on an empty cell stops the track.
enum class FollowAction : std::uint8_t {
    none = 0,
    stop = 1,
    next = 2,
    previous = 3,
    first = 4,
    last = 5,
    random = 6,
    again = 7
};
inline constexpr std::uint8_t follow_action_count = 8;

// One cell of the grid.
struct SceneSlot {
    std::size_t pattern = 0;   // index into Song::patterns
    std::uint32_t repeats = 0; // loops before the follow action; 0 loops for ever
    // Where launching this cell on its own waits for.
    LaunchQuantization quantization = LaunchQuantization::bar;
    FollowAction follow_action = FollowAction::none;

    friend bool operator==(const SceneSlot&, const SceneSlot&) = default;
};

// One row across the tracks. `cells` is indexed by track and may be shorter
// than the song's track list: a missing cell is an empty one.
struct Scene {
    std::string name;
    std::vector<std::optional<SceneSlot>> cells;

    friend bool operator==(const Scene&, const Scene&) = default;
};

class SceneMatrix {
public:
    std::vector<Scene> scenes;
    // Where launching a whole scene, and stopping, wait for.
    LaunchQuantization quantization = LaunchQuantization::bar;

    [[nodiscard]] std::size_t scene_count() const noexcept { return scenes.size(); }
    [[nodiscard]] bool empty() const noexcept { return scenes.empty(); }

    std::size_t add_scene(std::string name = "");
    void insert_scene(std::size_t index, std::string name = "");
    bool remove_scene(std::size_t index);

    void set_slot(std::size_t scene_index, std::size_t track_index, SceneSlot slot);
    void clear_slot(std::size_t scene_index, std::size_t track_index);
    [[nodiscard]] const std::optional<SceneSlot>& slot(std::size_t scene_index,
                                                      std::size_t track_index) const noexcept;

    // Track `track_index` was removed: its column goes and later ones move
    // left.
    void remove_track(std::size_t track_index);
    // Pattern `pattern_index` was removed from the song: cells that played it
    // are emptied, and cells of later patterns follow them down one.
    void remove_pattern(std::size_t pattern_index);

    // The first tick at or after `current_tick` that `quantization` allows a
    // launch on, by `meter`.
    [[nodiscard]] static Tick next_quantized_tick(Tick current_tick,
                                                  LaunchQuantization quantization,
                                                  const MeterMap& meter) noexcept;

    friend bool operator==(const SceneMatrix&, const SceneMatrix&) = default;
};

} // namespace blokkily
