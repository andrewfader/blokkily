#pragma once

#include "blokkily/model/timebase.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blokkily {

// Launch quantization boundary for triggering clips and scenes.
enum class LaunchQuantization : std::uint8_t {
    none = 0,         // Immediate launch without waiting
    quarter_beat = 1, // 1/16th note
    beat = 2,         // 1/4 note (one beat)
    bar = 3,          // 1 bar (standard default)
    two_bars = 4,     // 2 bars
    four_bars = 5     // 4 bars
};

// Follow action behavior when a slot completes its repeats.
enum class FollowAction : std::uint8_t {
    none = 0,     // Loop continuously / do nothing
    stop = 1,     // Stop track playback
    next = 2,     // Launch slot in next scene down
    previous = 3, // Launch slot in previous scene up
    first = 4,    // Launch slot in first scene (scene 0)
    last = 5,     // Launch slot in last scene
    random = 6,   // Launch slot in a random populated scene on this track
    again = 7     // Replay this slot from the beginning
};

// One cell in the scene launcher matrix.
struct SceneSlot {
    std::size_t pattern = 0;                     // Index into song.patterns
    std::uint32_t repeats = 0;                   // 0 = infinite loop; >0 = loops before follow action
    LaunchQuantization quantization = LaunchQuantization::bar;
    FollowAction follow_action = FollowAction::none;

    friend bool operator==(const SceneSlot&, const SceneSlot&) = default;
};

// One row across all tracks in the launcher matrix.
struct Scene {
    std::string name;
    std::optional<double> bpm = std::nullopt;    // Optional scene tempo change
    std::vector<std::optional<SceneSlot>> slots; // Indexed by track index

    friend bool operator==(const Scene&, const Scene&) = default;
};

// The non-linear clip / scene matrix model.
class SceneMatrix {
public:
    std::vector<Scene> scenes;

    [[nodiscard]] std::size_t scene_count() const noexcept { return scenes.size(); }
    [[nodiscard]] bool empty() const noexcept { return scenes.empty(); }

    std::size_t add_scene(std::string name = "", std::optional<double> bpm = std::nullopt);
    void insert_scene(std::size_t index, std::string name = "", std::optional<double> bpm = std::nullopt);
    bool remove_scene(std::size_t index);

    void set_slot(std::size_t scene_index, std::size_t track_index, SceneSlot slot);
    void clear_slot(std::size_t scene_index, std::size_t track_index);
    [[nodiscard]] const std::optional<SceneSlot>& slot(std::size_t scene_index,
                                                      std::size_t track_index) const noexcept;

    // Adjusts slot columns across all scenes when tracks are added/resized.
    void adjust_for_tracks(std::size_t track_count);
    // Adjusts slot columns when a track is removed.
    void remove_track(std::size_t track_index);

    // Calculates the next quantization boundary tick from `current_tick`.
    [[nodiscard]] static Tick next_quantized_tick(Tick current_tick,
                                                  LaunchQuantization quantization,
                                                  const MeterMap& meter) noexcept;

    // Resolves what scene should be queued next given a FollowAction.
    [[nodiscard]] std::optional<std::size_t> resolve_follow_action(FollowAction action,
                                                                   std::size_t current_scene,
                                                                   std::size_t track_index,
                                                                   std::uint64_t seed = 0) const;

    friend bool operator==(const SceneMatrix&, const SceneMatrix&) = default;
};

} // namespace blokkily
