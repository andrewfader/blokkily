#include "blokkily/guide/narrator.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace blokkily::guide {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

// "either from mcp or user" — the source is always named, so the producer
// knows whether they did it, the guide did it, or an agent did it.
std::string who(EventSource source) {
    switch (source) {
        case EventSource::mcp: return "Your agent";
        case EventSource::guide: return "I";
        case EventSource::engine: return "The engine";
        case EventSource::user: return "You";
    }
    return "You";
}

} // namespace

std::string Narrator::note_name(int midi_key) {
    static const char* names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                  "F#", "G",  "G#", "A",  "A#", "B"};
    const int pc = ((midi_key % 12) + 12) % 12;
    const int octave = midi_key / 12 - 1;
    return std::string(names[pc]) + std::to_string(octave);
}

std::string Narrator::describe(const AppEvent& event, const SongSnapshot& snap) const {
    // The guide already said what it did in its own voice; the event stays
    // on the feed for lessons and history, but needs no narration.
    if (event.source == EventSource::guide) return "";
    const auto actor = who(event.source);
    switch (event.kind) {
        case EventKind::note_added: {
            std::string s = actor + " added " + event.label + ".";
            if (snap.note_count == 1)
                s += " That's the first note in the pattern — press play and you'll hear it loop.";
            else if (snap.note_count <= 4)
                s += " Small cells like this are how grooves start: loop it and listen for what wants to come next.";
            return s;
        }
        case EventKind::note_removed:
            return actor + " removed " + event.label + ". Space in a pattern is an instrument too — rests make the hits that stay feel bigger.";
        case EventKind::step_toggled:
            return actor + " toggled " + event.label + ".";
        case EventKind::transport_started:
            return snap.song_note_count == 0
                       ? "Playing — but the song has no notes yet, so you'll hear silence. Add some steps first."
                       : "Playing. Everything you hear is the pattern looping; edits you make now land on the next pass without stopping.";
        case EventKind::transport_stopped:
            return "Stopped. The pattern keeps everything you wrote — nothing is lost when the transport stops.";
        case EventKind::tempo_changed:
            return "Tempo is now " + std::to_string(static_cast<int>(snap.tempo_bpm)) +
                   " BPM. Tempo stretches time, not pitch: the notes keep their intervals, they just arrive " +
                   (snap.tempo_bpm > 130 ? "faster — energy up, room for detail down."
                                          : snap.tempo_bpm < 90 ? "slower — every note gets more weight."
                                                               : "at a comfortable middle pace.");
        case EventKind::track_added:
            return actor + " added a track. Tracks are independent instruments sharing one timeline — layer a bass under your chords here.";
        case EventKind::instrument_loaded:
            return event.label.empty() ? actor + " loaded an instrument."
                                       : actor + " loaded " + event.label +
                                             ". The notes didn't change — only the voice playing them did. Same melody, new character.";
        case EventKind::pattern_renamed:
            return "Pattern renamed to \"" + event.label + "\". Names are for you; the engine only cares about the notes.";
        case EventKind::project_saved:
            return "Saved. The whole session — patterns, clips, instruments, mixer — is in that file.";
        case EventKind::project_loaded:
            return "Session loaded: " + std::to_string(snap.track_count) + " track(s), " +
                   std::to_string(snap.note_count) + " note(s) in the open pattern.";
        case EventKind::new_project:
            return "Fresh session. One empty pattern, one track — the classic blank page. The \"Your first beat\" lesson will fill it in minutes.";
        case EventKind::bounce_finished:
            return "Bounce finished. That's the mix exactly as you auditioned it — what you heard is what's in the file.";
        case EventKind::recording_toggled:
            return snap.recording ? "Recording armed. What you play now is written into the pattern as you play it."
                                  : "Recording off. The take you just played is already in the pattern.";
        case EventKind::view_changed:
            return "Same pattern, new eyes: the tracker, piano roll and step grid are three projections of one thing, so an edit anywhere shows everywhere.";
        case EventKind::guide_action:
            return event.label; // the dispatcher already phrases these ("I moved ...")
        case EventKind::lesson_step:
            return event.label;
        case EventKind::question_asked:
            return "";
    }
    return "";
}

