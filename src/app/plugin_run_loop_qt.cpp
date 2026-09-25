#include "plugin_run_loop_qt.hpp"

#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>

QtPluginRunLoop::QtPluginRunLoop(QObject* parent) : QObject(parent) {}

QtPluginRunLoop::~QtPluginRunLoop() {
    for (auto& [id, timer] : timers_) delete timer;
    for (auto& [fd, watch] : watches_)
        for (auto* notifier : watch.notifiers) delete notifier;
}

std::uint64_t QtPluginRunLoop::add_timer(std::uint32_t period_ms, TimerCallback callback) {
    if (!callback) return 0;
    const auto id = next_id_++;
    auto* timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(static_cast<int>(std::max<std::uint32_t>(1, period_ms)));
    // Held by a copy, so a callback that removes its own timer is safe.
    auto shared = std::make_shared<TimerCallback>(std::move(callback));
    QObject::connect(timer, &QTimer::timeout, this, [shared] { (*shared)(); });
    timer->start();
    timers_.emplace(id, timer);
    return id;
}

bool QtPluginRunLoop::remove_timer(std::uint64_t id) {
    const auto found = timers_.find(id);
    if (found == timers_.end()) return false;
    found->second->stop();
    // Later, not now: the timer may be the one whose timeout is running.
    found->second->deleteLater();
    timers_.erase(found);
    return true;
}

void QtPluginRunLoop::apply(int fd, Watch& watch, std::uint32_t events) {
    constexpr std::array<std::uint32_t, 3> bits{fd_read, fd_write, fd_error};
    constexpr std::array<QSocketNotifier::Type, 3> types{
        QSocketNotifier::Read, QSocketNotifier::Write, QSocketNotifier::Exception};
    for (std::size_t index = 0; index < bits.size(); ++index) {
        auto*& notifier = watch.notifiers[index];
        const bool wanted = (events & bits[index]) != 0;
        if (wanted && notifier == nullptr) {
            notifier = new QSocketNotifier(fd, types[index], this);
            const auto callback = watch.callback;
            const auto bit = bits[index];
            QObject::connect(notifier, &QSocketNotifier::activated, this,
                             [callback, fd, bit] { (*callback)(fd, bit); });
        } else if (!wanted && notifier != nullptr) {
            notifier->setEnabled(false);
            notifier->deleteLater();
            notifier = nullptr;
        }
    }
}

bool QtPluginRunLoop::add_fd(int fd, std::uint32_t events, FdCallback callback) {
    if (fd < 0 || !callback || watches_.contains(fd)) return false;
    auto& watch = watches_[fd];
    watch.callback = std::make_shared<FdCallback>(std::move(callback));
    apply(fd, watch, events);
    return true;
}

bool QtPluginRunLoop::modify_fd(int fd, std::uint32_t events) {
    const auto found = watches_.find(fd);
    if (found == watches_.end()) return false;
    apply(fd, found->second, events);
    return true;
}

bool QtPluginRunLoop::remove_fd(int fd) {
    const auto found = watches_.find(fd);
    if (found == watches_.end()) return false;
    apply(fd, found->second, 0);
    watches_.erase(found);
    return true;
}
