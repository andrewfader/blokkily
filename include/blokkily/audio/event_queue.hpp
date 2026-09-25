#pragma once

#include "blokkily/model/event.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace blokkily {

// A fixed ring with one writer and one reader, for handing events across
// threads that must never wait on each other: a keyboard or a MIDI port to the
// render callback, and the render callback back to the interface. Neither side
// allocates, locks, or blocks. A full ring refuses the push rather than making
// the writer wait, because a dropped note is better than a stalled callback.
template <typename T, std::size_t Capacity>
class SpscQueue {
public:
    bool push(const T& value) noexcept {
        const auto written = written_.load(std::memory_order_relaxed);
        if (written - read_.load(std::memory_order_acquire) >= Capacity) return false;
        items_[written % Capacity] = value;
        written_.store(written + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& value) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == written_.load(std::memory_order_acquire)) return false;
        value = items_[read % Capacity];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    std::array<T, Capacity> items_{};
    std::atomic<std::size_t> written_{0};
    std::atomic<std::size_t> read_{0};
};

// An event played from outside the arrangement, addressed to the track that
// should sound it.
struct RoutedEvent {
    std::uint32_t track = 0;
    PluginEvent event{};
};

// What a MIDI port hands the render callback.
using InputQueue = SpscQueue<RoutedEvent, 256>;

// An input event as the engine played it: the track it sounded on and where
// the song was when it did. This is what a take is recorded from, so what is
// written is what was heard, at the position it was heard. `tick` is the song
// tick at `sample` under the clock the render callback was playing when it
// stamped the event, so a tempo edit compiled before the take is drained
// cannot move a note that was already heard.
struct CapturedEvent {
    std::uint32_t track = 0;
    std::uint64_t sample = 0;
    Tick tick = 0;
    PluginEvent event{};
};

} // namespace blokkily
