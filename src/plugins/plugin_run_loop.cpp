#include "blokkily/plugins/plugin_run_loop.hpp"

#include <algorithm>
#include <atomic>
#include <limits>

namespace blokkily {
namespace {
std::atomic<PluginRunLoop*> installed{nullptr};
}

PluginRunLoop* plugin_run_loop() noexcept { return installed.load(std::memory_order_acquire); }

void set_plugin_run_loop(PluginRunLoop* loop) noexcept {
    installed.store(loop, std::memory_order_release);
}

std::uint64_t ManualRunLoop::add_timer(std::uint32_t period_ms, TimerCallback callback) {
    if (!callback) return 0;
    const auto period = std::max<std::uint32_t>(1, period_ms);
    const auto id = next_id_++;
    timers_.emplace(id, Timer{period, now_ + period,
                              std::make_shared<TimerCallback>(std::move(callback))});
    return id;
}

bool ManualRunLoop::remove_timer(std::uint64_t id) { return timers_.erase(id) > 0; }

bool ManualRunLoop::add_fd(int fd, std::uint32_t events, FdCallback callback) {
    if (fd < 0 || !callback || fds_.contains(fd)) return false;
    fds_.emplace(fd, Watch{events, std::make_shared<FdCallback>(std::move(callback))});
    return true;
}

bool ManualRunLoop::modify_fd(int fd, std::uint32_t events) {
    const auto found = fds_.find(fd);
    if (found == fds_.end()) return false;
    found->second.events = events;
    return true;
}

bool ManualRunLoop::remove_fd(int fd) { return fds_.erase(fd) > 0; }

std::size_t ManualRunLoop::advance(std::uint32_t milliseconds) {
    const auto target = now_ + milliseconds;
    std::size_t fired = 0;
    for (;;) {
        // The earliest timer due by the target; ties go to the older timer.
        auto next = timers_.end();
        for (auto it = timers_.begin(); it != timers_.end(); ++it)
            if (it->second.due <= target && (next == timers_.end() || it->second.due < next->second.due))
                next = it;
        if (next == timers_.end()) break;
        now_ = next->second.due;
        next->second.due += next->second.period;
        // Held by a copy, so a callback that removes its own timer is safe.
        const auto callback = next->second.callback;
        (*callback)();
        ++fired;
    }
    now_ = target;
    return fired;
}

bool ManualRunLoop::signal_fd(int fd, std::uint32_t events) {
    const auto found = fds_.find(fd);
    if (found == fds_.end()) return false;
    const auto wanted = events & found->second.events;
    if (wanted == 0) return false;
    const auto callback = found->second.callback;
    (*callback)(fd, wanted);
    return true;
}

std::uint32_t ManualRunLoop::watched_events(int fd) const {
    const auto found = fds_.find(fd);
    return found == fds_.end() ? 0U : found->second.events;
}

} // namespace blokkily
