#pragma once

// The main-thread run loop plugins are served from (item 2.6). A plugin's
// editor needs the host to call it back: CLAP plugins ask for timers
// (clap.timer-support) and for file descriptors to be watched
// (clap.posix-fd-support), and JUCE, which hosts VST3 editors, has its own
// message queue and X connection that someone has to pump. The adapters ask
// whatever run loop is installed; the application installs one backed by its
// event loop, and tests install a ManualRunLoop they advance by hand, so a
// timer firing or a descriptor becoming readable is a deterministic step.
//
// Everything here is main-thread only. Nothing on the audio thread touches it.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace blokkily {

class PluginRunLoop {
public:
    // What a watched descriptor is waiting for, and what it became. The bits
    // are CLAP's (CLAP_POSIX_FD_READ, _WRITE, _ERROR).
    enum FdEvents : std::uint32_t { fd_read = 1U << 0, fd_write = 1U << 1, fd_error = 1U << 2 };
    using TimerCallback = std::function<void()>;
    using FdCallback = std::function<void(int fd, std::uint32_t events)>;

    virtual ~PluginRunLoop() = default;
    // Calls `callback` every `period_ms` until removed. Returns a non-zero id,
    // or 0 when the timer could not be made.
    virtual std::uint64_t add_timer(std::uint32_t period_ms, TimerCallback callback) = 0;
    virtual bool remove_timer(std::uint64_t id) = 0;
    // Calls `callback` whenever `fd` is ready for one of `events`. One watch
    // per descriptor: a second add for the same one is refused.
    virtual bool add_fd(int fd, std::uint32_t events, FdCallback callback) = 0;
    virtual bool modify_fd(int fd, std::uint32_t events) = 0;
    virtual bool remove_fd(int fd) = 0;
};

// The run loop adapters register with, or nullptr when none is installed (a
// headless tool): a plugin asking for a timer is then told no.
[[nodiscard]] PluginRunLoop* plugin_run_loop() noexcept;
// Installs `loop` (nullptr uninstalls). The caller keeps it alive while it is
// installed and while any timer or descriptor registered with it remains.
void set_plugin_run_loop(PluginRunLoop* loop) noexcept;

// Installs a run loop for a scope and puts the previous one back after.
class ScopedPluginRunLoop {
public:
    explicit ScopedPluginRunLoop(PluginRunLoop& loop) noexcept
        : previous_(plugin_run_loop()) {
        set_plugin_run_loop(&loop);
    }
    ~ScopedPluginRunLoop() { set_plugin_run_loop(previous_); }
    ScopedPluginRunLoop(const ScopedPluginRunLoop&) = delete;
    ScopedPluginRunLoop& operator=(const ScopedPluginRunLoop&) = delete;

private:
    PluginRunLoop* previous_;
};

// A run loop driven by hand, for tests: time moves only when advance() says
// so, and a descriptor is ready only when signal_fd() says it is.
class ManualRunLoop final : public PluginRunLoop {
public:
    std::uint64_t add_timer(std::uint32_t period_ms, TimerCallback callback) override;
    bool remove_timer(std::uint64_t id) override;
    bool add_fd(int fd, std::uint32_t events, FdCallback callback) override;
    bool modify_fd(int fd, std::uint32_t events) override;
    bool remove_fd(int fd) override;

    // Moves time on by `milliseconds`, firing every timer that falls due, in
    // time order (a timer due several times fires several times). Returns how
    // many callbacks ran. Callbacks may add or remove timers while it runs.
    std::size_t advance(std::uint32_t milliseconds);
    // Reports `events` on `fd`. Calls the watch's callback with the events it
    // asked for, and returns whether it was called.
    bool signal_fd(int fd, std::uint32_t events);

    [[nodiscard]] std::size_t timer_count() const noexcept { return timers_.size(); }
    [[nodiscard]] std::size_t fd_count() const noexcept { return fds_.size(); }
    // The events `fd` is watched for, or 0 when it is not watched.
    [[nodiscard]] std::uint32_t watched_events(int fd) const;
    [[nodiscard]] std::uint64_t now() const noexcept { return now_; }

private:
    struct Timer {
        std::uint32_t period;
        std::uint64_t due;
        std::shared_ptr<TimerCallback> callback;
    };
    struct Watch {
        std::uint32_t events;
        std::shared_ptr<FdCallback> callback;
    };
    std::map<std::uint64_t, Timer> timers_;
    std::map<int, Watch> fds_;
    std::uint64_t next_id_ = 1;
    std::uint64_t now_ = 0;
};

} // namespace blokkily
