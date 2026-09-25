#include "blokkily/sequencer/take.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {

TakeRecorder::TakeRecorder(Tick song_length) : length_(std::max<Tick>(1, song_length)) {}

void TakeRecorder::note_on(Tick at, std::int16_t key, float velocity, double cents) {
    held_.push_back({at, 1, key, velocity, cents});
}

std::optional<PlayedNote> TakeRecorder::note_off(Tick at, std::int16_t key) {
    const auto found = std::find_if(held_.begin(), held_.end(),
                                    [key](const PlayedNote& note) { return note.key == key; });
    if (found == held_.end()) return std::nullopt;
    auto note = *found;
    held_.erase(found);
    // Read round the loop: a release that reads earlier than its press came
    // after the song wrapped.
    const Tick held_for = ((at - note.start) % length_ + length_) % length_;
    note.duration = std::max<Tick>(1, held_for);
    return note;
}

std::vector<PlayedNote> TakeRecorder::finish(Tick at) {
    std::vector<PlayedNote> released;
    while (!held_.empty()) {
        if (auto note = note_off(at, held_.front().key)) released.push_back(*note);
    }
    return released;
}

TakeTarget take_target(const Song& song, std::size_t track, Tick tick,
                       std::size_t open_pattern) {
    for (const auto& clip : song.clips) {
        if (clip.track != track || clip.pattern >= song.patterns.size()) continue;
        const Tick length = song.patterns[clip.pattern].pattern.length();
        const Tick span = length * static_cast<Tick>(std::max<std::uint32_t>(1, clip.repeats));
        if (tick >= clip.start && tick < clip.start + span)
            return {clip.pattern, (tick - clip.start) % length};
    }
    const auto pattern = std::min(open_pattern, song.patterns.size() - 1);
    const Tick length = song.patterns[pattern].pattern.length();
    return {pattern, ((tick % length) + length) % length};
}

namespace {
struct Voice {
    std::int16_t key;
    double cents;
};

// The pitches a step sounds, the way the scheduler voices them.
std::vector<Voice> voices_of(const Trigger& trigger) {
    if (const auto* note = std::get_if<Note>(&trigger.musical_data))
        return {{note->key, note->cents}};
    const auto& chord = std::get<Chord>(trigger.musical_data);
    std::vector<Voice> voices;
    const auto size = static_cast<int>(chord.intervals.size());
    for (int voice = 0; voice < size; ++voice) {
        const int source = (voice + chord.inversion % size + size) % size;
        const int octave = (voice + chord.inversion) >= size ? 12 : 0;
        const double retune = static_cast<std::size_t>(source) < chord.cents.size()
                                  ? chord.cents[static_cast<std::size_t>(source)]
                                  : 0.0;
        voices.push_back({static_cast<std::int16_t>(chord.root + chord.intervals[source] + octave),
                          retune});
    }
    return voices;
}

// Within a hundredth of a cent: a retune that has travelled through a
// single-precision key map is still the degree it was.
bool same_pitch(const Voice& left, const Voice& right) {
    return left.key == right.key && std::abs(left.cents - right.cents) < 0.01;
}
} // namespace

int write_played(Pattern& pattern, PlayedNote note, Tick ticks_per_step) {
    const Tick length = pattern.length();
    const Tick steps = std::max<Tick>(1, length / ticks_per_step);
    const Tick at = ((note.start % length) + length) % length;
    // The nearest step, and how far from it the note was played. A note played
    // just before the downbeat belongs to the downbeat of the next loop.
    Tick step = (at + ticks_per_step / 2) / ticks_per_step;
    Tick micro = at - step * ticks_per_step;
    if (step >= steps) {
        step = 0;
        micro = at - length;
    }
    const Tick reach = ticks_per_step / 2 - 1;
    micro = std::clamp(micro, -reach, reach);
    const Tick duration = std::clamp<Tick>(note.duration, 1, length);
    const Voice played{note.key, note.cents};

    const auto events = pattern.events();
    const auto existing = std::find_if(events.begin(), events.end(),
        [step, ticks_per_step](const Trigger& trigger) {
            return trigger.start / ticks_per_step == step;
        });
    if (existing == events.end()) {
        Trigger trigger;
        trigger.start = step * ticks_per_step;
        trigger.duration = duration;
        trigger.micro_offset = micro;
        trigger.musical_data = Note{note.key, note.velocity, 0.0F, note.cents};
        (void)pattern.add(trigger);
        return static_cast<int>(step);
    }

    auto voices = voices_of(*existing);
    if (std::any_of(voices.begin(), voices.end(),
                    [&played](const Voice& voice) { return same_pitch(voice, played); }))
        return static_cast<int>(step);
    voices.push_back(played);
    std::sort(voices.begin(), voices.end(), [](const Voice& left, const Voice& right) {
        return left.key < right.key || (left.key == right.key && left.cents < right.cents);
    });

    // The step keeps everything the producer set on it — its locks, its
    // ratchets, its timing — and gains a voice.
    Trigger merged = *existing;
    Chord chord;
    if (const auto* held = std::get_if<Chord>(&existing->musical_data)) chord.strum = held->strum;
    chord.root = voices.front().key;
    chord.intervals.clear();
    for (const auto& voice : voices) {
        chord.intervals.push_back(static_cast<std::int16_t>(voice.key - chord.root));
        chord.cents.push_back(voice.cents);
    }
    merged.musical_data = chord;
    merged.duration = std::max(merged.duration, duration);
    (void)pattern.update(merged.id, merged);
    return static_cast<int>(step);
}

} // namespace blokkily
