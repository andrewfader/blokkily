// The score view's projection of the canonical pattern: the open part as
// staff notation (notation::to_staff) under the song's clef, key and meter
// for it, laid out for the QML to draw. Read-only towards the pattern.

#include "score_model.hpp"

#include "pattern_model.hpp"
#include "song_model.hpp"

#include "blokkily/notation/layout.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <variant>

namespace notation = blokkily::notation;
using blokkily::Tick;

namespace {

// Room a column takes for the time until the next one starts, in staff
// spaces: longer values take more room, but less than in proportion.
double room_for(Tick ticks, Tick ticks_per_beat) {
    const double beats = static_cast<double>(std::max<Tick>(ticks, 1)) / static_cast<double>(ticks_per_beat);
    return 1.8 + 2.4 * std::pow(beats, 0.6);
}

constexpr double kAccidentalRoom = 1.4;
constexpr double kBarPadding = 1.2;

// Where the key signature's accidentals sit on a treble staff, in the order
// they are written; other clefs shift the pattern to the same letters.
constexpr int kSharpTreble[7] = {8, 5, 9, 6, 3, 7, 4}; // F C G D A E B
constexpr int kFlatTreble[7] = {4, 7, 3, 6, 2, 5, 1};  // B E A D G C F

struct Entry {
    std::size_t measure;
    Tick onset;   // ticks from the start of the part
    Tick length;
    const notation::StaffObject* object;
    int voice;
};

} // namespace

ScoreModel::ScoreModel(const PatternModel* pattern, SongModel* song, QObject* parent)
    : QObject(parent), pattern_(pattern), song_(song) {
    if (pattern_ != nullptr) connect(pattern_, &PatternModel::patternChanged, this, &ScoreModel::rebuild);
    if (song_ != nullptr) {
        // The selection, an undo, a load: anything but a fader move.
        connect(song_, &SongModel::arrangementChanged, this, &ScoreModel::rebuild);
        connect(song_, &SongModel::notationChanged, this, &ScoreModel::rebuild);
    }
    rebuild();
}

QStringList ScoreModel::clefNames() const {
    QStringList names;
    for (int i = 0; i <= static_cast<int>(notation::ClefKind::Percussion); ++i)
        names << QString::fromStdString(std::string(notation::Clef(static_cast<notation::ClefKind>(i)).name()));
    return names;
}

QString ScoreModel::clefName() const {
    return QString::fromStdString(std::string(notation::Clef(static_cast<notation::ClefKind>(clef_kind_)).name()));
}

QString ScoreModel::keyLabel() const {
    return QString::fromStdString(notation::KeySignature(key_fifths_).label());
}

void ScoreModel::write(const QVariantMap& settings) {
    if (song_ == nullptr) return;
    (void)song_->setNotationSettings(song_->selectedTrack(), song_->currentSection(), settings);
}

void ScoreModel::setClefKind(int kind) {
    if (kind < 0 || kind > static_cast<int>(notation::ClefKind::Percussion) || kind == clef_kind_) return;
    write({{"clef", clefNames().at(kind)}});
}

void ScoreModel::setKeyFifths(int fifths) {
    if (fifths < -7 || fifths > 7 || fifths == key_fifths_) return;
    write({{"keyFifths", fifths}});
}

void ScoreModel::setUnmetered(bool unmetered) {
    if (unmetered == unmetered_) return;
    write({{"unmetered", unmetered}});
}

void ScoreModel::refresh() { rebuild(); }

int ScoreModel::clefRefPos() const {
    const notation::Clef clef(static_cast<notation::ClefKind>(clef_kind_));
    if (!clef.pitched()) return 4;
    // The pitch the clef names: G4 for G clefs, F3 for F clefs, C4 for C clefs.
    notation::SpelledPitch ref{notation::Step::C, notation::Accidental::Natural, 4};
    switch (clef.kind()) {
        case notation::ClefKind::FrenchViolin:
        case notation::ClefKind::Treble: ref = {notation::Step::G, notation::Accidental::Natural, 4}; break;
        case notation::ClefKind::BaritoneF:
        case notation::ClefKind::Bass:
        case notation::ClefKind::Subbass: ref = {notation::Step::F, notation::Accidental::Natural, 3}; break;
        default: break; // C clefs keep C4
    }
    return notation::staff_position(ref, clef);
}

