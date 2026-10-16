#include "engine_arpeggiator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace blokkily::engine {
namespace {

struct Settings {
    bool enabled = false;
    bool latch = false;
    Arpeggiator::Mode mode = Arpeggiator::Mode::up;
    std::uint32_t division = 4;
    std::uint32_t octaves = 1;
    double gate = 0.5;
};

std::uint64_t pack(const Arpeggiator& arp) noexcept {
    const auto gate = static_cast<std::uint64_t>(std::lround(std::clamp(arp.gate, 0.05F, 1.0F) * 10000.0F));
    return (arp.enabled ? 1ULL : 0ULL) | (arp.latch ? 2ULL : 0ULL) |
           (static_cast<std::uint64_t>(arp.mode) & 7ULL) << 2 |
           static_cast<std::uint64_t>(arp.division) << 8 |
           static_cast<std::uint64_t>(arp.octaves) << 16 | gate << 32;
}

Settings unpack(std::uint64_t bits) noexcept {
    Settings settings;
    settings.enabled = (bits & 1ULL) != 0;
    settings.latch = (bits & 2ULL) != 0;
    const auto mode = static_cast<std::uint8_t>((bits >> 2) & 7ULL);
    settings.mode = mode < Arpeggiator::mode_count ? static_cast<Arpeggiator::Mode>(mode)
                                                   : Arpeggiator::Mode::up;
    settings.division = std::max<std::uint32_t>(1, static_cast<std::uint32_t>((bits >> 8) & 0xFFULL));
    settings.octaves = std::clamp<std::uint32_t>(static_cast<std::uint32_t>((bits >> 16) & 0xFFULL), 1, 4);
    settings.gate = std::clamp(static_cast<double>((bits >> 32) & 0xFFFFULL) / 10000.0, 0.05, 1.0);
    return settings;
}

PluginEvent note_off(std::int32_t key, double cents, std::uint32_t offset) noexcept {
    return {PluginEvent::Type::note_off, offset, key, 0.0, cents, 0};
}

// Builds the chunk's events in `scratch`, as far as it has room.
struct Writer {
    std::span<PluginEvent> scratch;
    std::size_t count = 0;
    void put(const PluginEvent& event) noexcept {
        if (count < scratch.size()) scratch[count++] = event;
    }
};

void forget_keys(ArpPlayback& arp) noexcept {
    arp.held_count = 0;
    arp.step = 0;
}

// The key and which held note the arpeggio plays on its step `step`.
bool pick(ArpPlayback& arp, const Settings& settings, std::int32_t& key, std::size_t& which) noexcept {
    const std::size_t n = arp.held_count;
    if (n == 0) return false;
    // The held notes in the order the mode walks them: by pitch, or by when
    // they were pressed. Insertion sort over at most 32 indices.
    std::array<std::size_t, ArpPlayback::capacity> order{};
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t at = i;
        const bool by_press = settings.mode == Arpeggiator::Mode::played;
        while (at > 0) {
            const auto& before = arp.held[order[at - 1]];
            const auto& mine = arp.held[i];
            const bool later = by_press ? before.order > mine.order : before.key > mine.key;
            if (!later) break;
            order[at] = order[at - 1];
            --at;
        }
        order[at] = i;
    }
    const std::size_t length = n * settings.octaves;
    std::size_t j = 0;
    switch (settings.mode) {
    case Arpeggiator::Mode::up:
    case Arpeggiator::Mode::played: j = arp.step % length; break;
    case Arpeggiator::Mode::down: j = length - 1 - arp.step % length; break;
    case Arpeggiator::Mode::up_down: {
        const std::size_t cycle = length > 1 ? 2 * length - 2 : 1;
        j = arp.step % cycle;
        if (j >= length) j = cycle - j;
        break;
    }
    case Arpeggiator::Mode::random:
        arp.random ^= arp.random << 13;
        arp.random ^= arp.random >> 17;
        arp.random ^= arp.random << 5;
        j = arp.random % length;
        break;
    }
    which = order[j % n];
    key = arp.held[which].key + 12 * static_cast<std::int32_t>(j / n);
    while (key > 127) key -= 12;
    return true;
}

} // namespace

