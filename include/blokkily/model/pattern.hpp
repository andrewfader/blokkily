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

private:
    void validate(const Trigger& event) const;
    void sort();

    Tick length_;
    Tick ticks_per_beat_;
    EventId next_id_ = 1;
    std::vector<Trigger> events_;
};

} // namespace blokkily

