#include "blokkily/model/groove.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <variant>
#include <vector>

namespace blokkily {
namespace {

// Where a trigger at `at` ticks is written: its step, and how far off it.
struct Placement {
    Tick start = 0;
    Tick micro = 0;
};

Placement place(Tick at, Tick length, Tick ticks_per_step) {
    const Tick steps = std::max<Tick>(1, length / ticks_per_step);
    at = ((at % length) + length) % length;
    Tick step = (at + ticks_per_step / 2) / ticks_per_step;
    Tick micro = at - step * ticks_per_step;
    if (step >= steps) {
        step = 0;
        micro = at - length;
    }
    const Tick reach = ticks_per_step / 2 - 1;
    return {step * ticks_per_step, std::clamp(micro, -reach, reach)};
}

} // namespace

GrooveResult quantize_pattern(Pattern& pattern, Tick grid, double strength, Tick ticks_per_step) {
    GrooveResult result;
    if (grid <= 0 || ticks_per_step <= 0) return result;
    strength = std::clamp(strength, 0.0, 1.0);
    const Tick length = pattern.length();
    std::vector<Trigger> triggers(pattern.events().begin(), pattern.events().end());
    // Steps already taken: a trigger that stays where it is keeps its own.
    std::set<Tick> taken;
    for (const auto& trigger : triggers) taken.insert(trigger.start);
    for (auto& trigger : triggers) {
        const Tick at = trigger.start + trigger.micro_offset;
        const Tick target = static_cast<Tick>(std::llround(static_cast<double>(at) / grid)) * grid;
        const auto moved = static_cast<Tick>(std::llround(at + strength * static_cast<double>(target - at)));
        const auto placement = place(moved % length == 0 ? 0 : moved, length, ticks_per_step);
        if (placement.start == trigger.start && placement.micro == trigger.micro_offset) continue;
        if (placement.start != trigger.start && taken.contains(placement.start)) {
            ++result.kept;
            continue;
        }
        taken.erase(trigger.start);
        taken.insert(placement.start);
        trigger.start = placement.start;
        trigger.micro_offset = placement.micro;
        (void)pattern.update(trigger.id, trigger);
        ++result.changed;
    }
    return result;
}

GrooveResult humanize_pattern(Pattern& pattern, Tick timing, float velocity, std::uint32_t seed,
                              Tick ticks_per_step) {
    GrooveResult result;
    std::mt19937 random(seed);
    const Tick reach = ticks_per_step / 2 - 1;
    timing = std::clamp<Tick>(timing, 0, reach);
    velocity = std::clamp(velocity, 0.0F, 1.0F);
    // Uniform, from integers so the result is the same with any standard
    // library: std::uniform_*_distribution is not specified bit for bit.
    const auto spread = [&random](double amount) {
        return amount * (static_cast<double>(random()) / 4294967295.0 * 2.0 - 1.0);
    };
    std::vector<Trigger> triggers(pattern.events().begin(), pattern.events().end());
    for (auto& trigger : triggers) {
        const Tick shift = static_cast<Tick>(std::llround(spread(static_cast<double>(timing))));
        const double change = spread(velocity);
        trigger.micro_offset = std::clamp<Tick>(trigger.micro_offset + shift, -reach, reach);
        const auto nudge = [change](float value) {
            return static_cast<float>(std::clamp(static_cast<double>(value) + change, 0.01, 1.0));
        };
        if (auto* note = std::get_if<Note>(&trigger.musical_data)) {
            note->velocity = nudge(note->velocity);
        } else {
            auto& chord = std::get<Chord>(trigger.musical_data);
            chord.velocity = nudge(chord.velocity);
            for (auto& voice : chord.velocities) voice = nudge(voice);
        }
        (void)pattern.update(trigger.id, trigger);
        ++result.changed;
    }
    return result;
}

} // namespace blokkily