void set_arpeggiator(ArpPlayback& arp, const Arpeggiator& settings) noexcept {
    arp.settings.store(pack(settings), std::memory_order_relaxed);
}

void reset_arpeggiator(ArpPlayback& arp) noexcept {
    forget_keys(arp);
    arp.pressed = 0;
    arp.random = 0x9E3779B9U;
    arp.sounding = false;
    arp.samples = 0;
    arp.free_tick = 0.0;
    arp.was_enabled = false;
}

std::size_t run_arpeggiator(ArpPlayback& arp, const ArpChunk& chunk,
                            std::span<PluginEvent> events, std::size_t count,
                            std::span<PluginEvent> scratch) noexcept {
    const auto settings = unpack(arp.settings.load(std::memory_order_relaxed));
    count = std::min(count, events.size());
    const std::uint64_t chunk_start = arp.samples;
    arp.samples += chunk.frames;
    if (!settings.enabled) {
        // Switched off: the note it was sounding is let go; the keys it
        // held are the instrument's again from the next note-on.
        if (arp.was_enabled) {
            Writer out{scratch};
            if (arp.sounding) out.put(note_off(arp.sounding_key, arp.sounding_cents, 0));
            for (std::size_t i = 0; i < count; ++i) out.put(events[i]);
            reset_arpeggiator(arp);
            arp.samples = chunk_start + chunk.frames;
            std::copy_n(scratch.begin(), std::min(out.count, events.size()), events.begin());
            return std::min(out.count, events.size());
        }
        return count;
    }
    if (!arp.was_enabled) {
        forget_keys(arp);
        arp.sounding = false;
        arp.free_tick = 0.0;
        arp.was_enabled = true;
    }
    if (chunk.clock == nullptr || chunk.frames == 0 || chunk.sample_rate <= 0.0) return count;

    Writer out{scratch};
    // A stop or a jump lets go of everything, latched or not.
    if (chunk.release) {
        if (arp.sounding) out.put(note_off(arp.sounding_key, arp.sounding_cents, 0));
        arp.sounding = false;
        forget_keys(arp);
    }

    // The grid points in this chunk, as sample offsets.
    const double step_ticks = static_cast<double>(std::max<Tick>(1, chunk.ticks_per_beat)) /
                              static_cast<double>(settings.division);
    const auto position = static_cast<double>(chunk.song_position);
    const double ticks_per_sample =
        chunk.clock->bpm_at_sample(position) * static_cast<double>(chunk.ticks_per_beat) /
        60.0 / chunk.sample_rate;
    std::array<std::uint32_t, 64> steps{};
    std::size_t step_count = 0;
    {
        const double first = chunk.rolling ? chunk.clock->tick_at(position) : arp.free_tick;
        const double last = chunk.rolling ? chunk.clock->tick_at(position + static_cast<double>(chunk.frames))
                                          : first + static_cast<double>(chunk.frames) * ticks_per_sample;
        for (auto k = static_cast<std::int64_t>(std::ceil(first / step_ticks - 1e-9));
             step_count < steps.size(); ++k) {
            const double tick = static_cast<double>(k) * step_ticks;
            if (tick >= last) break;
            const double at = chunk.rolling ? chunk.clock->sample_at_tick(tick) - position
                                            : (tick - first) / std::max(ticks_per_sample, 1e-12);
            // Rounded down, as the timeline places a note on its tick, so a
            // step and a note written on the same tick land together.
            steps[step_count++] = static_cast<std::uint32_t>(std::clamp<double>(
                std::floor(at + 1e-9), 0.0, static_cast<double>(chunk.frames - 1)));
        }
        arp.free_tick = chunk.rolling ? 0.0 : last;
    }
    const double step_samples = step_ticks / std::max(ticks_per_sample, 1e-12);
    const auto gate_samples = static_cast<std::uint64_t>(std::max(1.0, std::round(settings.gate * step_samples)));

    // A step sounds the next note of the arpeggio at `offset`.
    std::int64_t last_step_offset = -1;
    const auto play_step = [&](std::uint32_t offset) {
        if (static_cast<std::int64_t>(offset) == last_step_offset) return;
        last_step_offset = offset;
        if (arp.sounding) out.put(note_off(arp.sounding_key, arp.sounding_cents, offset));
        arp.sounding = false;
        std::int32_t key = 0;
        std::size_t which = 0;
        if (!pick(arp, settings, key, which)) return;
        const auto& held = arp.held[which];
        out.put({PluginEvent::Type::note_on, offset, key, held.velocity, held.cents, 0});
        arp.sounding = true;
        arp.sounding_key = key;
        arp.sounding_cents = held.cents;
        arp.off_at = chunk_start + offset + gate_samples;
        ++arp.step;
    };
    const auto any_down = [&] {
        for (std::size_t i = 0; i < arp.held_count; ++i)
            if (arp.held[i].down) return true;
        return false;
    };
    // The first key of a chord starts the arpeggio there and then, once
    // every key struck on that sample is in.
    std::int64_t start_at = -1;
    const auto press = [&](const PluginEvent& event) {
        // Latched, a chord played after every key was let go replaces the
        // one the latch kept.
        if (settings.latch && !any_down()) forget_keys(arp);
        const bool first = arp.held_count == 0;
        std::size_t i = 0;
        while (i < arp.held_count && arp.held[i].key != event.key_or_parameter) ++i;
        if (i == arp.held_count) {
            if (arp.held_count == ArpPlayback::capacity) return;
            ++arp.held_count;
        }
        arp.held[i] = {event.key_or_parameter, static_cast<float>(event.value), event.cents,
                       arp.pressed++, true};
        if (first) {
            arp.step = 0;
            start_at = event.sample_offset;
        }
    };
    const auto let_go = [&](const PluginEvent& event) {
        std::size_t i = 0;
        while (i < arp.held_count && arp.held[i].key != event.key_or_parameter) ++i;
        // A key it never took (struck before it was switched on) is the
        // instrument's to let go.
        if (i == arp.held_count) {
            out.put(event);
            return;
        }
        if (settings.latch) {
            arp.held[i].down = false;
            return;
        }
        arp.held[i] = arp.held[--arp.held_count];
        if (arp.held_count == 0 && arp.sounding) {
            out.put(note_off(arp.sounding_key, arp.sounding_cents, event.sample_offset));
            arp.sounding = false;
        }
    };

    std::size_t next_event = 0, next_step = 0;
    constexpr auto never = std::numeric_limits<std::uint64_t>::max();
    for (;;) {
        const std::uint64_t at_event = next_event < count ? events[next_event].sample_offset : never;
        const std::uint64_t at_step = next_step < step_count ? steps[next_step] : never;
        const std::uint64_t at_off = arp.sounding && arp.off_at < chunk_start + chunk.frames
                                         ? (arp.off_at > chunk_start ? arp.off_at - chunk_start : 0)
                                         : never;
        const std::uint64_t soonest = std::min({at_event, at_step, at_off});
        if (start_at >= 0 && soonest > static_cast<std::uint64_t>(start_at)) {
            play_step(static_cast<std::uint32_t>(start_at));
            start_at = -1;
            continue;
        }
        if (soonest == never) break;
        // Notes played on a step are part of it; a gate that ends where a
        // step begins ends first.
        if (at_event <= at_step && at_event <= at_off) {
            const auto& event = events[next_event++];
            if (event.type == PluginEvent::Type::note_on && event.value > 0.0) press(event);
            else if (event.type == PluginEvent::Type::note_on || event.type == PluginEvent::Type::note_off)
                let_go(event);
            else out.put(event);
        } else if (at_off <= at_step) {
            out.put(note_off(arp.sounding_key, arp.sounding_cents, static_cast<std::uint32_t>(at_off)));
            arp.sounding = false;
        } else {
            play_step(steps[next_step++]);
        }
    }
    const auto written = std::min(out.count, events.size());
    std::copy_n(scratch.begin(), written, events.begin());
    return written;
}

} // namespace blokkily::engine
