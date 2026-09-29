#include "blokkily/model/pattern.hpp"

#include <algorithm>
#include <numeric>
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
    {
        const auto* chord = std::get_if<Chord>(&event.musical_data);
        const std::size_t voices = chord == nullptr ? 1 : chord->intervals.size();
        for (const auto& expression : event.expression) {
            if (expression.voice >= voices)
                throw std::invalid_argument("note expression names a voice the step does not have");
            if (expression.offset < 0)
                throw std::invalid_argument("note expression starts before its note");
            if (!NoteExpression::valid_value(expression.kind, expression.value))
                throw std::invalid_argument("note expression value is out of range");
        }
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

bool Pattern::valid_continuous(const ContinuousEvent& event, Tick length) noexcept {
    if (event.tick < 0 || event.tick >= length) return false;
    switch (event.kind) {
    case ContinuousEvent::Kind::pitch_bend: return event.value <= 16383 && event.controller == 0;
    case ContinuousEvent::Kind::channel_pressure: return event.value <= 127 && event.controller == 0;
    case ContinuousEvent::Kind::control_change:
        return event.value <= 127 && event.controller < 120 && event.controller != 64;
    case ContinuousEvent::Kind::poly_pressure:
        return event.value <= 127 && event.controller <= 127;
    }
    return false;
}

void Pattern::add_continuous(const ContinuousEvent& event) {
    if (!valid_continuous(event, length_))
        throw std::invalid_argument("controller event is outside the pattern or its range");
    const auto same = std::find_if(continuous_.begin(), continuous_.end(),
                                   [&event](const ContinuousEvent& held) {
                                       return held.tick == event.tick && held.kind == event.kind &&
                                              held.controller == event.controller;
                                   });
    if (same != continuous_.end()) {
        same->value = event.value;
        return;
    }
    const auto after = std::upper_bound(
        continuous_.begin(), continuous_.end(), event.tick,
        [](Tick tick, const ContinuousEvent& held) { return tick < held.tick; });
    continuous_.insert(after, event);
}

std::size_t Pattern::erase_continuous(ContinuousEvent::Kind kind, std::uint8_t controller,
                                      Tick from, Tick to) {
    return std::erase_if(continuous_, [&](const ContinuousEvent& held) {
        return held.kind == kind && held.controller == controller && held.tick >= from &&
               held.tick < to;
    });
}

Pattern Pattern::with_length(Tick length) const {
    Pattern resized(length, ticks_per_beat_);
    for (const auto& event : events_)
        if (event.start < length) resized.events_.push_back(event);
    for (const auto& event : continuous_)
        if (event.tick < length) resized.continuous_.push_back(event);
    resized.next_id_ = next_id_;
    return resized;
}

void Pattern::sort() {
    // GCC 16's libstdc++ false-positives -Wmaybe-uninitialized inside the
    // insertion-sort path of std::stable_sort whenever the value type holds
    // a std::variant (note: the diagnostic escapes a #pragma diagnostic
    // ignored because the warning is raised inside an inlined destructor
    // in <bits/move.h>, not at the call site). Sort an index permutation
    // instead — behaviourally identical because id is unique and already
    // breaks ties, and avoids the move-construction codegen the warning
    // tracks.
    const auto n = events_.size();
    if (n < 2) return;
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(order.begin(), order.end(),
        [this](std::size_t a, std::size_t b) {
            const auto& x = events_[a];
            const auto& y = events_[b];
            if (x.start != y.start) return x.start < y.start;
            return x.id < y.id;
        });
    std::vector<Trigger> reordered;
    reordered.reserve(n);
    for (auto i : order) reordered.push_back(std::move(events_[i]));
    events_ = std::move(reordered);
}

} // namespace blokkily

