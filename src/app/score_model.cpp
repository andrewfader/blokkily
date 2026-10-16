// The score view's projection of the canonical pattern: it re-spells the
// pattern through notation::to_staff on every pattern change and hands the
// QML a flat list of engraved objects. Read-only: it never writes back.

#include "score_model.hpp"

#include "pattern_model.hpp"

#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/layout.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/pitch.hpp"
#include "blokkily/notation/signature.hpp"

#include <climits>
#include <string>
#include <variant>

namespace notation = blokkily::notation;

ScoreModel::ScoreModel(const PatternModel* pattern, QObject* parent)
    : QObject(parent), pattern_(pattern) {
    if (pattern_ != nullptr) {
        connect(pattern_, &PatternModel::patternChanged,
                this, &ScoreModel::rebuild);
    }
    rebuild();
}

QStringList ScoreModel::clefNames() const {
    QStringList names;
    for (int i = 0; i <= static_cast<int>(notation::ClefKind::Percussion); ++i) {
        names << QString::fromStdString(
            std::string(notation::Clef(static_cast<notation::ClefKind>(i)).name()));
    }
    return names;
}

QString ScoreModel::clefName() const {
    const notation::Clef clef(static_cast<notation::ClefKind>(clef_kind_));
    return QString::fromStdString(std::string(clef.name()));
}

QString ScoreModel::keyLabel() const {
    return QString::fromStdString(notation::KeySignature(key_fifths_).label());
}

void ScoreModel::setClefKind(int kind) {
    if (kind < 0 || kind > static_cast<int>(notation::ClefKind::Percussion)) return;
    if (kind == clef_kind_) return;
    clef_kind_ = kind;
    rebuild();
}

void ScoreModel::setKeyFifths(int fifths) {
    if (fifths < -7 || fifths > 7 || fifths == key_fifths_) return;
    key_fifths_ = fifths;
    rebuild();
}

void ScoreModel::setUnmetered(bool unmetered) {
    if (unmetered == unmetered_) return;
    unmetered_ = unmetered;
    rebuild();
}

void ScoreModel::setTransposeOctaves(int octaves) {
    if (octaves == transpose_octaves_) return;
    transpose_octaves_ = octaves;
    rebuild();
}

void ScoreModel::refresh() { rebuild(); }

int ScoreModel::clefRefPos() const {
    const notation::Clef clef(static_cast<notation::ClefKind>(clef_kind_));
    if (!clef.pitched()) return 4;
    // The pitch the clef names: G4 for G clefs, F3 for F clefs, C4 for C clefs.
    notation::SpelledPitch ref{notation::Step::C, notation::Accidental::Natural, 4};
    switch (clef.kind()) {
        case notation::ClefKind::FrenchViolin:
        case notation::ClefKind::Treble:
            ref = {notation::Step::G, notation::Accidental::Natural, 4};
            break;
        case notation::ClefKind::BaritoneF:
        case notation::ClefKind::Bass:
        case notation::ClefKind::Subbass:
            ref = {notation::Step::F, notation::Accidental::Natural, 3};
            break;
        default:
            break; // C clefs keep C4
    }
    return notation::staff_position(ref, clef);
}

void ScoreModel::rebuild() {
    if (pattern_ == nullptr) return;
    const notation::Clef clef(static_cast<notation::ClefKind>(clef_kind_));
    const notation::KeySignature key(key_fifths_);
    // Sixteenth steps, 120 ticks each: 480 ticks per beat.
    constexpr blokkily::Tick ticks_per_beat = PatternModel::ticks_per_step * 4;
    notation::Staff staff = notation::to_staff(pattern_->pattern(), key, clef, ticks_per_beat);

    if (unmetered_) staff.time = notation::TimeSignature::unmetered();
    time_beats_ = staff.time.beats();
    time_beat_unit_ = staff.time.beat_unit();

    // Key-signature accidentals run through the sharp/flat order, each at the
    // octave nearest the previous accidental's position, preferring on-staff
    // positions. Readable, not Gould-perfect.
    key_sig_.clear();
    {
        static constexpr int sharp_order[7] = {3, 0, 4, 1, 5, 2, 6}; // F C G D A E B
        static constexpr int flat_order[7] = {6, 2, 5, 1, 4, 0, 3};  // B E A D G C F
        const int* order = key_fifths_ > 0 ? sharp_order : flat_order;
        const notation::Accidental acc =
            key_fifths_ > 0 ? notation::Accidental::Sharp : notation::Accidental::Flat;
        int prev = key_fifths_ > 0 ? 7 : 2; // first sharp high, first flat low
        for (int i = 0; i < std::abs(key_fifths_); ++i) {
            const notation::Step step = static_cast<notation::Step>(order[i]);
            int best_pos = 0, best_cost = INT_MAX;
            for (int oct = 0; oct <= 9; ++oct) {
                const int pos = notation::staff_position({step, acc, oct}, clef);
                int cost = std::abs(pos - prev);
                if (pos < 0 || pos > 8) cost += 8; // prefer on-staff
                if (cost < best_cost) { best_cost = cost; best_pos = pos; }
            }
            prev = best_pos;
            QVariantMap a;
            a.insert("acc", static_cast<int>(acc));
            a.insert("pos", best_pos);
            key_sig_.append(a);
        }
    }

    objects_.clear();
    for (const auto& measure : staff.measures) {
        for (const auto& object : measure.objects) {
            QVariantMap map;
            if (const auto* note = std::get_if<notation::NotationNote>(&object)) {
                notation::SpelledPitch written = note->spelled;
                written.octave += transpose_octaves_;
                const int pos = notation::staff_position(written, clef);
                map.insert("kind", "note");
                map.insert("pos", pos);
                map.insert("acc", static_cast<int>(written.accidental));
                map.insert("showAcc", notation::needs_accidental(written, key));
                map.insert("ledger", notation::ledger_lines(pos));
                map.insert("value", static_cast<int>(note->duration.value));
                map.insert("dots", note->duration.dots);
                // Chant staves draw bare noteheads; metered music keeps stems.
                map.insert("stemless", unmetered_ || note->duration.stemless);
                map.insert("stemUp", pos <= 4);
                map.insert("tieStart", note->tie_start);
                map.insert("lyric", QString::fromStdString(note->lyric));
            } else if (const auto* rest = std::get_if<notation::Rest>(&object)) {
                map.insert("kind", "rest");
                map.insert("value", static_cast<int>(rest->duration.value));
            }
            objects_.append(map);
        }
    }
    emit scoreChanged();
}
