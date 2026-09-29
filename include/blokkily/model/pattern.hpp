#pragma once

#include "blokkily/model/event.hpp"

#include <span>
#include <vector>

namespace blokkily {

class Pattern {
public:
    explicit Pattern(Tick length = 1920, Tick ticks_per_beat = 480);

    [[nodiscard]] EventId add(Trigger trigger);
    // Reinstates a trigger under the identifier it was saved with, so that a
    // reloaded project keeps the references a session already handed out.
    [[nodiscard]] bool restore(Trigger trigger);
    [[nodiscard]] bool update(EventId id, const Trigger& replacement);
    [[nodiscard]] bool remove(EventId id);
    [[nodiscard]] const Trigger* find(EventId id) const;
    [[nodiscard]] std::span<const Trigger> events() const noexcept { return events_; }
    [[nodiscard]] Tick length() const noexcept { return length_; }
    [[nodiscard]] Tick ticks_per_beat() const noexcept { return ticks_per_beat_; }

    // Controller movements (wave 4.1), sorted by tick; several on one tick
    // keep the order they were written in. A value of the same controller
    // already on that tick is replaced, so a take recorded over a pattern
    // leaves one value per controller per tick. Throws for a tick outside
    // the pattern or a value out of range.
    void add_continuous(const ContinuousEvent& event);
    [[nodiscard]] std::span<const ContinuousEvent> continuous() const noexcept {
        return continuous_;
    }
    // Removes every movement of `kind` (and `controller`, for a CC) in
    // [from, to); returns how many.
    std::size_t erase_continuous(ContinuousEvent::Kind kind, std::uint8_t controller, Tick from,
                                 Tick to);
    void clear_continuous() noexcept { continuous_.clear(); }
    // Whether `event` could be stored in a pattern this long.
    [[nodiscard]] static bool valid_continuous(const ContinuousEvent& event, Tick length) noexcept;
    // The same pattern made `length` ticks long. Every trigger that still
    // starts inside it keeps its identifier and everything it carries; a
    // trigger that no longer fits is dropped. Identifiers are never handed out
    // again, so a reference to a dropped trigger cannot come to mean another.
    [[nodiscard]] Pattern with_length(Tick length) const;

private:
    void validate(const Trigger& event) const;
    void sort();

    Tick length_;
    Tick ticks_per_beat_;
    EventId next_id_ = 1;
    std::vector<Trigger> events_;
    std::vector<ContinuousEvent> continuous_;
};

} // namespace blokkily

