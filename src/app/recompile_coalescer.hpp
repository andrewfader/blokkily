#pragma once

// Recompiles coalesced to at most one per turn of the event loop (plan F-D).
// A drag on a tempo lane, a clip or an automation lane asks for a recompile on
// every mouse move; compiling each one would waste the control thread and can
// run the engine's triple buffer out of free slots. Requests made in one turn
// share one recompile of whatever the song is when it runs, which is the
// latest edit, and a recompile the engine refuses for want of a free slot is
// tried again on a later turn rather than dropped. Needs no Qt: the caller
// supplies how to defer work to the next turn.

#include <functional>
#include <string>
#include <utility>

namespace blokkily {

class RecompileCoalescer {
public:
    // What a recompile reports: done, or refused with the engine's reason.
    struct Outcome {
        bool ok = false;
        std::string error;
    };
    using Recompile = std::function<Outcome()>;
    // Runs `work` on a later turn of the event loop.
    using Defer = std::function<void(std::function<void()> work)>;

    // The reason an engine gives when every arrangement slot is in use; only
    // that refusal is retried, because only it can clear up by itself.
    static constexpr const char* busy_reason = "no free arrangement slot";
    // How many turns a busy engine is retried for before the request is given
    // up; a later request starts again.
    static constexpr int maximum_retries = 64;

    RecompileCoalescer(Recompile recompile, Defer defer)
        : recompile_(std::move(recompile)), defer_(std::move(defer)) {}

    // Asks for a recompile. The first request in a turn schedules one; the
    // rest ride along with it.
    void request() {
        pending_ = true;
        retries_ = 0;
        schedule();
    }

    // Recompiles now if one is owed, without waiting for the deferred turn.
    // For a caller that is about to read the engine and needs it current.
    void flush() {
        if (!pending_) return;
        run();
    }

    // Forgets an owed recompile, for a caller that has just rebuilt the
    // engine from the current song.
    void cancel() noexcept {
        pending_ = false;
        retries_ = 0;
    }

    [[nodiscard]] bool pending() const noexcept { return pending_; }
    // How many recompiles actually ran, refused ones included.
    [[nodiscard]] int recompiles() const noexcept { return recompiles_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    void schedule() {
        if (scheduled_) return;
        scheduled_ = true;
        defer_([this] {
            scheduled_ = false;
            if (pending_) run();
        });
    }

    void run() {
        pending_ = false;
        ++recompiles_;
        auto outcome = recompile_();
        last_error_ = outcome.ok ? std::string{} : outcome.error;
        if (!outcome.ok && outcome.error == busy_reason && retries_ < maximum_retries) {
            ++retries_;
            pending_ = true;
            schedule();
        }
    }

    Recompile recompile_;
    Defer defer_;
    bool pending_ = false;
    bool scheduled_ = false;
    int retries_ = 0;
    int recompiles_ = 0;
    std::string last_error_;
};

} // namespace blokkily
