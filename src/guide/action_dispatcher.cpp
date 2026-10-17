#include "blokkily/guide/action_dispatcher.hpp"
#include "blokkily/guide/narrator.hpp"

#include <cmath>

namespace blokkily::guide {
namespace {

int get_int(const ActionDispatcher::Args& args, const std::string& key, int fallback) {
    const auto it = args.find(key);
    if (it == args.end()) return fallback;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

double get_double(const ActionDispatcher::Args& args, const std::string& key,
                  double fallback) {
    const auto it = args.find(key);
    if (it == args.end()) return fallback;
    try {
        return std::stod(it->second);
    } catch (...) {
        return fallback;
    }
}

} // namespace

std::vector<std::string> ActionDispatcher::actions() const {
    return {"add-note",       "add-chord", "remove-note", "set-tempo",
            "toggle-playback", "add-track",      "select-instrument",
            "transpose-selection", "clear-pattern"};
}

std::string ActionDispatcher::describe(const std::string& id) const {
    if (id == "add-note") return "Write a note on a step";
    if (id == "add-chord") return "Write a chord on a step";
    if (id == "remove-note") return "Erase the note on a step";
    if (id == "set-tempo") return "Set the tempo in BPM";
    if (id == "toggle-playback") return "Start or stop playback";
    if (id == "add-track") return "Add a track";
    if (id == "select-instrument") return "Load an instrument by browser index";
    if (id == "transpose-selection") return "Move the selection by semitones";
    if (id == "clear-pattern") return "Erase the whole pattern (asks first)";
    return id;
}

bool ActionDispatcher::is_destructive(const std::string& id) const {
    return id == "clear-pattern";
}

DispatchResult ActionDispatcher::dispatch(const std::string& id, const Args& args,
                                          ICommandSink& sink, bool confirmed) {
    DispatchResult result;
    if (is_destructive(id) && !confirmed) {
        result.needs_confirm = true;
        result.message = "This erases the whole pattern. Are you sure?";
        return result;
    }
    const auto emit_done = [&](const std::string& label) {
        AppEvent event{EventSource::guide, EventKind::guide_action, label, ""};
        bus_.publish(event);
        result.ok = true;
        result.message = label;
    };

    if (id == "add-note") {
        const int step = get_int(args, "step", 0);
        const int key = get_int(args, "key", 60);
        if (key < 0 || key > 127) {
            result.message = "That key is outside the keyboard (0 to 127), so I left it alone.";
        } else if (sink.addNote(step, key)) {
            emit_done("I added " + Narrator::note_name(key) + " on step " +
                      std::to_string(step + 1) + " — hear how it sits in the loop?");
        } else {
            result.message = "That step is outside the pattern or already has a note, so I left it alone.";
        }
    } else if (id == "add-chord") {
        const int step = get_int(args, "step", 0);
        const int root = get_int(args, "root", 60);
        std::vector<std::int16_t> intervals;
        const auto it = args.find("intervals");
        if (it != args.end()) {
            std::string token;
            for (const char c : it->second + ",") {
                if (c == ',') {
                    try {
                        intervals.push_back(static_cast<std::int16_t>(std::stoi(token)));
                    } catch (...) {
                    }
                    token.clear();
                } else if (c != ' ') {
                    token += c;
                }
            }
        }
        if (intervals.empty()) intervals = {0, 4, 7};
        bool on_keyboard = true;
        for (const auto interval : intervals) on_keyboard = on_keyboard && root + interval >= 0 && root + interval <= 127;
        if (!on_keyboard) {
            result.message = "That chord runs off the keyboard (0 to 127), so I left it alone.";
        } else if (sink.addChord(step, root, intervals)) {
            std::string names;
            for (const auto iv : intervals)
                names += (names.empty() ? "" : " ") + Narrator::note_name(root + iv);
            emit_done("I laid down " + names + " on step " + std::to_string(step + 1) +
                      " — a chord is just stacked thirds. Hear how they fuse into one sound?");
        } else {
            result.message = "That step is outside the pattern or already has a note, so I left it alone.";
        }
    } else if (id == "remove-note") {
        const int step = get_int(args, "step", 0);
        if (sink.removeNote(step)) {
            emit_done("I cleared step " + std::to_string(step + 1) + ".");
        } else {
            result.message = "There's no note on that step to remove.";
        }
    } else if (id == "set-tempo") {
        const double bpm = get_double(args, "bpm", 120.0);
        if (bpm < 20.0 || bpm > 300.0) {
            result.message = "Tempos live between 20 and 300 BPM.";
        } else if (sink.setTempo(bpm)) {
            emit_done("I set the tempo to " + std::to_string(static_cast<int>(std::round(bpm))) +
                      " BPM.");
        }
    } else if (id == "toggle-playback") {
        sink.togglePlayback();
        emit_done("Toggled playback.");
    } else if (id == "add-track") {
        sink.addTrack();
        emit_done("I added a track — a new instrument lane for your idea.");
    } else if (id == "select-instrument") {
        const int index = get_int(args, "index", 0);
        if (sink.selectInstrument(index)) {
            emit_done("I loaded that instrument. The notes didn't change — only the voice playing them.");
        } else {
            result.message = "No instrument at that browser index.";
        }
    } else if (id == "transpose-selection") {
        const int semitones = get_int(args, "semitones", 0);
        if (semitones == 0) {
            result.message = "Zero semitones changes nothing — give me a number.";
        } else if (sink.transposeSelection(semitones)) {
            emit_done("I moved the selection " + std::to_string(semitones) +
                      (std::abs(semitones) == 1 ? " semitone." : " semitones.") +
                      (semitones == 7 || semitones == -7 ? " A fifth — the most natural move in harmony."
                                                        : semitones == 12 || semitones == -12 ? " An octave — same notes, new register."
                                                                                              : ""));
        } else {
            result.message = "Nothing selected to move.";
        }
    } else if (id == "clear-pattern") {
        if (sink.clearPattern()) {
            emit_done("Pattern cleared. Blank canvas — the \"Your first beat\" lesson loves those.");
        } else {
            result.message = "The pattern was already empty.";
        }
    } else {
        result.message = "I don't know how to do \"" + id + "\" yet.";
    }
    return result;
}

} // namespace blokkily::guide
