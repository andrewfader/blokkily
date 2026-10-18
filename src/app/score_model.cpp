// The score view's projection of the canonical pattern: the open part as
// staff notation (notation::to_staff) under the song's clef, key and meter
// for it, laid out for the QML to draw. Read-only towards the pattern.

#include "score_model.hpp"

#include "pattern_model.hpp"
#include "song_model.hpp"

#include <QTimer>

#include "blokkily/notation/layout.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <utility>
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

ScoreModel::ScoreModel(PatternModel* pattern, SongModel* song, QObject* parent)
    : QObject(parent), pattern_(pattern), song_(song) {
    if (pattern_ != nullptr) connect(pattern_, &PatternModel::patternChanged, this, &ScoreModel::queueRebuild);
    if (song_ != nullptr) {
        // The selection, an undo, a load: anything but a fader move. One
        // edit often says so on several signals; it is laid out once.
        connect(song_, &SongModel::arrangementChanged, this, &ScoreModel::queueRebuild);
        connect(song_, &SongModel::notationChanged, this, &ScoreModel::queueRebuild);
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

void ScoreModel::queueRebuild() {
    stale_ = true;
    if (!active_ || queued_) return;
    queued_ = true;
    QTimer::singleShot(0, this, [this] {
        if (queued_ && stale_) rebuild();
        queued_ = false;
    });
}

void ScoreModel::setActive(bool active) {
    if (active == active_) return;
    active_ = active;
    if (active_ && stale_) rebuild();
    emit activeChanged();
}

void ScoreModel::setNoteValue(int value) {
    if (value != 1 && value != 2 && value != 4 && value != 8 && value != 16) return;
    if (value == note_value_) return;
    note_value_ = value;
    emit entryChanged();
}

void ScoreModel::setDotted(bool dotted) {
    if (dotted == dotted_) return;
    dotted_ = dotted;
    emit entryChanged();
}

int ScoreModel::tickAt(double x) const {
    if (time_map_.size() < 2 || x < time_map_.front().first - 0.6 || x > time_map_.back().first) return -1;
    // The column at or before x, and how far x has gone toward the next one.
    std::size_t i = 0;
    while (i + 1 < time_map_.size() && time_map_[i + 1].first <= x) ++i;
    double tick = static_cast<double>(time_map_[i].second);
    if (i + 1 < time_map_.size()) {
        const auto& [x0, t0] = time_map_[i];
        const auto& [x1, t1] = time_map_[i + 1];
        if (x1 > x0 && t1 > t0) tick += (std::max(x, x0) - x0) / (x1 - x0) * static_cast<double>(t1 - t0);
    }
    const Tick step = PatternModel::ticks_per_step;
    // To the nearest step: a column's own x is its time, wherever the
    // spacing around it falls.
    const auto snapped = static_cast<Tick>(std::lround(tick / static_cast<double>(step))) * step;
    return snapped >= std::as_const(*pattern_).pattern().length() ? -1 : static_cast<int>(snapped);
}

double ScoreModel::xForTick(int tick) const {
    if (time_map_.empty()) return 0.0;
    for (std::size_t i = 0; i + 1 < time_map_.size(); ++i) {
        const auto& [x0, t0] = time_map_[i];
        const auto& [x1, t1] = time_map_[i + 1];
        if (tick >= t0 && tick < t1) return x0 + (x1 - x0) * static_cast<double>(tick - t0) / static_cast<double>(t1 - t0);
    }
    return tick < time_map_.front().second ? time_map_.front().first : time_map_.back().first;
}

int ScoreModel::keyAt(int pos) const {
    const notation::Clef clef(static_cast<notation::ClefKind>(clef_kind_));
    // An unpitched staff writes on the treble's lines.
    const int bottom = clef.pitched() ? clef.bottom_line_diatonic()
                                      : notation::Clef(notation::ClefKind::Treble).bottom_line_diatonic();
    const int diatonic = bottom + pos;
    if (diatonic < 0) return -1;
    notation::SpelledPitch written;
    written.step = static_cast<notation::Step>(diatonic % 7);
    written.octave = diatonic / 7;
    written.accidental = notation::KeySignature(key_fifths_).accidental_for(written.step);
    const int key = notation::to_midi(written) + transposition_;
    return key < 0 || key > 127 ? -1 : key;
}

QVariantMap ScoreModel::selectedObject() const {
    for (const auto& item : objects_) {
        const auto object = item.toMap();
        if (object.value("kind") == "note" && object.value("step").toInt() == selected_step_ &&
            object.value("key").toInt() == selected_key_)
            return object;
    }
    return {};
}

bool ScoreModel::select(int step, int key) {
    selected_step_ = step;
    selected_key_ = key;
    rebuild();
    return !selectedObject().isEmpty();
}

void ScoreModel::deselect() {
    if (selected_step_ < 0) return;
    selected_step_ = selected_key_ = -1;
    rebuild();
}

bool ScoreModel::click(double x, double pos) {
    if (pattern_ == nullptr) return false;
    // A notehead is an oval about a staff space wide and a space tall.
    QVariantMap best;
    double nearest = 1e9;
    for (const auto& item : objects_) {
        const auto object = item.toMap();
        if (object.value("kind") != "note" || object.value("step").toInt() < 0) continue;
        const double dx = std::abs(object.value("x").toDouble() - x);
        const double dy = std::abs(object.value("pos").toDouble() - pos);
        if (dx > 0.8 || dy > 0.75) continue;
        if (dx + dy < nearest) {
            nearest = dx + dy;
            best = object;
        }
    }
    if (!best.isEmpty()) return select(best.value("step").toInt(), best.value("key").toInt());

    const int tick = tickAt(x);
    const int key = keyAt(static_cast<int>(std::lround(pos)));
    if (tick < 0 || key < 0) return false;
    const int step = tick / static_cast<int>(PatternModel::ticks_per_step);
    if (pattern_->rollNoteDuration(step, key) > 0) return select(step, key);
    const Tick whole = 4 * std::as_const(*pattern_).pattern().ticks_per_beat();
    Tick ticks = whole / note_value_;
    if (dotted_) ticks += ticks / 2;
    ticks = std::min<Tick>(ticks, std::as_const(*pattern_).pattern().length() - tick);
    if (song_ != nullptr) song_->beginGesture();
    pattern_->addRollNote(step, key);
    pattern_->resizeRollNote(step, key, static_cast<int>(ticks));
    if (song_ != nullptr) song_->endGesture();
    return select(step, key);
}

bool ScoreModel::deleteSelected() {
    if (pattern_ == nullptr || selectedObject().isEmpty()) return false;
    pattern_->eraseRollNote(selected_step_, selected_key_);
    deselect();
    return true;
}

bool ScoreModel::moveSelected(int steps) {
    const auto object = selectedObject();
    if (object.isEmpty() || steps == 0) return false;
    // An octave keeps the note's spelling; a line or space takes the key's.
    const int key = steps % 7 == 0 ? selected_key_ + 12 * (steps / 7) : keyAt(object.value("pos").toInt() + steps);
    return alterSelected(key - selected_key_);
}

bool ScoreModel::alterSelected(int semitones) {
    if (pattern_ == nullptr || selectedObject().isEmpty() || semitones == 0) return false;
    // The edit rebuilds the score, which lets go of a note it cannot find.
    const int step = selected_step_;
    const int key = selected_key_ + semitones;
    if (key < 0 || key > 127) return false;
    if (!pattern_->moveRollNote(step, selected_key_, step, key)) return false;
    return select(step, key);
}

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
    stale_ = false;
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
    // starts on a step is drawn to the end of the steps it sounds in, unless
    // it is exactly a tuplet's value (a triplet eighth from a take or a
    // score), which is drawn as the tuplet it is.
    blokkily::Pattern shown = std::as_const(*pattern_).pattern();
    const Tick step = std::max<Tick>(1, shown.ticks_per_beat() / 4);
    for (const auto& trigger : std::as_const(*pattern_).pattern().events()) {
        if (trigger.start % step != 0 || trigger.duration % step == 0) continue;
        if (trigger.duration > 0) {
            const auto written = notation::notate_ticks(trigger.duration, shown.ticks_per_beat());
            if (written.size() == 1 && written.front().tuplet()) continue;
        }
        auto drawn = trigger;
        drawn.duration = std::min((trigger.duration + step - 1) / step * step, shown.length() - trigger.start);
        (void)shown.update(trigger.id, drawn);
    }
    layout(notation::to_staff(shown, key, clef, time));
    // A selection the pattern no longer holds (an undo, another editor) goes.
    if (selected_step_ >= 0 && selectedObject().isEmpty()) selected_step_ = selected_key_ = -1;
    emit scoreChanged();
}

void ScoreModel::layout(const notation::Staff& staff) {
    const Tick tpb = std::as_const(*pattern_).pattern().ticks_per_beat();
    objects_.clear();
    barlines_.clear();
    beams_.clear();
    time_map_.clear();
    transposition_ = staff.transposition();
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
            time_map_.emplace_back(x, entries[j].onset);
            x += room_for(next - entries[j].onset, tpb);
            j = k;
        }
        // Chant has no barlines, not even a closing one.
        if (barred) barlines_.push_back(QVariantMap{{"x", x}, {"final", j == entries.size()}});
        time_map_.emplace_back(x, measure_end[m]);
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

    // Beams: eighths and shorter that follow each other in one voice, inside
    // one beat (a quarter; a dotted quarter in 6/8, 9/8, 12/8), with no rest
    // between, share a flat beam and one stem direction. Chant has no stems
    // and so no beams.
    using ChordKey = std::tuple<std::size_t, Tick, int>;
    struct BeamGroup {
        std::vector<ChordKey> chords;
        std::vector<int> values;
        bool up = true;
        int pos = 0;
    };
    std::vector<BeamGroup> beam_groups;
    std::map<ChordKey, std::size_t> beamed;
    {
        std::vector<Tick> beat(staff.measures.size(), tpb);
        notation::TimeSignature meter = staff.time;
        for (std::size_t m = 0; m < staff.measures.size(); ++m) {
            if (staff.measures[m].time) meter = *staff.measures[m].time;
            const bool compound = meter.metered() && meter.beat_unit() == 8 && meter.beats() % 3 == 0 &&
                                  meter.beats() > 3;
            beat[m] = compound ? tpb * 3 / 2 : tpb;
        }
        struct Open {
            BeamGroup group;
            std::size_t measure = 0;
            Tick beat_index = 0;
            Tick next = 0;   // where the next chord must start to join
        };
        std::map<int, Open> open;
        const auto close = [&](int voice) {
            const auto found = open.find(voice);
            if (found == open.end()) return;
            if (found->second.group.chords.size() >= 2) beam_groups.push_back(std::move(found->second.group));
            open.erase(found);
        };
        std::map<int, ChordKey> last_chord;
        for (const auto& entry : entries) {
            const auto* note = std::get_if<notation::NotationNote>(entry.object);
            if (note == nullptr) {
                close(entry.voice);
                continue;
            }
            const ChordKey key{entry.measure, entry.onset, entry.voice};
            if (const auto seen = last_chord.find(entry.voice); seen != last_chord.end() && seen->second == key)
                continue;   // another note of the same chord
            last_chord[entry.voice] = key;
            const int value = static_cast<int>(note->duration.value);
            if (value < 3 || note->duration.stemless) {
                close(entry.voice);
                continue;
            }
            const Tick start = entry.measure == 0 ? 0 : measure_end[entry.measure - 1];
            const Tick beat_index = (entry.onset - start) / std::max<Tick>(1, beat[entry.measure]);
            auto current = open.find(entry.voice);
            if (current != open.end() &&
                (current->second.measure != entry.measure || current->second.beat_index != beat_index ||
                 current->second.next != entry.onset)) {
                close(entry.voice);
                current = open.end();
            }
            if (current == open.end())
                current = open.emplace(entry.voice, Open{{}, entry.measure, beat_index, entry.onset}).first;
            current->second.group.chords.push_back(key);
            current->second.group.values.push_back(value);
            current->second.next = entry.onset + entry.length;
        }
        while (!open.empty()) close(open.begin()->first);
        for (std::size_t g = 0; g < beam_groups.size(); ++g) {
            auto& group = beam_groups[g];
            const auto measure = std::get<0>(group.chords.front());
            const int voice = std::get<2>(group.chords.front());
            double middle = 0.0;
            int lowest = 1000, highest = -1000;
            for (const auto& key : group.chords) {
                const auto [low, high] = chord_span[key];
                middle += (low + high) / 2.0;
                lowest = std::min(lowest, low);
                highest = std::max(highest, high);
                beamed[key] = g;
            }
            const bool shared = voice_height[measure].size() > 1;
            group.up = shared ? voice == top_voice[measure]
                              : middle / static_cast<double>(group.chords.size()) < 4.0;
            group.pos = group.up ? std::max(highest + 7, 4) : std::min(lowest - 7, 4);
        }
    }

    highest_pos_ = 8;
    lowest_pos_ = 0;
    bool lyrics = false;
    std::map<std::tuple<std::size_t, Tick, int>, bool> stem_drawn;
    std::map<std::pair<int, int>, Tick> tied_from;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        const double at = column_x[{entry.measure, entry.onset}];
        const bool shared = voice_height[entry.measure].size() > 1;
        if (const auto* rest = std::get_if<notation::Rest>(entry.object)) {
            const int value = static_cast<int>(rest->duration.value);
            int pos = value <= 0 ? 6 : 4;
            if (shared) pos += entry.voice == top_voice[entry.measure] ? 4 : -4;
            objects_.push_back(QVariantMap{{"kind", "rest"}, {"x", at}, {"pos", pos}, {"value", value},
                                           {"dots", rest->duration.dots}, {"voice", entry.voice},
                                           {"tuplet", rest->duration.tuplet() ? rest->duration.tuplet_actual : 0}});
            continue;
        }
        const auto& note = std::get<notation::NotationNote>(*entry.object);
        const int pos = pos_of(note);
        const auto chord = std::make_tuple(entry.measure, entry.onset, entry.voice);
        // The pattern note this piece of notation draws: a tied piece points
        // back to where its tie began, so a click on any piece finds it.
        const int midi = notation::to_midi(note.spelled) + staff.transposition();
        const auto sounding = std::make_pair(entry.voice, midi);
        Tick source = entry.onset;
        if (note.tie_end) {
            if (const auto open = tied_from.find(sounding); open != tied_from.end()) source = open->second;
        }
        if (note.tie_start) tied_from[sounding] = source;
        else tied_from.erase(sounding);
        const Tick step_ticks = PatternModel::ticks_per_step;
        const int source_step = source % step_ticks == 0 ? static_cast<int>(source / step_ticks) : -1;
        const auto [low, high] = chord_span[chord];
        const auto in_beam = beamed.find(chord);
        const BeamGroup* beam = in_beam != beamed.end() ? &beam_groups[in_beam->second] : nullptr;
        const bool up = beam != nullptr ? beam->up
                        : shared ? entry.voice == top_voice[entry.measure] : (low + high) / 2.0 < 4.0;
        const int value = static_cast<int>(note.duration.value);
        const bool stemmed = value >= 1 && !note.duration.stemless;
        const bool draws_stem = stemmed && !stem_drawn[chord];
        stem_drawn[chord] = stem_drawn[chord] || stemmed;
        const int stem_from = up ? low : high;
        const int stem_to = beam != nullptr ? beam->pos : up ? std::max(high + 7, 4) : std::min(low - 7, 4);
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
        int tuplet_pos = 0;
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
                // The bracket clears every member, chord tones too, as if
                // each had a stem up from it, and never sits in the staff.
                int top = 8;
                for (std::size_t k = i; k < entries.size() && covered < group; ++k) {
                    if (entries[k].voice != entry.voice || entries[k].measure != entry.measure) continue;
                    const auto* member = std::get_if<notation::NotationNote>(entries[k].object);
                    top = std::max(top, member != nullptr ? pos_of(*member) + 7 : 6);
                    if (member != nullptr && member->chord) continue;
                    tuplet_end = column_x[{entries[k].measure, entries[k].onset}];
                    covered += entries[k].length;
                }
                tuplet_pos = top + 2;
                highest_pos_ = std::max({highest_pos_, high + 10, tuplet_pos + 2});
            }
        }

        const bool extend = note.lyric == "_" || note.lyric == "-";
        // A word that goes on: the hyphen is drawn between this syllable and
        // the next sung one in its voice, not as part of the text.
        const bool hyphen = !extend && note.lyric.size() > 1 && note.lyric.back() == '-';
        double hyphen_to = -1.0;
        if (hyphen)
            for (std::size_t k = i + 1; k < entries.size(); ++k) {
                const auto* next = std::get_if<notation::NotationNote>(entries[k].object);
                if (next != nullptr && entries[k].voice == entry.voice && !next->chord && !next->lyric.empty()) {
                    hyphen_to = column_x[{entries[k].measure, entries[k].onset}];
                    break;
                }
            }
        lyrics = lyrics || !note.lyric.empty();
        objects_.push_back(QVariantMap{
            {"kind", "note"},
            {"step", source_step},
            {"key", midi},
            {"selected", source_step >= 0 && source_step == selected_step_ && midi == selected_key_},
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
            {"lyric", extend ? QString()
                             : QString::fromStdString(hyphen ? note.lyric.substr(0, note.lyric.size() - 1) : note.lyric)},
            {"extend", extend},
            {"hyphen", hyphen},
            {"hyphenToX", hyphen_to},
            {"tuplet", tuplet},
            {"tupletEndX", tuplet_end},
            {"tupletPos", tuplet_pos},
            {"beamed", beam != nullptr},
        });
    }
    if (lyrics) lowest_pos_ = std::min(lowest_pos_, -6);

    // The beams themselves: the main one across the group, and for each
    // shorter value a beam across every run of notes that short, or a hook
    // from a note that stands alone at it (toward the note before, or after
    // when it opens the group).
    for (const auto& group : beam_groups) {
        const auto x_of = [&](std::size_t index) {
            return column_x[{std::get<0>(group.chords[index]), std::get<1>(group.chords[index])}];
        };
        const auto beam = [&](double from, double to, int level, int hook) {
            beams_.push_back(QVariantMap{{"fromX", from}, {"toX", to}, {"pos", group.pos}, {"up", group.up},
                                         {"level", level}, {"hook", hook}});
        };
        beam(x_of(0), x_of(group.chords.size() - 1), 0, 0);
        for (int level = 1; level <= 3; ++level) {
            const int shortest = 3 + level;
            for (std::size_t i = 0; i < group.chords.size();) {
                if (group.values[i] < shortest) {
                    ++i;
                    continue;
                }
                std::size_t j = i;
                while (j + 1 < group.chords.size() && group.values[j + 1] >= shortest) ++j;
                if (j > i) beam(x_of(i), x_of(j), level, 0);
                else beam(x_of(i), x_of(i), level, i == 0 ? 1 : -1);
                i = j + 1;
            }
        }
        if (group.up) highest_pos_ = std::max(highest_pos_, group.pos + 1);
        else lowest_pos_ = std::min(lowest_pos_, group.pos - 1);
    }
}
