#pragma once

// Recording automation (item 3.1; decision 3). While the transport plays, a
// control the producer moves - a strip's fader, pan or mute, or a parameter
// turned in a plugin's own window - is recorded as a pass over its lane: it
// opens where the control was taken hold of, collects every value it was
// moved to at the tick it was heard, and closes where it was let go (touch)
// or where the transport stopped (latch, write). A closed pass is written
// into the song with AutomationLane::write_pass, so the lane outside the pass
// is kept, and then thinned back to the points it needs.
//
// A lane that did not exist is made for the pass, starting from the value the
// control had before it was touched, so the song sounds as it did everywhere
// the pass did not reach. Control thread only; the engine delivers the moves
// (SongEngine::take_strip_move, take_plugin_edit).

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/model/song.hpp"

#include <cstddef>
#include <vector>

namespace blokkily {

class AutomationTake {
public:
    explicit AutomationTake(Tick song_length = 0) : length_(song_length) {}
    // Where the song wraps, so a pass that runs over the loop point is split
    // there.
    void set_length(Tick song_length) { length_ = song_length; }

    // The producer took hold of a control at `at`; `before` is the value it
    // had just before. A pass already open on it carries on.
    void touch(std::size_t track, const AutomationTarget& target, Tick at, double before);
    // The control is at `value` from `at`. Opens a pass if none is open (the
    // value before it taken to be `value`).
    void value(std::size_t track, const AutomationTarget& target, Tick at, double value);
    // Let go at `at`: the pass closes there.
    void release(std::size_t track, const AutomationTarget& target, Tick at);
    // The transport stopped at `at`: every pass still open closes there.
    void finish(Tick at);

    // What the engine hands back, recorded as the mode of the track that
    // owns the lane says (decision 3): nothing for off and read; for touch a
    // pass from the take of hold to the release; for latch and write a pass
    // from the take of hold to the stop. A move is a pass's step: the value
    // it left holds until the next one, which is what was heard.
    void record(const Song& song, const StripMoveEvent& played);
    // A plugin's own edit (the engine's edit ring), at `tick`, while the
    // transport played (`edit.rolling`). The lane belongs to the track the
    // processor sits on; an edit of a return's or the master's processor is
    // recorded into the track that already holds a lane for it, and not at
    // all when none does. `fallback` is the parameter's value before the
    // edit when no edit has reported it yet (the plugin's default).
    void record(const Song& song, const PluginEditEvent& edit, Tick tick, double fallback);
    // Whether an edit has reported the parameter's value yet.
    [[nodiscard]] bool knows(ProcessorAddress where, std::int32_t parameter) const;

    [[nodiscard]] bool open(std::size_t track, const AutomationTarget& target) const;
    // Whether any pass is open or waiting to be written.
    [[nodiscard]] bool active() const noexcept { return !passes_.empty(); }
    // Whether a closed pass is waiting for commit().
    [[nodiscard]] bool ready() const noexcept;

    // Writes every closed pass into its lane in `song`: the lane of the
    // pass's track, made if the track has none for the control. A pass that
    // never moved its control makes no new lane. Returns how many lanes were
    // written. Open passes stay open.
    std::size_t commit(Song& song);
    // Forgets every pass, open or not (the values the plugins reported are
    // kept).
    void clear() { passes_.clear(); }

    // Moves further apart than this, in ticks, are steps; closer ones are a
    // ramp (1/20 of a beat at 480 ticks a beat: 25 ms at 120 bpm, longer
    // than an interface's frame).
    static constexpr Tick step_gap = 24;
    // How far a thinned point may be from the ramp that replaces it, in the
    // control's units.
    [[nodiscard]] static double tolerance(const AutomationTarget& target) noexcept;

private:
    struct Pass {
        std::size_t track = 0;
        AutomationTarget target;
        Tick from = 0;
        Tick to = 0;
        double before = 0.0;
        std::vector<AutomationPoint> points;
        bool open = true;
        bool changed = false;
    };
    Pass* find_open(std::size_t track, const AutomationTarget& target);
    // Closes `pass` at `at`, splitting it at the loop point if the song
    // wrapped since its last point.
    void close(Pass& pass, Tick at);
    // The pass wrapped: its part up to the loop point is closed, and the
    // rest starts again at tick 0 from the value it had.
    void wrap(Pass& pass);

    struct KnownValue {
        ProcessorAddress where;
        std::int32_t parameter = 0;
        double value = 0.0;
    };

    Tick length_ = 0;
    std::vector<Pass> passes_;
    // The last value every plugin parameter was reported at: where a new lane
    // for it starts from.
    std::vector<KnownValue> known_;
};

} // namespace blokkily
