#pragma once

// Rule-based narration: maps AppEvents to plain-language explanations that
// say *why* something works, grounded in the actual model state handed in as
// a SongSnapshot. No LLM needed, nothing canned detached from the song.
// Theory Q&A (triads, cadences, ...) also lives here so the guide can answer
// without any network.

#include "blokkily/guide/event_bus.hpp"

#include <string>

namespace blokkily::guide {

// Plain data the app layer builds from SongModel/PatternModel/Transport so
// the narrator never hallucinates the user's song.
struct SongSnapshot {
    int pattern_steps = 16;      // steps in the current pattern
    int note_count = 0;          // notes currently in the pattern
    int song_note_count = 0;     // notes in every pattern of the song
    double tempo_bpm = 120.0;
    int beats_per_bar = 4;
    std::string scale_name = "major";
    std::string tuning_name = "12-TET";
    std::string root_name = "C";
    int track_count = 1;
    bool playing = false;
    bool recording = false;
};

class Narrator {
public:
    // One or two sentences about the event, with the why. Empty when the
    // event needs no narration (to avoid chatter).
    [[nodiscard]] std::string describe(const AppEvent& event,
                                       const SongSnapshot& snapshot) const;

    // Rule-based answer to a free-form theory/composition question. Empty
    // when nothing matches — the caller then tries the LLM or says so.
    [[nodiscard]] std::string answer_question(const std::string& query,
                                              const SongSnapshot& snapshot) const;

    // "C4", "F#3", ... for a MIDI key.
    [[nodiscard]] static std::string note_name(int midi_key);
};

} // namespace blokkily::guide
