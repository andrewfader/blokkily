#include "keyboard_model.hpp"

#include <QVariantMap>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {

// The four surfaces, named the way the interface labels them. The order is the
// order of the switcher, so a chip index and a kind are the same thing.
constexpr blokkily::KeyboardKind kinds[] = {
    blokkily::KeyboardKind::piano, blokkily::KeyboardKind::isomorphic,
    blokkily::KeyboardKind::fretboard, blokkily::KeyboardKind::theoryboard};
constexpr const char* surface_names[] = {"PIANO", "GRID", "FRETS", "CHORDS"};

// Which way the surface runs. The order is the order of the switcher.
constexpr blokkily::KeyboardOrientation orientations_[] = {
    blokkily::KeyboardOrientation::horizontal, blokkily::KeyboardOrientation::vertical};
constexpr const char* orientation_names[] = {"ACROSS", "DOWN"};

// How far a degree sits from the twelve-tone key an instrument is told, said
// the way a tuner says it. Exact keys show nothing rather than "+0".
QString retune_text(double cents) {
    if (std::abs(cents) < 0.5) return {};
    return QString("%1%2").arg(cents > 0 ? "+" : "").arg(qRound(cents));
}

} // namespace

KeyboardModel::KeyboardModel(SongModel* song, PatternModel* pattern,
                             AppController* controller, QObject* parent)
    : QObject(parent), song_(song), pattern_(pattern), controller_(controller) {
    spec_.range = blokkily::keyboard_registers().back();   // the full span
    spec_.isomorphic = blokkily::isomorphic_layouts().front();
    spec_.strings = blokkily::string_tunings().front();
    // An isomorphic grid needs depth as well as width: a chord shape on one
    // reaches across rows, so four of them is a strip rather than a keyboard.
    spec_.rows = 6;
    spec_.columns = 16;
    spec_.frets = 12;
    if (song_ != nullptr) {
        // The surface is a projection of the song's tuning and scale, so it
        // follows them rather than remembering its own.
        QObject::connect(song_, &SongModel::tuningChanged, this, &KeyboardModel::rebuild);
    }
    rebuild();
}

void KeyboardModel::rebuild() {
    if (song_ != nullptr) {
        spec_.tuning = song_->song().tuning;
        spec_.scale = song_->song().scale;
        spec_.root_degree = song_->song().root_degree;
    }
    cells_ = blokkily::keyboard_cells(spec_);
    emit specChanged();
}

QString KeyboardModel::surface() const {
    for (std::size_t index = 0; index < std::size(kinds); ++index)
        if (kinds[index] == spec_.kind) return QString::fromLatin1(surface_names[index]);
    return QStringLiteral("PIANO");
}

QStringList KeyboardModel::surfaces() const {
    QStringList names;
    for (const auto* name : surface_names) names << QString::fromLatin1(name);
    return names;
}

QStringList KeyboardModel::layoutNames() const {
    QStringList names;
    for (const auto& layout : blokkily::isomorphic_layouts())
        names << QString::fromStdString(layout.name);
    return names;
}

QStringList KeyboardModel::registerNames() const {
    QStringList names;
    for (const auto& span : blokkily::keyboard_registers())
        names << QString::fromStdString(span.name);
    return names;
}

QStringList KeyboardModel::stringTuningNames() const {
    QStringList names;
    for (const auto& tuning : blokkily::string_tunings())
        names << QString::fromStdString(tuning.name);
    return names;
}

int KeyboardModel::rows() const {
    int highest = 0;
    for (const auto& cell : cells_) highest = std::max(highest, cell.row);
    return cells_.empty() ? 0 : highest + 1;
}

int KeyboardModel::columns() const {
    int widest = 0;
    for (const auto& cell : cells_) widest = std::max(widest, cell.column);
    return cells_.empty() ? 0 : widest + 1;
}

double KeyboardModel::spanX() const {
    return blokkily::keyboard_extent(cells_).columns;
}

double KeyboardModel::spanY() const {
    return blokkily::keyboard_extent(cells_).rows;
}

QString KeyboardModel::cellShape() const {
    return blokkily::keyboard_shape(spec_) == blokkily::CellShape::hexagon
               ? QStringLiteral("HEX")
               : QStringLiteral("RECT");
}

QString KeyboardModel::orientation() const {
    for (std::size_t index = 0; index < std::size(orientations_); ++index)
        if (orientations_[index] == spec_.orientation)
            return QString::fromLatin1(orientation_names[index]);
    return QStringLiteral("ACROSS");
}

QStringList KeyboardModel::orientations() const {
    QStringList names;
    for (const auto* name : orientation_names) names << QString::fromLatin1(name);
    return names;
}

QVariantList KeyboardModel::cells() const {
    QVariantList rows;
    rows.reserve(static_cast<qsizetype>(cells_.size()));
    for (std::size_t index = 0; index < cells_.size(); ++index) {
        const auto& cell = cells_[index];
        QVariantMap entry;
        entry["index"] = static_cast<int>(index);
        entry["degree"] = cell.degree;
        entry["key"] = static_cast<int>(cell.pitch.key);
        entry["cents"] = cell.pitch.cents;
        entry["retune"] = retune_text(cell.pitch.cents);
        entry["row"] = cell.row;
        entry["column"] = cell.column;
        // Where to draw it, in cell widths and heights. The row and the column
        // say which key this is; these say where it lands once the layout has
        // been staggered, tiled, or turned.
        entry["x"] = cell.x;
        entry["y"] = cell.y;
        entry["accidental"] = cell.accidental;
        entry["inScale"] = cell.in_scale;
        entry["root"] = cell.root;
        entry["label"] = QString::fromStdString(cell.label);
        entry["voices"] = static_cast<int>(cell.chord.size());
        rows.push_back(entry);
    }
    return rows;
}

