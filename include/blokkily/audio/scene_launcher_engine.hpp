#pragma once

#include "blokkily/model/scene_launcher.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace blokkily {

struct TimedTickEvent {
    Tick tick = 0;
    PluginEvent event;
};

struct CompiledPattern {
    Tick length = 1920;
    std::vector<TimedTickEvent> events;
};

// Represents a clip recorded during a live jam session
struct JamRecord {
    std::size_t track = 0;
    std::size_t pattern = 0;
    Tick start_tick = 0;
    Tick end_tick = 0;

    friend bool operator==(const JamRecord&, const JamRecord&) = default;
};

class SceneLauncherEngine {
public:
    enum class TrackStatus : std::uint8_t {
        stopped = 0,
        playing = 1,
        queued_play = 2,
        queued_stop = 3
    };

    struct TrackState {
        TrackStatus status{TrackStatus::stopped};
        std::size_t active_scene = 0;
        std::size_t active_pattern = 0;
        std::uint32_t current_repeat = 0;
        std::uint32_t total_repeats = 0;
        FollowAction follow_action = FollowAction::none;
        Tick pattern_start_tick = 0;
        std::size_t event_cursor = 0;

        // Queued parameters
        std::size_t queued_scene = 0;
        std::size_t queued_pattern = 0;
        std::uint32_t queued_repeats = 0;
        FollowAction queued_follow = FollowAction::none;
        LaunchQuantization quantization = LaunchQuantization::bar;
        Tick target_launch_tick = 0;

        // Sounding notes tracking for clean note-off dispatch
        std::array<std::uint8_t, 128> sounding{};

        // Active jam record for this track
        std::optional<JamRecord> current_jam;
    };

    SceneLauncherEngine() = default;

    void prepare(const Song& song);

    // Control thread API
    void launch_slot(std::size_t track, std::size_t scene_index,
                     LaunchQuantization q = LaunchQuantization::bar);
    void launch_scene(std::size_t scene_index,
                      LaunchQuantization q = LaunchQuantization::bar);
    void stop_track(std::size_t track,
                    LaunchQuantization q = LaunchQuantization::bar);
    void stop_all(LaunchQuantization q = LaunchQuantization::bar);

    [[nodiscard]] bool is_playing(std::size_t track) const noexcept;
    [[nodiscard]] bool is_queued(std::size_t track) const noexcept;
    [[nodiscard]] std::size_t active_scene(std::size_t track) const noexcept;
    [[nodiscard]] std::size_t queued_scene(std::size_t track) const noexcept;

    // Jam recording
    void set_jam_recording(bool active) noexcept { jam_recording_.store(active, std::memory_order_release); }
    [[nodiscard]] bool is_jam_recording() const noexcept { return jam_recording_.load(std::memory_order_acquire); }
    [[nodiscard]] std::vector<JamRecord> take_jam_records();

    // Audio thread processing (Zero allocations guaranteed)
    // Returns number of events added to track_events
    std::size_t process_track(std::size_t track,
                              std::uint64_t song_position,
                              std::size_t frames,
                              const TickClock& clock,
                              const MeterMap& meter,
                              std::span<PluginEvent> track_events,
                              std::size_t current_count,
                              std::size_t capacity) noexcept;

    // Pre-chunk hook to process queued launch/stop commands at the start of each block
    void begin_chunk(Tick start_tick, const MeterMap& meter) noexcept;

private:
    SceneMatrix matrix_;
    std::vector<CompiledPattern> patterns_;
    std::vector<TrackState> tracks_;
    std::atomic<bool> jam_recording_{false};
    static constexpr std::size_t max_jams = 256;
    std::array<JamRecord, max_jams> completed_jams_{};
    std::atomic<std::size_t> jam_count_{0};

    struct LaunchCommand {
        enum class Type : std::uint8_t { none, launch_slot, launch_scene, stop_track, stop_all };
        Type type = Type::none;
        std::size_t track = 0;
        std::size_t scene = 0;
        LaunchQuantization quantization = LaunchQuantization::bar;
    };

    static constexpr std::size_t cmd_queue_capacity = 64;
    std::array<LaunchCommand, cmd_queue_capacity> cmd_queue_{};
    std::atomic<std::size_t> cmd_read_{0};
    std::atomic<std::size_t> cmd_write_{0};

    bool push_command(const LaunchCommand& cmd) noexcept;
    void apply_commands(Tick current_tick, const MeterMap& meter) noexcept;
    void close_jam_for_track(std::size_t track, Tick end_tick) noexcept;
};

} // namespace blokkily
