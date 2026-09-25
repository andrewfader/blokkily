#pragma once

// Turns the plugins' own parameter edits, as the engine's edit ring delivers
// them (plan F-D), into steps of history (decision 12): a knob turned in a
// plugin's window is one undoable step per gesture, however many values the
// turn sends. A gesture on one processor is begin, values, end; while any
// gesture on a processor is open its values are part of it, and the step is
// taken when the last one closes. A value that arrives outside any gesture
// (a plugin moving an output meter, or catching up after a state load) is not
// a producer's knob turn and makes no step. Control thread only; needs no Qt.

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/model/processor_address.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace blokkily {

class EditGestures {
public:
    // A gesture that has just finished: the processor it moved, and the last
    // value it left each parameter at.
    struct Completed {
        ProcessorAddress where;
        std::int32_t parameter = 0;   // the parameter the gesture ended on
        double value = 0.0;           // its last value
    };

    // Feeds one edit. Returns the finished gesture when this edit closes the
    // last one open on its processor.
    std::optional<Completed> feed(const PluginEditEvent& event) {
        auto& processor = entry(event.where);
        const auto& edit = event.edit;
        switch (edit.kind) {
        case ParameterEdit::Kind::begin:
            ++processor.open;
            processor.moved = false;
            break;
        case ParameterEdit::Kind::value:
            processor.parameter = edit.parameter;
            processor.value = edit.value;
            if (processor.open > 0) processor.moved = true;
            last_ = Completed{event.where, edit.parameter, edit.value};
            break;
        case ParameterEdit::Kind::end:
            if (processor.open == 0) break;   // an end without its begin
            if (--processor.open > 0) break;
            if (!processor.moved) break;       // a click that changed nothing
            processor.moved = false;
            return Completed{event.where, processor.parameter, processor.value};
        }
        return std::nullopt;
    }

    // The last value any edit carried, for a readout.
    [[nodiscard]] const std::optional<Completed>& last_value() const noexcept { return last_; }
    // Whether a gesture is open on `where` right now.
    [[nodiscard]] bool open(ProcessorAddress where) const {
        for (const auto& processor : processors_)
            if (processor.where == where) return processor.open > 0;
        return false;
    }
    // Forgets every gesture in flight, for an engine that is being replaced:
    // what it half-reported cannot be finished by the next one.
    void clear() {
        processors_.clear();
        last_.reset();
    }

private:
    struct Processor {
        ProcessorAddress where;
        int open = 0;
        bool moved = false;
        std::int32_t parameter = 0;
        double value = 0.0;
    };
    Processor& entry(ProcessorAddress where) {
        for (auto& processor : processors_)
            if (processor.where == where) return processor;
        processors_.push_back({where});
        return processors_.back();
    }
    std::vector<Processor> processors_;
    std::optional<Completed> last_;
};

} // namespace blokkily
