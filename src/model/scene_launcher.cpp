#include "blokkily/model/scene_launcher.hpp"

#include <algorithm>

namespace blokkily {

namespace {

static const std::optional<SceneSlot> empty_slot = std::nullopt;

} // namespace

std::size_t SceneMatrix::add_scene(std::string name, std::optional<double> bpm) {
    const std::size_t idx = scenes.size();
    std::size_t track_count = 0;
    if (!scenes.empty()) {
        track_count = scenes.front().cells.size();
    }
    scenes.push_back(Scene{
        .name = std::move(name),
        .bpm = bpm,
        .cells = std::vector<std::optional<SceneSlot>>(track_count, std::nullopt)
    });
    return idx;
}

void SceneMatrix::insert_scene(std::size_t index, std::string name, std::optional<double> bpm) {
    std::size_t track_count = 0;
    if (!scenes.empty()) {
        track_count = scenes.front().cells.size();
    }
    const auto it = scenes.begin() + std::min(index, scenes.size());
    scenes.insert(it, Scene{
        .name = std::move(name),
        .bpm = bpm,
        .cells = std::vector<std::optional<SceneSlot>>(track_count, std::nullopt)
    });
}

bool SceneMatrix::remove_scene(std::size_t index) {
    if (index >= scenes.size()) return false;
    scenes.erase(scenes.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void SceneMatrix::set_slot(std::size_t scene_index, std::size_t track_index, SceneSlot slot) {
    if (scene_index >= scenes.size()) return;
    auto& scene = scenes[scene_index];
    if (track_index >= scene.cells.size()) {
        scene.cells.resize(track_index + 1, std::nullopt);
    }
    scene.cells[track_index] = slot;
}

void SceneMatrix::clear_slot(std::size_t scene_index, std::size_t track_index) {
    if (scene_index >= scenes.size()) return;
    auto& scene = scenes[scene_index];
    if (track_index < scene.cells.size()) {
        scene.cells[track_index] = std::nullopt;
    }
}

const std::optional<SceneSlot>& SceneMatrix::slot(std::size_t scene_index,
                                                  std::size_t track_index) const noexcept {
    if (scene_index >= scenes.size()) return empty_slot;
    const auto& scene = scenes[scene_index];
    if (track_index >= scene.cells.size()) return empty_slot;
    return scene.cells[track_index];
}

void SceneMatrix::adjust_for_tracks(std::size_t track_count) {
    for (auto& scene : scenes) {
        scene.cells.resize(track_count, std::nullopt);
    }
}

void SceneMatrix::remove_track(std::size_t track_index) {
    for (auto& scene : scenes) {
        if (track_index < scene.cells.size()) {
            scene.cells.erase(scene.cells.begin() + static_cast<std::ptrdiff_t>(track_index));
        }
    }
}

Tick SceneMatrix::next_quantized_tick(Tick current_tick,
                                      LaunchQuantization quantization,
                                      const MeterMap& meter) noexcept {
    if (quantization == LaunchQuantization::none || current_tick < 0) {
        return current_tick;
    }

    const Tick beat_len = std::max<Tick>(1, meter.beat_length(current_tick));

    switch (quantization) {
        case LaunchQuantization::none:
            return current_tick;

        case LaunchQuantization::quarter_beat: {
            const Tick step = std::max<Tick>(1, beat_len / 4);
            const Tick rem = current_tick % step;
            return rem == 0 ? current_tick : (current_tick + (step - rem));
        }

        case LaunchQuantization::beat: {
            const Tick rem = current_tick % beat_len;
            return rem == 0 ? current_tick : (current_tick + (beat_len - rem));
        }

        case LaunchQuantization::bar: {
            const std::int32_t bar = meter.bar_at(current_tick);
            const Tick bar_start = meter.bar_start(bar);
            if (current_tick == bar_start) return current_tick;
            return meter.bar_start(bar + 1);
        }

        case LaunchQuantization::two_bars: {
            const std::int32_t bar = meter.bar_at(current_tick);
            const Tick bar_start = meter.bar_start(bar);
            if (current_tick == bar_start && (bar % 2 == 0)) return current_tick;
            const std::int32_t next_bar = bar + (2 - (bar % 2));
            return meter.bar_start(next_bar);
        }

        case LaunchQuantization::four_bars: {
            const std::int32_t bar = meter.bar_at(current_tick);
            const Tick bar_start = meter.bar_start(bar);
            if (current_tick == bar_start && (bar % 4 == 0)) return current_tick;
            const std::int32_t next_bar = bar + (4 - (bar % 4));
            return meter.bar_start(next_bar);
        }
    }

    return current_tick;
}

std::optional<std::size_t> SceneMatrix::resolve_follow_action(FollowAction action,
                                                              std::size_t current_scene,
                                                              std::size_t track_index,
                                                              std::uint64_t seed) const {
    if (scenes.empty()) return std::nullopt;

    switch (action) {
        case FollowAction::none:
            return current_scene;

        case FollowAction::stop:
            return std::nullopt;

        case FollowAction::again:
            return current_scene;

        case FollowAction::next: {
            const std::size_t next_idx = (current_scene + 1) % scenes.size();
            return next_idx;
        }

        case FollowAction::previous: {
            const std::size_t prev_idx = current_scene > 0 ? current_scene - 1 : scenes.size() - 1;
            return prev_idx;
        }

        case FollowAction::first:
            return 0;

        case FollowAction::last:
            return scenes.size() - 1;

        case FollowAction::random: {
            std::size_t populated_count = 0;
            for (std::size_t s = 0; s < scenes.size(); ++s) {
                if (track_index < scenes[s].cells.size() && scenes[s].cells[track_index].has_value()) {
                    ++populated_count;
                }
            }
            if (populated_count == 0) return std::nullopt;
            const std::size_t choice = static_cast<std::size_t>(seed % populated_count);
            std::size_t current = 0;
            for (std::size_t s = 0; s < scenes.size(); ++s) {
                if (track_index < scenes[s].cells.size() && scenes[s].cells[track_index].has_value()) {
                    if (current == choice) return s;
                    ++current;
                }
            }
            return std::nullopt;
        }
    }

    return std::nullopt;
}

} // namespace blokkily
