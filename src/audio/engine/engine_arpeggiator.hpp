#pragma once

// The arpeggiator stage (features/arpeggiator.feature): between a track's
// gathered events and its instrument. It takes the note-ons and note-offs
// out of the chunk's events, keeps the keys they hold, and puts back one
// note at a time on the arpeggiator's grid. Everything else passes through
// at its sample. Callback only, except set_arpeggiator(); it never
// allocates: its state is fixed-size and it writes into the engine's merge
// scratch, which the events were gathered through and is free again here.

#include "blokkily/model/arpeggiator.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace blokkily::engine {

struct ArpPlayback {
    // The settings, packed so the control thread can change them while the
    // callback runs (set_arpeggiator).
    std::atomic<std::uint64_t> settings{0};

    struct Held {
        std::int32_t key = 0;
        float velocity = 0.8F;
        double cents = 0.0;
        std::uint32_t order = 0;
        bool down = true; // false: let go, kept by the latch
    };
    static constexpr std::size_t capacity = 32;
    std::array<Held, capacity> held{};
    std::size_t held_count = 0;
    std::uint32_t pressed = 0;
    std::uint32_t step = 0;
    std::uint32_t random = 0x9E3779B9U;
    // The note the arpeggiator is sounding, if any, and the sample (on its
    // own running count) it ends at.
    bool sounding = false;
    std::int32_t sounding_key = 0;
    double sounding_cents = 0.0;
    std::uint64_t off_at = 0;
    // Samples rendered, whatever the transport did: what gate ends count in.
    std::uint64_t samples = 0;
    // The grid while the transport is stopped: ticks since it stopped.
    double free_tick = 0.0;
    bool was_enabled = false;
};

void set_arpeggiator(ArpPlayback& arp, const Arpeggiator& settings) noexcept;
// Forgets every key and the note sounding, as a fresh engine has none.
void reset_arpeggiator(ArpPlayback& arp) noexcept;

struct ArpChunk {
    // The chunk's first sample on the song, and how long it is.
    std::uint64_t song_position = 0;
    std::size_t frames = 0;
    // The song is playing: the grid follows its tempo map. Stopped, the
    // arpeggiator keeps its own time at the tempo where the song rests.
    bool rolling = false;
    // The transport stopped or jumped: what the latch kept is let go too.
    bool release = false;
    const TickClock* clock = nullptr;
    Tick ticks_per_beat = 480;
    double sample_rate = 48000.0;
};

// Rewrites events[0..count) through the arpeggiator, using `scratch` (at
// least as large as `events`), and returns the new count. Sorted in, sorted
// out by sample offset. Nothing changes while it is off, except that the
// note it was sounding when it was switched off is let go.
[[nodiscard]] std::size_t run_arpeggiator(ArpPlayback& arp, const ArpChunk& chunk,
                                          std::span<PluginEvent> events, std::size_t count,
                                          std::span<PluginEvent> scratch) noexcept;

} // namespace blokkily::engine
