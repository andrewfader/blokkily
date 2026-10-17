#pragma once

// Every clef the staff view can draw, including the ones that survive mostly
// in early music and theory exams. A clef is a kind plus the diatonic position
// of the staff's bottom line; octave transposition (treble-8vb and friends) is
// a `transpose_octaves` field on Staff, not a clef of its own.

#include <string_view>

namespace blokkily::notation {

enum class ClefKind {
    FrenchViolin, // G clef on line 1
    Treble,       // G clef on line 2
    Soprano,      // C clef on line 1
    MezzoSoprano, // C clef on line 2
    Alto,         // C clef on line 3
    Tenor,        // C clef on line 4
    BaritoneC,    // C clef on line 5
    BaritoneF,    // F clef on line 3
    Bass,         // F clef on line 4
    Subbass,      // F clef on line 5
    Percussion    // unpitched
};

class Clef {
public:
    constexpr explicit Clef(ClefKind kind) noexcept : kind_(kind) {}

    [[nodiscard]] constexpr ClefKind kind() const noexcept { return kind_; }

    [[nodiscard]] constexpr bool pitched() const noexcept {
        return kind_ != ClefKind::Percussion;
    }

    // The conventional English name, for menus and MusicXML sign disambiguation.
    [[nodiscard]] constexpr std::string_view name() const noexcept {
        switch (kind_) {
            case ClefKind::FrenchViolin: return "French violin";
            case ClefKind::Treble: return "Treble";
            case ClefKind::Soprano: return "Soprano";
            case ClefKind::MezzoSoprano: return "Mezzo-soprano";
            case ClefKind::Alto: return "Alto";
            case ClefKind::Tenor: return "Tenor";
            case ClefKind::BaritoneC: return "Baritone (C)";
            case ClefKind::BaritoneF: return "Baritone (F)";
            case ClefKind::Bass: return "Bass";
            case ClefKind::Subbass: return "Subbass";
            case ClefKind::Percussion: return "Percussion";
        }
        return "Treble";
    }

    // Absolute diatonic index (pitch.hpp: C0 == 0) of the staff's bottom line.
    // The clef's named pitch sits on its reference line; each line above is two
    // diatonic steps higher. The two baritone clefs agree, as they should.
    [[nodiscard]] constexpr int bottom_line_diatonic() const noexcept {
        // (reference pitch diatonic index, reference line), bottom = ref - 2*(line-1)
        switch (kind_) {
            case ClefKind::FrenchViolin: return 32 - 2 * 0; // G4, line 1
            case ClefKind::Treble: return 32 - 2 * 1;       // G4, line 2
            case ClefKind::Soprano: return 28 - 2 * 0;     // C4, line 1
            case ClefKind::MezzoSoprano: return 28 - 2 * 1; // C4, line 2
            case ClefKind::Alto: return 28 - 2 * 2;        // C4, line 3
            case ClefKind::Tenor: return 28 - 2 * 3;       // C4, line 4
            case ClefKind::BaritoneC: return 28 - 2 * 4;   // C4, line 5
            case ClefKind::BaritoneF: return 24 - 2 * 2;   // F3, line 3
            case ClefKind::Bass: return 24 - 2 * 3;        // F3, line 4
            case ClefKind::Subbass: return 24 - 2 * 4;      // F3, line 5
            case ClefKind::Percussion: return 30; // treble-equivalent line map
        }
        return 30;
    }

    [[nodiscard]] constexpr bool operator==(const Clef& other) const noexcept {
        return kind_ == other.kind_;
    }
    [[nodiscard]] constexpr bool operator!=(const Clef& other) const noexcept {
        return !(*this == other);
    }

private:
    ClefKind kind_;
};

} // namespace blokkily::notation