std::string Narrator::answer_question(const std::string& query,
                                      const SongSnapshot& snap) const {
    const auto q = lower(query);
    auto has = [&](const char* w) { return contains(q, w); };

    if ((has("triad") || has("chord")) && (has("what") || has("why") || has("how") || q.size() < 40)) {
        if (has("minor") || has("sad"))
            return "A minor triad is root + minor third (3 semitones) + perfect fifth (7 semitones) — e.g. A–C–E. "
                   "The minor third is the narrower, tenser interval, and our ears read that tension as darker or sadder "
                   "next to the wide-open major third (4 semitones). Try A–C–E against A–C#–E and feel the third move.";
        if (has("major") || has("happy"))
            return "A major triad is root + major third (4 semitones) + perfect fifth (7 semitones) — e.g. C–E–G. "
                   "The major third rings close to the natural harmonic series, which is why it sounds settled and bright.";
        if (has("diminished"))
            return "A diminished triad stacks two minor thirds (root + 3 + 6 semitones, e.g. B–D–F). The tritone "
                   "between root and fifth wants to resolve — that's why diminished chords sound unstable and pull somewhere.";
        if (has("augmented"))
            return "An augmented triad stacks two major thirds (root + 4 + 8 semitones, e.g. C–E–G#). "
                   "It divides the octave symmetrically, so it floats — no strong pull, useful for dreamy transitions.";
        if (has("seventh") || has("7th"))
            return "Seventh chords add a fourth note a seventh above the root. Major seventh (C–E–G–B) sounds lush; "
                   "dominant seventh (C–E–G–Bb) has a tritone between third and seventh that begs to resolve — "
                   "that's the engine of most cadences.";
        return "A triad is three notes stacked in thirds. Major (root+4+7 semitones) sounds bright, minor (root+3+7) "
               "sounds darker, diminished (root+3+6) sounds tense and wants to move. The third decides the mood; the fifth anchors it.";
    }
    if (has("cadence")) {
        if (has("perfect") || has("authentic"))
            return "A perfect (authentic) cadence is V–I: the dominant chord resolving to the tonic. The leading tone "
                   "rises a semitone to the tonic and the dominant seventh's tritone collapses — maximum resolution, the musical full stop.";
        if (has("plagal"))
            return "A plagal cadence is IV–I — the \"Amen\" cadence. Gentler than V–I because there's no leading tone pulling; it settles rather than lands.";
        if (has("deceptive") || has("interrupted"))
            return "A deceptive cadence is V–vi: everything promises home (I) and lands on vi instead. The surprise is the point — use it to extend a phrase.";
        if (has("half"))
            return "A half cadence ends ON the dominant (…–V). It doesn't resolve, it pauses — like a comma, or a question waiting for an answer.";
        return "Cadences are how phrases end: perfect V–I (full stop), plagal IV–I (gentle \"amen\"), deceptive V–vi (surprise), half …–V (comma).";
    }
    if (has("scale")) {
        if (has("pentatonic"))
            return "The minor pentatonic is five notes: root, minor third, fourth, fifth, minor seventh (e.g. A–C–D–E–G). "
                   "No semitones, so no wrong notes — that's why it's the improviser's safety net. Your session scale is " +
                   snap.scale_name + " on " + snap.root_name + ".";
        if (has("blues"))
            return "The blues scale is the minor pentatonic plus the flat fifth (the \"blue note\", e.g. Eb in A blues). "
                   "That tritone rubs against everything — bend into it and out of it, don't sit on it.";
        if (has("dorian"))
            return "Dorian is the natural minor scale with a raised sixth (e.g. D–E–F–G–A–B–C). That major sixth against a minor third "
                   "is its signature — jazzy, floating, neither sad nor happy.";
        return "Your session is in " + snap.root_name + " " + snap.scale_name + " (" + snap.tuning_name + "). " +
               "Scales are just agreed palettes: the fewer semitone clashes a scale has, the safer it is to wander in.";
    }
    if (has("bass")) {
        return "Good bass lines move by small steps and land on chord tones on strong beats. Roots on beat one anchor the harmony; "
               "walking up or down into the next chord's root (even chromatically) creates pull. Keep it simple — the bass is felt more than heard.";
    }
    if (has("melody")) {
        return "Singable melodies move mostly by step, leap occasionally, and always come back: a leap up wants a step down after it. "
               "Leave space — rests are phrasing. Try humming your pattern; if you can't hum it, simplify it.";
    }
    if (has("why") && has("sound") && (has("good") || has("nice") || has("great"))) {
        return "Things sound \"good\" mostly for three reasons: consonant intervals (octaves, fifths, thirds) with the bass, "
               "rhythmic alignment (accents landing together), and repetition with small variations. Your pattern has " +
               std::to_string(snap.note_count) + " notes — check whether the strong beats agree with each other.";
    }
    if (has("mix") || has("loud")) {
        return "Mixing is balance before polish: faders first until every part is heard, then pan to give each its own space, "
               "then effects. If two things fight, turn one down or move it aside — don't turn everything up.";
    }
    if (has("tempo") || has("bpm")) {
        return "You're at " + std::to_string(static_cast<int>(snap.tempo_bpm)) + " BPM. Tempo is feel: 90–100 strolls, "
               "120–128 dances, 140+ rushes. Change it while looping and trust your body over the number.";
    }
    if (has("what") && has("doing") && has("now")) {
        std::ostringstream s;
        s << "Right now: " << (snap.playing ? "playing" : "stopped") << " at "
          << static_cast<int>(snap.tempo_bpm) << " BPM, " << snap.note_count
          << " notes in the open pattern, " << snap.track_count << " track(s), in "
          << snap.root_name << " " << snap.scale_name << ".";
        return s.str();
    }
    return "";
}

} // namespace blokkily::guide
