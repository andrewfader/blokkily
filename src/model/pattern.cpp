#include "blokkily/model/pattern.hpp"

#include <algorithm>
#include <stdexcept>

namespace blokkily {

Pattern::Pattern(Tick length, Tick ticks_per_beat)
    : length_(length), ticks_per_beat_(ticks_per_beat) {
    if (length <= 0 || ticks_per_beat <= 0) {
        throw std::invalid_argument("pattern timing values must be positive");
    }
}

void Pattern::validate(const Trigger& event) const {
    if (event.start < 0 || event.start >= length_ || event.duration <= 0) {
        throw std::invalid_argument("event is outside the pattern");
    }
    if (event.probability < 0.0F || event.probability > 1.0F) {
        throw std::invalid_argument("probability must be in [0, 1]");
    }
    if (event.ratchets == 0 || event.ratchets > 16) {
        throw std::invalid_argument("ratchets must be in [1, 16]");
    }
    if (const auto* chord = std::get_if<Chord>(&event.musical_data)) {
        const auto voices = chord->intervals.size();
        const auto in_range = [](float velocity) { return velocity >= 0.0F && velocity <= 1.0F; };
        if (!in_range(chord->velocity))
            throw std::invalid_argument("chord velocity must be in [0, 1]");
        if (!chord->velocities.empty() && chord->velocities.size() != voices)
            throw std::invalid_argument("a chord needs one velocity per voice or none");
        if (!std::all_of(chord->velocities.begin(), chord->velocities.end(), in_range))
            throw std::invalid_argument("voice velocity must be in [0, 1]");
        if (!chord->durations.empty() && chord->durations.size() != voices)
            throw std::invalid_argument("a chord needs one length per voice or none");
        if (!std::all_of(chord->durations.begin(), chord->durations.end(),
                         [](Tick length) { return length > 0; }))
            throw std::invalid_argument("voice length must be positive");
    }
}

EventId Pattern::add(Trigger trigger) {
    validate(trigger);
    trigger.id = next_id_++;
    const auto id = trigger.id;
    events_.push_back(std::move(trigger));
    sort();
    return id;
}

bool Pattern::restore(Trigger trigger) {
    if (trigger.id == 0 || find(trigger.id) != nullptr) return false;
    validate(trigger);
    next_id_ = std::max(next_id_, trigger.id + 1);
    events_.push_back(std::move(trigger));
    sort();
    return true;
}

bool Pattern::update(EventId id, const Trigger& replacement) {
    validate(replacement);
    auto* current = const_cast<Trigger*>(find(id));
    if (current == nullptr) return false;
    *current = replacement;
    current->id = id;
    sort();
    return true;
}

bool Pattern::remove(EventId id) {
    const auto old_size = events_.size();
    std::erase_if(events_, [id](const Trigger& event) { return event.id == id; });
    return events_.size() != old_size;
}

const Trigger* Pattern::find(EventId id) const {
    const auto it = std::find_if(events_.begin(), events_.end(),
        [id](const Trigger& event) { return event.id == id; });
    return it == events_.end() ? nullptr : &*it;
}

Pattern Pattern::with_length(Tick length) const {
    Pattern resized(length, ticks_per_beat_);
    for (const auto& event : events_)
        if (event.start < length) resized.events_.push_back(event);
    resized.next_id_ = next_id_;
    return resized;
}

void Pattern::sort() {
    std::stable_sort(events_.begin(), events_.end(), [](const Trigger& a, const Trigger& b) {
        return a.start < b.start || (a.start == b.start && a.id < b.id);
    });
}

} // namespace blokkily

