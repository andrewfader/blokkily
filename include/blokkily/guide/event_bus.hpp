#pragma once

// Append-only, thread-safe feed of everything that mutates the session —
// whether the user did it, a future MCP client did it, or the guide itself
// did it. The narrator reads this feed; a future MCP server writes into it
// with source=mcp, which is why narration covers "either from mcp or user"
// with no extra work.

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace blokkily::guide {

enum class EventSource { user, mcp, guide, engine };

enum class EventKind {
    note_added,
    note_removed,
    step_toggled,
    transport_started,
    transport_stopped,
    tempo_changed,
    track_added,
    instrument_loaded,
    pattern_renamed,
    project_saved,
    project_loaded,
    new_project,
    bounce_finished,
    recording_toggled,
    view_changed,
    guide_action,   // the guide changed something itself
    lesson_step,    // a lesson advanced
    question_asked, // the user asked the guide something
};

[[nodiscard]] inline std::string to_string(EventSource source) {
    switch (source) {
        case EventSource::user: return "user";
        case EventSource::mcp: return "mcp";
        case EventSource::guide: return "guide";
        case EventSource::engine: return "engine";
    }
    return "user";
}

[[nodiscard]] inline std::string to_string(EventKind kind) {
    switch (kind) {
        case EventKind::note_added: return "note_added";
        case EventKind::note_removed: return "note_removed";
        case EventKind::step_toggled: return "step_toggled";
        case EventKind::transport_started: return "transport_started";
        case EventKind::transport_stopped: return "transport_stopped";
        case EventKind::tempo_changed: return "tempo_changed";
        case EventKind::track_added: return "track_added";
        case EventKind::instrument_loaded: return "instrument_loaded";
        case EventKind::pattern_renamed: return "pattern_renamed";
        case EventKind::project_saved: return "project_saved";
        case EventKind::project_loaded: return "project_loaded";
        case EventKind::new_project: return "new_project";
        case EventKind::bounce_finished: return "bounce_finished";
        case EventKind::recording_toggled: return "recording_toggled";
        case EventKind::view_changed: return "view_changed";
        case EventKind::guide_action: return "guide_action";
        case EventKind::lesson_step: return "lesson_step";
        case EventKind::question_asked: return "question_asked";
    }
    return "note_added";
}

struct AppEvent {
    EventSource source = EventSource::user;
    EventKind kind = EventKind::note_added;
    std::string label;   // short human summary, e.g. "Note C4 added at step 0"
    std::string detail;  // machine detail, e.g. "step=0 key=60"
    std::uint64_t seq = 0;
};

class EventBus {
public:
    using Listener = std::function<void(const AppEvent&)>;
    using Token = std::uint64_t;

    // Appends the event and notifies listeners (on the caller's thread).
    // Listeners must not emit from inside a callback.
    void publish(AppEvent event);

    // Returns a token for unsubscribe(). The listener stays valid until then.
    Token subscribe(Listener listener);
    void unsubscribe(Token token);

    // The last `count` events, oldest first. Bounded: keeps the tail.
    [[nodiscard]] std::vector<AppEvent> tail(std::size_t count) const;
    [[nodiscard]] std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::vector<AppEvent> events_;
    std::vector<std::pair<Token, Listener>> listeners_;
    std::uint64_t next_seq_ = 1;
    std::uint64_t next_token_ = 1;
    static constexpr std::size_t kKeep = 512;
};

} // namespace blokkily::guide
