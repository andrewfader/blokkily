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

void Pattern::sort() {
    std::stable_sort(events_.begin(), events_.end(), [](const Trigger& a, const Trigger& b) {
        return a.start < b.start || (a.start == b.start && a.id < b.id);
    });
}

} // namespace blokkily

