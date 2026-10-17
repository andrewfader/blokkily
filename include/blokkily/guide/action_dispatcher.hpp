#pragma once

// The guide's hands. Every change the guide makes to the session goes through
// here, and from here into the same command functions the QML buttons call —
// never by poking the model directly. Each performed action emits an AppEvent
// with source=guide, so the narration feed stays truthful about who did what.
// Destructive actions (clearing a pattern, …) are not performed until the
// caller passes confirmed=true; the first call returns needs_confirm so the
// bubble can ask the producer.
//
// This is the in-process half of the "guide acts" story. Out of process, the
// same commands are reachable through the MCP server's tools; a future MCP
// server emits source=mcp events into the same bus.

#include "blokkily/guide/event_bus.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace blokkily::guide {

// The command functions the guide may call, implemented by the app layer on
// top of the same AppController/PatternModel methods the QML buttons call.
struct ICommandSink {
    virtual ~ICommandSink() = default;
    virtual bool addNote(int step, int key) = 0;
    virtual bool addChord(int step, int root, const std::vector<std::int16_t>& intervals) = 0;
    virtual bool removeNote(int step) = 0;
    virtual bool setTempo(double bpm) = 0;
    virtual void togglePlayback() = 0;
    virtual void addTrack() = 0;
    virtual bool selectInstrument(int index) = 0;
    virtual bool transposeSelection(int semitones) = 0;
    virtual bool clearPattern() = 0; // destructive: confirm first
};

struct DispatchResult {
    bool ok = false;
    bool needs_confirm = false; // destructive action, awaiting the producer
    std::string message;        // what happened, in the guide's voice
};

class ActionDispatcher {
public:
    using Args = std::map<std::string, std::string>;

    explicit ActionDispatcher(EventBus& bus) : bus_(bus) {}

    // The action ids the guide (and lessons) may use.
    [[nodiscard]] std::vector<std::string> actions() const;
    // One-line description for UI/tooltips.
    [[nodiscard]] std::string describe(const std::string& id) const;
    [[nodiscard]] bool is_destructive(const std::string& id) const;

    // Performs the action. Pass confirmed=true after the producer confirmed
    // a destructive one. Emits source=guide on success.
    DispatchResult dispatch(const std::string& id, const Args& args,
                            ICommandSink& sink, bool confirmed = false);

private:
    EventBus& bus_;
};

} // namespace blokkily::guide