void ScoreModel::rebuild() {
    if (pattern_ == nullptr) return;
    QVariantMap settings;
    if (song_ != nullptr) settings = song_->notationSettings(song_->selectedTrack(), song_->currentSection());
    clef_kind_ = std::max(0, static_cast<int>(clefNames().indexOf(settings.value("clef", "Treble").toString())));
    key_fifths_ = std::clamp(settings.value("keyFifths", 0).toInt(), -7, 7);
    time_beats_ = std::max(1, settings.value("timeBeats", 4).toInt());
    time_beat_unit_ = std::max(1, settings.value("timeUnit", 4).toInt());
    unmetered_ = settings.value("unmetered", false).toBool();

    const notation::Clef clef(static_cast<notation::ClefKind>(clef_kind_));
    const notation::KeySignature key(key_fifths_);
    notation::TimeSignature time = notation::TimeSignature::unmetered();
    try {
        if (!unmetered_) time = notation::TimeSignature(time_beats_, time_beat_unit_);
    } catch (const std::invalid_argument&) {
        time = notation::TimeSignature(4, 4);
    }

    // The key signature: each accidental on the line or space it is written
    // at for this clef.
    key_sig_.clear();
    if (clef.pitched()) {
        const notation::Clef treble(notation::ClefKind::Treble);
        int shift = (treble.bottom_line_diatonic() - clef.bottom_line_diatonic()) % 7;
        if (shift > 3) shift -= 7;
        if (shift < -3) shift += 7;
        const bool sharps = key_fifths_ > 0;
        for (int i = 0; i < std::abs(key_fifths_); ++i) {
            int pos = (sharps ? kSharpTreble[i] : kFlatTreble[i]) + shift;
            if (pos > 9) pos -= 7;
            if (pos < -1) pos += 7;
            key_sig_.push_back(QVariantMap{{"pos", pos}, {"acc", sharps ? 1 : -1}});
        }
    }

    // The step grid writes notes shorter than their step (a gate under 100%):
    // drawn exactly they would read as dotted 32nds and rests. A note that
    // starts on a step is drawn to the end of the steps it sounds in.
    blokkily::Pattern shown = pattern_->pattern();
    const Tick step = std::max<Tick>(1, shown.ticks_per_beat() / 4);
    for (const auto& trigger : pattern_->pattern().events()) {
        if (trigger.start % step != 0 || trigger.duration % step == 0) continue;
        auto drawn = trigger;
        drawn.duration = std::min((trigger.duration + step - 1) / step * step, shown.length() - trigger.start);
        (void)shown.update(trigger.id, drawn);
    }
    layout(notation::to_staff(shown, key, clef, time));
    emit scoreChanged();
}

