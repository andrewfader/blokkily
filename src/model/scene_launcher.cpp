#include "blokkily/model/scene_launcher.hpp"

#include <algorithm>

namespace blokkily {

namespace {

static const std::optional<SceneSlot> empty_slot = std::nullopt;

} // namespace

std::size_t SceneMatrix::add_scene(std::string name) {
    scenes.push_back(Scene{.name = std::move(name), .cells = {}});
    return scenes.size() - 1;
}

void SceneMatrix::insert_scene(std::size_t index, std::string name) {
    const auto it = scenes.begin() + static_cast<std::ptrdiff_t>(std::min(index, scenes.size()));
    scenes.insert(it, Scene{.name = std::move(name), .cells = {}});
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

void SceneMatrix::remove_track(std::size_t track_index) {
    for (auto& scene : scenes) {
        if (track_index < scene.cells.size()) {
            scene.cells.erase(scene.cells.begin() + static_cast<std::ptrdiff_t>(track_index));
        }
    }
}

void SceneMatrix::remove_pattern(std::size_t pattern_index) {
    for (auto& scene : scenes)
        for (auto& cell : scene.cells) {
            if (!cell) continue;
            if (cell->pattern == pattern_index) cell.reset();
            else if (cell->pattern > pattern_index) --cell->pattern;
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

        case LaunchQuantization::quarter_beat:
        case LaunchQuantization::beat: {
            // Counted from the bar's start, so a bar of 7/8 still puts its
            // beats where the meter does.
            const std::int32_t bar = meter.bar_at(current_tick);
            const Tick bar_start = meter.bar_start(bar);
            const Tick step = quantization == LaunchQuantization::beat
                                  ? beat_len
                                  : std::max<Tick>(1, beat_len / 4);
            const Tick into = current_tick - bar_start;
            const Tick rem = into % step;
            const Tick next = rem == 0 ? current_tick : current_tick + (step - rem);
            return std::min(next, meter.bar_start(bar + 1));
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

} // namespace blokkily
