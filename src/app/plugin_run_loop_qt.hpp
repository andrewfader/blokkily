#pragma once

// The application's plugin run loop (item 2.6): plugin timers are QTimers and
// watched descriptors are QSocketNotifiers, so a plugin's editor is served by
// the same event loop that draws the interface, on the main thread. main()
// installs one for the life of the application, before any plugin exists.

#include "blokkily/plugins/plugin_run_loop.hpp"

#include <QObject>

#include <array>
#include <cstdint>
#include <map>
#include <memory>

class QSocketNotifier;
class QTimer;

class QtPluginRunLoop final : public QObject, public blokkily::PluginRunLoop {
    Q_OBJECT
public:
    explicit QtPluginRunLoop(QObject* parent = nullptr);
    ~QtPluginRunLoop() override;

    std::uint64_t add_timer(std::uint32_t period_ms, TimerCallback callback) override;
    bool remove_timer(std::uint64_t id) override;
    bool add_fd(int fd, std::uint32_t events, FdCallback callback) override;
    bool modify_fd(int fd, std::uint32_t events) override;
    bool remove_fd(int fd) override;

    [[nodiscard]] std::size_t timer_count() const noexcept { return timers_.size(); }
    [[nodiscard]] std::size_t fd_count() const noexcept { return watches_.size(); }

private:
    struct Watch {
        std::shared_ptr<FdCallback> callback;
        // Read, write and error, each a notifier while it is asked for.
        std::array<QSocketNotifier*, 3> notifiers{};
    };
    void apply(int fd, Watch& watch, std::uint32_t events);
    std::map<std::uint64_t, QTimer*> timers_;
    std::map<int, Watch> watches_;
    std::uint64_t next_id_ = 1;
};
