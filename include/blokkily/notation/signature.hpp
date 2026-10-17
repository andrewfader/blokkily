#pragma once

// Key and time signatures. A KeySignature is a fifths count in [-7, 7]; a
// TimeSignature is a meter, or unmetered for chant (no signature drawn, no
// barlines).

#include "blokkily/notation/pitch.hpp"

#include <stdexcept>
#include <string>

namespace blokkily::notation {

class KeySignature {
public:
    explicit KeySignature(int fifths = 0) : fifths_(fifths) {
        if (fifths < -7 || fifths > 7)
            throw std::invalid_argument("key signature fifths must be in [-7, 7]");
    }

    [[nodiscard]] int fifths() const noexcept { return fifths_; }

    // The accidental this key imposes on a diatonic step: Natural when the key
    // does not touch the step.
    [[nodiscard]] Accidental accidental_for(Step step) const noexcept {
        const int s = static_cast<int>(step);
        if (fifths_ > 0) {
            // Order of sharps: F C G D A E B.
            constexpr int order[7] = {3, 0, 4, 1, 5, 2, 6};
            for (int i = 0; i < fifths_; ++i)
                if (order[i] == s) return Accidental::Sharp;
        } else {
            // Order of flats: B E A D G C F.
            constexpr int order[7] = {6, 2, 5, 1, 4, 0, 3};
            for (int i = 0; i < -fifths_; ++i)
                if (order[i] == s) return Accidental::Flat;
        }
        return Accidental::Natural;
    }

    // The major and relative-minor tonics, spelled as the key spells them.
    [[nodiscard]] SpelledPitch major_tonic() const noexcept {
        constexpr Step steps[15] = {Step::C, Step::G, Step::D, Step::A, Step::E,
                                    Step::B, Step::F, Step::C, Step::G, Step::D,
                                    Step::A, Step::E, Step::B, Step::F, Step::C};
        constexpr Accidental accs[15] = {Accidental::Flat,  Accidental::Flat,
                                         Accidental::Flat,  Accidental::Flat,
                                         Accidental::Flat,  Accidental::Flat,
                                         Accidental::Natural, Accidental::Natural,
                                         Accidental::Natural, Accidental::Natural,
                                         Accidental::Natural, Accidental::Natural,
                                         Accidental::Natural, Accidental::Sharp,
                                         Accidental::Sharp};
        // index by fifths+7: -7..-1 -> Cb..F, 0 -> C, 1..7 -> G..C#
        return SpelledPitch{steps[fifths_ + 7], accs[fifths_ + 7], 4};
    }

    [[nodiscard]] SpelledPitch minor_tonic() const noexcept {
        constexpr Step steps[15] = {Step::A, Step::E, Step::B, Step::F, Step::C,
                                    Step::G, Step::D, Step::A, Step::E, Step::B,
                                    Step::F, Step::C, Step::G, Step::D, Step::A};
        constexpr Accidental accs[15] = {Accidental::Flat,  Accidental::Flat,
                                         Accidental::Flat,  Accidental::Natural,
                                         Accidental::Natural, Accidental::Natural,
                                         Accidental::Natural, Accidental::Natural,
                                         Accidental::Natural, Accidental::Natural,
                                         Accidental::Sharp,   Accidental::Sharp,
                                         Accidental::Sharp,   Accidental::Sharp,
                                         Accidental::Sharp};
        return SpelledPitch{steps[fifths_ + 7], accs[fifths_ + 7], 4};
    }

    // "G major", "3 sharps" style label for menus.
    [[nodiscard]] std::string label() const;

    [[nodiscard]] bool operator==(const KeySignature& other) const noexcept {
        return fifths_ == other.fifths_;
    }
    [[nodiscard]] bool operator!=(const KeySignature& other) const noexcept {
        return !(*this == other);
    }

private:
    int fifths_;
};

class TimeSignature {
public:
    TimeSignature(int beats, int beat_unit) : beats_(beats), beat_unit_(beat_unit), metered_(true) {
        if (beats < 1) throw std::invalid_argument("time signature needs at least one beat");
        switch (beat_unit) {
            case 1: case 2: case 4: case 8: case 16: case 32: break;
            default: throw std::invalid_argument("beat unit must be a power of two up to 32");
        }
    }

    // Chant: no signature is drawn and no barlines fall. Notes still carry
    // durations; stemless noteheads are a per-note choice (NoteDuration).
    [[nodiscard]] static TimeSignature unmetered() noexcept {
        TimeSignature t(4, 4);
        t.metered_ = false;
        return t;
    }

    [[nodiscard]] int beats() const noexcept { return beats_; }
    [[nodiscard]] int beat_unit() const noexcept { return beat_unit_; }
    [[nodiscard]] bool metered() const noexcept { return metered_; }

    [[nodiscard]] std::string label() const {
        return metered_ ? std::to_string(beats_) + "/" + std::to_string(beat_unit_)
                        : std::string("unmetered");
    }

    [[nodiscard]] bool operator==(const TimeSignature& other) const noexcept {
        return beats_ == other.beats_ && beat_unit_ == other.beat_unit_ &&
               metered_ == other.metered_;
    }
    [[nodiscard]] bool operator!=(const TimeSignature& other) const noexcept {
        return !(*this == other);
    }

private:
    int beats_;
    int beat_unit_;
    bool metered_;
};

} // namespace blokkily::notation