void KeyboardModel::setSurface(const QString& name) {
    for (std::size_t index = 0; index < std::size(kinds); ++index) {
        if (name != QString::fromLatin1(surface_names[index])) continue;
        if (spec_.kind == kinds[index]) return;
        spec_.kind = kinds[index];
        rebuild();
        return;
    }
}

void KeyboardModel::setLayout(const QString& name) {
    for (const auto& layout : blokkily::isomorphic_layouts())
        if (QString::fromStdString(layout.name) == name) {
            spec_.isomorphic = layout;
            rebuild();
            return;
        }
}

void KeyboardModel::setRegister(const QString& name) {
    for (const auto& span : blokkily::keyboard_registers())
        if (QString::fromStdString(span.name) == name) {
            spec_.range = span;
            rebuild();
            return;
        }
}

void KeyboardModel::setStringTuning(const QString& name) {
    for (const auto& tuning : blokkily::string_tunings())
        if (QString::fromStdString(tuning.name) == name) {
            spec_.strings = tuning;
            rebuild();
            return;
        }
}

void KeyboardModel::setOrientation(const QString& name) {
    for (std::size_t index = 0; index < std::size(orientations_); ++index) {
        if (name != QString::fromLatin1(orientation_names[index])) continue;
        if (spec_.orientation == orientations_[index]) return;
        spec_.orientation = orientations_[index];
        // Turning the surface moves the keys; it does not change which key is
        // which, so nothing about the song or the pattern is touched.
        rebuild();
        return;
    }
}

void KeyboardModel::toggleOrientation() {
    setOrientation(spec_.orientation == blokkily::KeyboardOrientation::horizontal
                       ? QStringLiteral("DOWN")
                       : QStringLiteral("ACROSS"));
}

void KeyboardModel::toggleRecording() {
    recording_ = !recording_;
    emit specChanged();
}

const blokkily::KeyboardCell* KeyboardModel::cellForDegree(int degree) const {
    for (const auto& cell : cells_)
        if (cell.degree == degree) return &cell;
    return nullptr;
}

bool KeyboardModel::pressDegree(int degree) {
    const auto* cell = cellForDegree(degree);
    if (cell == nullptr) return false;
    return press(static_cast<int>(cell - cells_.data()));
}

bool KeyboardModel::press(int index) { return sound(index, false); }

bool KeyboardModel::hold(int index) { return sound(index, true); }

void KeyboardModel::release() {
    if (controller_ != nullptr) controller_->releaseAudition();
}

bool KeyboardModel::sound(int index, bool held) {
    if (index < 0 || static_cast<std::size_t>(index) >= cells_.size()) return false;
    const auto& cell = cells_[static_cast<std::size_t>(index)];

    // A chord pad sounds every degree it holds. A single key is asked of the
    // scale first: with auto-scale on, a key between the notes of the scale
    // plays the nearest note of it rather than a note that is not in the song.
    std::vector<int> degrees = cell.chord;
    if (degrees.size() == 1 && song_ != nullptr) degrees = {song_->snapDegree(cell.degree)};
    if (degrees.empty()) return false;

    std::vector<blokkily::TunedPitch> pitches;
    pitches.reserve(degrees.size());
    QStringList names;
    for (const int degree : degrees) {
        pitches.push_back(song_ == nullptr ? blokkily::TunedPitch{}
                                           : song_->pitchForDegree(degree));
        if (song_ != nullptr) names << song_->degreeName(degree);
    }

    // The step is written before the press is sounded. Editing the pattern
    // recompiles the arrangement, and a note sent to the engine that the edit
    // replaces is a note nobody hears.
    if (recording_ && pattern_ != nullptr && pattern_->selectedStep() >= 0) {
        if (pitches.size() == 1) {
            pattern_->placeNote(pattern_->selectedStep(),
                                blokkily::Note{pitches.front().key, 0.9F, 0.0F,
                                               pitches.front().cents});
        } else {
            // A chord is written as one event rather than as several notes on
            // the same step: the step grid shows one trigger there, and the
            // scheduler is what spreads it back into voices.
            blokkily::Chord chord;
            chord.root = pitches.front().key;
            chord.inversion = 0;
            chord.strum = 0;
            // Struck as one, as hard as the pad sounds it.
            chord.velocity = 0.9F;
            chord.intervals.clear();
            chord.cents.clear();
            for (const auto& pitch : pitches) {
                chord.intervals.push_back(static_cast<std::int16_t>(pitch.key - chord.root));
                chord.cents.push_back(pitch.cents);
            }
            pattern_->placeChord(pattern_->selectedStep(), chord);
        }
    }

    if (controller_ != nullptr) (void)controller_->auditionPitches(pitches, 0.9, held);

    last_played_ = names.isEmpty() ? QString::fromStdString(cell.label) : names.join(' ');
    emit played(degrees.front());
    return true;
}
