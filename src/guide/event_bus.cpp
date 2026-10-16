#include "blokkily/guide/event_bus.hpp"

namespace blokkily::guide {

void EventBus::publish(AppEvent event) {
    std::vector<Listener> listeners;
    {
        std::lock_guard lock(mutex_);
        event.seq = next_seq_++;
        events_.push_back(event);
        if (events_.size() > kKeep) events_.erase(events_.begin());
        listeners.reserve(listeners_.size());
        for (const auto& [token, listener] : listeners_) listeners.push_back(listener);
    }
    for (const auto& listener : listeners) listener(event);
}

EventBus::Token EventBus::subscribe(Listener listener) {
    std::lock_guard lock(mutex_);
    const auto token = next_token_++;
    listeners_.emplace_back(token, std::move(listener));
    return token;
}

void EventBus::unsubscribe(Token token) {
    std::lock_guard lock(mutex_);
    std::erase_if(listeners_,
                  [token](const auto& entry) { return entry.first == token; });
}

std::vector<AppEvent> EventBus::tail(std::size_t count) const {
    std::lock_guard lock(mutex_);
    const auto n = std::min(count, events_.size());
    return {events_.end() - static_cast<std::ptrdiff_t>(n), events_.end()};
}

std::size_t EventBus::size() const {
    std::lock_guard lock(mutex_);
    return events_.size();
}

} // namespace blokkily::guide