void ScoreModel::layout(const notation::Staff& staff) {
    const Tick tpb = pattern_->pattern().ticks_per_beat();
    objects_.clear();
    barlines_.clear();
    measure_count_ = static_cast<int>(staff.measures.size());

    // Every object where it falls, and which accidentals print.
    std::vector<Entry> entries;
    std::vector<bool> show_acc;
    for (const auto& placed : notation::place(staff, tpb)) {
        entries.push_back({placed.measure, placed.onset, placed.length,
                           &staff.measures[placed.measure].objects[placed.index], placed.voice});
        show_acc.push_back(placed.accidental_shown);
    }
    std::vector<Tick> measure_end;
    notation::TimeSignature time = staff.time;
    Tick measure_start = 0;
    for (const auto& measure : staff.measures) {
        if (measure.time) time = *measure.time;
        measure_start += notation::measure_ticks(measure, time, tpb);
        measure_end.push_back(measure_start);
    }

    // Columns: one per onset; each takes room for the time to the next.
    std::map<std::pair<std::size_t, Tick>, double> column_x;
    double x = 0.0;
    const bool barred = staff.time.metered();
    for (std::size_t i = 0; i < entries.size();) {
        const std::size_t m = entries[i].measure;
        x += i == 0 ? 0.6 : 0.0;
        std::size_t j = i;
        while (j < entries.size() && entries[j].measure == m) {
            std::size_t k = j;
            bool accidental = false;
            while (k < entries.size() && entries[k].measure == m && entries[k].onset == entries[j].onset) {
                accidental = accidental || show_acc[k];
                ++k;
            }
            const Tick next = k < entries.size() && entries[k].measure == m ? entries[k].onset : measure_end[m];
            if (accidental) x += kAccidentalRoom;
            column_x[{m, entries[j].onset}] = x;
            x += room_for(next - entries[j].onset, tpb);
            j = k;
        }
        // Chant has no barlines, not even a closing one.
        if (barred) barlines_.push_back(QVariantMap{{"x", x}, {"final", j == entries.size()}});
        x += kBarPadding;
        i = j;
    }
    content_width_ = x;

    const auto pos_of = [&](const notation::NotationNote& note) {
        return staff.clef.pitched() ? notation::staff_position(note.spelled, staff.clef) : 4;
    };
    // Where two or more voices sound in a bar, the one that sits highest
    // there has its stems up and the others down.
    std::map<std::size_t, std::map<int, std::pair<double, int>>> voice_height; // bar -> voice -> (sum, count)
    for (const auto& entry : entries)
        if (const auto* note = std::get_if<notation::NotationNote>(entry.object)) {
            auto& height = voice_height[entry.measure][entry.voice];
            height.first += pos_of(*note);
            ++height.second;
        }
    std::map<std::size_t, int> top_voice;
    for (const auto& [bar, voices] : voice_height) {
        double best = -1e9;
        for (const auto& [voice, height] : voices)
            if (height.first / height.second > best) {
                best = height.first / height.second;
                top_voice[bar] = voice;
            }
    }

    // Chords: the notes that share an onset in one voice share one stem.
    std::map<std::tuple<std::size_t, Tick, int>, std::pair<int, int>> chord_span; // lowest, highest pos
    for (const auto& entry : entries)
        if (const auto* note = std::get_if<notation::NotationNote>(entry.object)) {
            const int pos = pos_of(*note);
            auto [it, fresh] = chord_span.try_emplace({entry.measure, entry.onset, entry.voice}, pos, pos);
            if (!fresh) it->second = {std::min(it->second.first, pos), std::max(it->second.second, pos)};
        }

    highest_pos_ = 8;
    lowest_pos_ = 0;
    bool lyrics = false;
    std::map<std::tuple<std::size_t, Tick, int>, bool> stem_drawn;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        const double at = column_x[{entry.measure, entry.onset}];
        const bool shared = voice_height[entry.measure].size() > 1;
        if (const auto* rest = std::get_if<notation::Rest>(entry.object)) {
            const int value = static_cast<int>(rest->duration.value);
            int pos = value <= 0 ? 6 : 4;
            if (shared) pos += entry.voice == top_voice[entry.measure] ? 2 : -4;
            objects_.push_back(QVariantMap{{"kind", "rest"}, {"x", at}, {"pos", pos}, {"value", value},
                                           {"dots", rest->duration.dots}, {"voice", entry.voice},
                                           {"tuplet", rest->duration.tuplet() ? rest->duration.tuplet_actual : 0}});
            continue;
        }
        const auto& note = std::get<notation::NotationNote>(*entry.object);
        const int pos = pos_of(note);
        const auto chord = std::make_tuple(entry.measure, entry.onset, entry.voice);
        const auto [low, high] = chord_span[chord];
        const bool up = shared ? entry.voice == top_voice[entry.measure] : (low + high) / 2.0 < 4.0;
        const int value = static_cast<int>(note.duration.value);
        const bool stemmed = value >= 1 && !note.duration.stemless;
        const bool draws_stem = stemmed && !stem_drawn[chord];
        stem_drawn[chord] = stem_drawn[chord] || stemmed;
        const int stem_from = up ? low : high;
        const int stem_to = up ? std::max(high + 7, 4) : std::min(low - 7, 4);
        highest_pos_ = std::max(highest_pos_, draws_stem && up ? stem_to : pos + 1);
        lowest_pos_ = std::min(lowest_pos_, draws_stem && !up ? stem_to : pos - 1);

        // The tie lands on the next note of this pitch in this voice.
        double tie_to = -1.0;
        if (note.tie_start)
            for (std::size_t k = i + 1; k < entries.size(); ++k) {
                const auto* next = std::get_if<notation::NotationNote>(entries[k].object);
                if (next != nullptr && entries[k].voice == entry.voice && next->tie_end &&
                    entries[k].onset == entry.onset + entry.length && next->spelled.step == note.spelled.step &&
                    next->spelled.octave == note.spelled.octave &&
                    next->spelled.accidental == note.spelled.accidental) {
                    tie_to = column_x[{entries[k].measure, entries[k].onset}];
                    break;
                }
            }

        // A tuplet's number goes on its first note, over the group it opens.
        int tuplet = 0;
        double tuplet_end = at;
        if (note.duration.tuplet() && !note.chord) {
            Tick before = 0;
            for (std::size_t k = i; k-- > 0;) {
                if (entries[k].voice != entry.voice) continue;
                const auto& previous = std::visit(
                    [](const auto& o) -> const notation::NoteDuration& { return o.duration; }, *entries[k].object);
                const auto* previous_note = std::get_if<notation::NotationNote>(entries[k].object);
                if (previous_note != nullptr && previous_note->chord) continue;
                if (!previous.tuplet() || entries[k].measure != entry.measure) break;
                before += entries[k].length;
            }
            notation::NoteDuration plain = note.duration;
            plain.tuplet_actual = plain.tuplet_normal = 1;
            const Tick group = notation::ticks(plain, tpb) * note.duration.tuplet_normal;
            if (group > 0 && before % group == 0) {
                tuplet = note.duration.tuplet_actual;
                Tick covered = 0;
                for (std::size_t k = i; k < entries.size() && covered < group; ++k) {
                    if (entries[k].voice != entry.voice || entries[k].measure != entry.measure) continue;
                    const auto* member = std::get_if<notation::NotationNote>(entries[k].object);
                    if (member != nullptr && member->chord) continue;
                    tuplet_end = column_x[{entries[k].measure, entries[k].onset}];
                    covered += entries[k].length;
                }
                highest_pos_ = std::max(highest_pos_, high + 10);
            }
        }

        const bool extend = note.lyric == "_" || note.lyric == "-";
        lyrics = lyrics || !note.lyric.empty();
        objects_.push_back(QVariantMap{
            {"kind", "note"},
            {"x", at},
            {"pos", pos},
            {"acc", static_cast<int>(note.spelled.accidental)},
            {"showAcc", static_cast<bool>(show_acc[i])},
            {"value", value},
            {"dots", note.duration.dots},
            {"voice", entry.voice},
            {"stemUp", up},
            {"stem", draws_stem},
            {"stemFrom", stem_from},
            {"stemTo", stem_to},
            {"stemless", note.duration.stemless},
            {"ledger", notation::ledger_lines(pos)},
            {"tieStart", tie_to >= 0.0},
            {"tieToX", tie_to},
            {"lyric", extend ? QString() : QString::fromStdString(note.lyric)},
            {"extend", extend},
            {"tuplet", tuplet},
            {"tupletEndX", tuplet_end},
        });
    }
    if (lyrics) lowest_pos_ = std::min(lowest_pos_, -6);
}
