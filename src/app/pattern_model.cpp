#include "pattern_model.hpp"
#include "blokkily/instruments/soundfont_catalog.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <QFileInfo>
#include <QString>
#include <QVariantMap>
#include <QUrl>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {
QVariantMap plugin_entry(const QString& format, const std::string& name,
                         const std::string& vendor, const std::string& path = {},
                         const std::string& identifier = {}, int index = 0) {
    return {{"format", format},
            {"name", QString::fromStdString(name)},
            {"vendor", QString::fromStdString(vendor)},
            {"path", QString::fromStdString(path)},
            {"identifier", QString::fromStdString(identifier)},
            {"index", index}};
}

QString local_path(const QString& value) {
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : value;
}

QString note_name(int key) {
    static constexpr const char* names[] = {"C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B"};
    return QString("%1%2").arg(names[key % 12]).arg(key / 12 - 1);
}

QString hex2(int value) {
    return QString("%1").arg(qBound(0, value, 255), 2, 16, QChar('0')).toUpper();
}

// Velocity has exactly one numeric meaning in this app: MIDI units. Computing
// it in one place keeps the tracker and the inspector from disagreeing.
int velocity_units(float velocity) {
    return qBound(0, qRound(static_cast<double>(velocity) * 127.0), 127);
}

QString lock_text(const blokkily::Trigger& trigger) {
    if (trigger.locks.empty()) return QStringLiteral("---");
    const auto& lock = trigger.locks.front();
    // Tracker-style effect column: kind, parameter index, then the value.
    return QString("%1%2%3")
        .arg(lock.kind == blokkily::ParameterLock::Kind::modulation ? "M" : "A")
        .arg(lock.parameter_index, 1, 16)
        .arg(hex2(qRound(lock.value * 127.0)));
}
}

PatternModel::PatternModel(SongModel* song, QObject* parent)
    : QAbstractListModel(parent), song_(song) {
    selected_step_ = 8;
    if (song_ != nullptr)
        QObject::connect(song_, &SongModel::songChanged, this, &PatternModel::refresh);
}

// The editors project whichever pattern the song has open, so switching
// patterns in the arrangement moves all three views at once.
blokkily::Pattern& PatternModel::pattern() { return song_->editPattern(); }
const blokkily::Pattern& PatternModel::pattern() const { return song_->editPattern(); }

void PatternModel::refresh() {
    beginResetModel();
    endResetModel();
    emit patternChanged();
}

int PatternModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(pattern().events().size());
}

QVariant PatternModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& event = pattern().events()[static_cast<std::size_t>(index.row())];
    const auto* note = std::get_if<blokkily::Note>(&event.musical_data);
    const int key = note == nullptr ? 60 : note->key;
    switch (role) {
    case IdRole: return QVariant::fromValue<qulonglong>(event.id);
    case StepRole: return static_cast<int>(event.start / ticks_per_step);
    case KeyRole: return key;
    case NameRole: return note_name(key);
    case DurationRole: return static_cast<int>(event.duration);
    case VelocityRole: return hex2(velocity_units(note == nullptr ? 0.8F : note->velocity));
    case LockRole: return lock_text(event);
    default: return {};
    }
}

QHash<int, QByteArray> PatternModel::roleNames() const {
    return {{IdRole, "eventId"}, {StepRole, "step"}, {KeyRole, "key"},
            {NameRole, "noteName"}, {DurationRole, "duration"},
            {VelocityRole, "velocityHex"}, {LockRole, "lockText"}};
}

const blokkily::Trigger* PatternModel::triggerAt(int step) const {
    for (const auto& event : pattern().events())
        if (event.start / ticks_per_step == step) return &event;
    return nullptr;
}

// One row projection feeding both the step grid and the tracker, so the two
// editors can never disagree about what a step contains.
QVariantList PatternModel::steps() const {
    QVariantList rows;
    rows.reserve(step_count);
    for (int step = 0; step < step_count; ++step) {
        const auto* trigger = triggerAt(step);
        QVariantMap row;
        row["active"] = trigger != nullptr;
        row["selected"] = step == selected_step_;
        if (trigger == nullptr) {
            row["noteName"] = "---";
            row["velocityHex"] = "--";
            row["lockText"] = "---";
            row["velocity"] = 0.0;
            row["velocityUnits"] = 0;
            row["hasLock"] = false;
            row["key"] = 0;
            row["ratchets"] = 0;
        } else {
            const auto* note = std::get_if<blokkily::Note>(&trigger->musical_data);
            const float velocity = note == nullptr ? 0.8F : note->velocity;
            row["noteName"] = note_name(note == nullptr ? 60 : note->key);
            row["velocityHex"] = hex2(velocity_units(velocity));
            row["velocityUnits"] = velocity_units(velocity);
            row["lockText"] = lock_text(*trigger);
            row["velocity"] = static_cast<double>(velocity);
            row["hasLock"] = !trigger->locks.empty();
            row["key"] = note == nullptr ? 60 : note->key;
            row["ratchets"] = static_cast<int>(trigger->ratchets);
        }
        rows.push_back(row);
    }
    return rows;
}

QVariantMap PatternModel::selected() const {
    QVariantMap detail;
    const auto* trigger = selected_step_ < 0 ? nullptr : triggerAt(selected_step_);
    detail["step"] = selected_step_;
    detail["exists"] = trigger != nullptr;
    if (trigger == nullptr) return detail;
    const auto* note = std::get_if<blokkily::Note>(&trigger->musical_data);
    detail["key"] = note == nullptr ? 60 : note->key;
    detail["noteName"] = note_name(note == nullptr ? 60 : note->key);
    detail["velocity"] = note == nullptr ? 0.8 : static_cast<double>(note->velocity);
    detail["velocityUnits"] = velocity_units(note == nullptr ? 0.8F : note->velocity);
    detail["probability"] = static_cast<double>(trigger->probability);
    detail["ratchets"] = static_cast<int>(trigger->ratchets);
    detail["micro"] = static_cast<int>(trigger->micro_offset);
    detail["loop"] = static_cast<int>(trigger->play_on_loop);
    detail["hasLock"] = !trigger->locks.empty();
    detail["lockText"] = lock_text(*trigger);
    if (!trigger->locks.empty()) {
        const auto& lock = trigger->locks.front();
        detail["lockIndex"] = lock.parameter_index;
        detail["lockValue"] = lock.value;
        detail["lockModulation"] = lock.kind == blokkily::ParameterLock::Kind::modulation;
    } else {
        detail["lockIndex"] = 0;
        detail["lockValue"] = 0.5;
        detail["lockModulation"] = false;
    }
    return detail;
}

// The roll shows at least two octaves and always widens to fit the pattern, so
// no note can be drawn outside the lanes the keyboard gutter labels.
namespace {
constexpr int minimum_roll_span = 24; // never show less than two octaves
}

int PatternModel::lowKey() const {
    int lowest = 127;
    int highest = 0;
    for (const auto& event : pattern().events())
        if (const auto* note = std::get_if<blokkily::Note>(&event.musical_data)) {
            lowest = std::min(lowest, static_cast<int>(note->key));
            highest = std::max(highest, static_cast<int>(note->key));
        }
    if (pattern().events().empty()) { lowest = 48; highest = 60; }
    int low = lowest - 4;
    const int span = (highest + 4) - low;
    if (span < minimum_roll_span) low -= (minimum_roll_span - span) / 2;
    return qBound(0, low, 127 - minimum_roll_span);
}

int PatternModel::highKey() const {
    int lowest = 127;
    int highest = 0;
    for (const auto& event : pattern().events())
        if (const auto* note = std::get_if<blokkily::Note>(&event.musical_data)) {
            lowest = std::min(lowest, static_cast<int>(note->key));
            highest = std::max(highest, static_cast<int>(note->key));
        }
    if (pattern().events().empty()) highest = 60;
    return qBound(lowKey() + minimum_roll_span, highest + 4, 127);
}

bool PatternModel::hasStep(int step) const { return triggerAt(step) != nullptr; }

void PatternModel::selectStep(int step) {
    selected_step_ = qBound(-1, step, step_count - 1);
    emit patternChanged();
}

void PatternModel::toggleStep(int step, int key) {
    if (const auto* existing = triggerAt(step)) {
        const auto id = existing->id;
        beginResetModel();
        (void)pattern().remove(id);
        endResetModel();
        selected_step_ = step;
        emit patternChanged();
        return;
    }
    blokkily::Trigger trigger;
    trigger.start = step * ticks_per_step;
    trigger.duration = 96;
    trigger.musical_data = blokkily::Note{static_cast<std::int16_t>(key), 0.9F, 0.0F};
    beginResetModel();
    (void)pattern().add(trigger);
    endResetModel();
    selected_step_ = step;
    emit patternChanged();
}

void PatternModel::mutate(int step, const std::function<void(blokkily::Trigger&)>& edit) {
    const auto* existing = triggerAt(step);
    if (existing == nullptr) return;
    blokkily::Trigger replacement = *existing;
    edit(replacement);
    beginResetModel();
    (void)pattern().update(replacement.id, replacement);
    endResetModel();
    emit patternChanged();
}

void PatternModel::transposeSelected(int semitones) {
    mutate(selected_step_, [semitones](blokkily::Trigger& trigger) {
        if (auto* note = std::get_if<blokkily::Note>(&trigger.musical_data))
            note->key = static_cast<std::int16_t>(qBound(0, note->key + semitones, 127));
    });
}

void PatternModel::setSelectedVelocity(double velocity) {
    mutate(selected_step_, [velocity](blokkily::Trigger& trigger) {
        if (auto* note = std::get_if<blokkily::Note>(&trigger.musical_data))
            note->velocity = static_cast<float>(qBound(0.0, velocity, 1.0));
    });
}

void PatternModel::setSelectedProbability(double probability) {
    mutate(selected_step_, [probability](blokkily::Trigger& trigger) {
        trigger.probability = static_cast<float>(qBound(0.0, probability, 1.0));
    });
}

void PatternModel::setSelectedRatchets(int ratchets) {
    mutate(selected_step_, [ratchets](blokkily::Trigger& trigger) {
        trigger.ratchets = static_cast<std::uint8_t>(qBound(1, ratchets, 16));
    });
}

void PatternModel::setSelectedMicroOffset(int ticks) {
    mutate(selected_step_, [ticks](blokkily::Trigger& trigger) {
        trigger.micro_offset = qBound(-59, ticks, 59);
    });
}

void PatternModel::setSelectedPlayOnLoop(int loop) {
    mutate(selected_step_, [loop](blokkily::Trigger& trigger) {
        trigger.play_on_loop = static_cast<std::uint8_t>(qBound(0, loop, 8));
    });
}

void PatternModel::setSelectedLock(int index, double value, bool modulation) {
    mutate(selected_step_, [index, value, modulation](blokkily::Trigger& trigger) {
        trigger.locks = {{"level", qBound(0, index, 15), qBound(0.0, value, 1.0),
                          modulation ? blokkily::ParameterLock::Kind::modulation
                                     : blokkily::ParameterLock::Kind::automation}};
    });
}

void PatternModel::clearSelectedLock() {
    mutate(selected_step_, [](blokkily::Trigger& trigger) { trigger.locks.clear(); });
}

void PatternModel::replace(blokkily::Pattern replacement) {
    beginResetModel();
    pattern() = std::move(replacement);
    endResetModel();
    emit patternChanged();
}

Transport::Transport(QObject* parent) : QObject(parent) {
    timer_.setInterval(16);
    QObject::connect(&timer_, &QTimer::timeout, this, &Transport::tick);
}

// Bar.beat.sixteenth across the whole arrangement, which is what a transport
// readout means. A step is a sixteenth note, so sixteen of them make a bar.
QString Transport::position() const {
    const auto absolute = static_cast<int>(step_position_);
    const int step = absolute % steps_per_bar;
    return QString("%1.%2.%3")
        .arg(absolute / steps_per_bar + 1).arg(step / 4 + 1).arg(step % 4 + 1);
}

void Transport::setSongBars(int bars) {
    bars_ = std::max(1, bars);
    locate(step_position_);
}

void Transport::followSamples(std::uint64_t samples, double sample_rate) {
    following_ = true;
    if (sample_rate <= 0.0 || bpm_ <= 0.0) return;
    // Four sixteenths to the beat.
    const double samples_per_step = sample_rate * 60.0 / (bpm_ * 4.0);
    if (samples_per_step <= 0.0) return;
    locate(static_cast<double>(samples) / samples_per_step);
}

void Transport::releaseFollowing() { following_ = false; }

void Transport::play() {
    if (playing_) return;
    playing_ = true;
    clock_.restart();
    timer_.start();
    emit changed();
}

void Transport::stop() {
    if (!playing_) return;
    playing_ = false;
    timer_.stop();
    emit changed();
}

void Transport::toggle() { playing_ ? stop() : play(); }

void Transport::rewind() {
    step_position_ = 0.0;
    clock_.restart();
    emit changed();
}

void Transport::locate(double step) {
    const double length = steps_per_bar * bars_;
    step_position_ = std::fmod(std::fmod(step, length) + length, length);
    clock_.restart();
    emit changed();
}

void Transport::setBpm(double bpm) {
    bpm_ = qBound(20.0, bpm, 300.0);
    emit changed();
}

void Transport::tick() {
    // While an engine is bound the playhead follows its sample position; this
    // free-running clock only drives the interface when nothing is rendering.
    if (following_) return;
    // A 16th note per step: one beat covers four steps.
    const double elapsed = static_cast<double>(clock_.restart()) / 1000.0;
    locate(step_position_ + elapsed * bpm_ / 60.0 * 4.0);
}

AppController::AppController(SongModel* song, PatternModel* pattern, Transport* transport,
                             QObject* parent)
    : QObject(parent), song_(song), pattern_(pattern), transport_(transport) {
    if (pattern_ != nullptr)
        QObject::connect(pattern_, &PatternModel::patternChanged, this, [this] {
            // Editing a step changes what the arrangement plays, so the engine
            // recompiles; the transport keeps running while it does.
            if (engine_) (void)rebuildEngine();
        });
    if (song_ != nullptr) {
        QObject::connect(song_, &SongModel::structureChanged, this, [this] {
            if (engine_) (void)rebuildEngine();
        });
        // A fader move only changes gains, so it reaches the running engine
        // without rebuilding the graph or interrupting playback.
        QObject::connect(song_, &SongModel::mixChanged, this, &AppController::applyMix);
    }
    meter_timer_.setInterval(33);
    QObject::connect(&meter_timer_, &QTimer::timeout, this, &AppController::pollMeters);
}

QString AppController::activeInstrument() const {
    if (song_ == nullptr) return QStringLiteral("Choose an instrument");
    const auto tracks = song_->tracks();
    const int selected = song_->selectedTrack();
    if (selected < 0 || selected >= tracks.size()) return QStringLiteral("Choose an instrument");
    const auto row = tracks.at(selected).toMap();
    // The strip's own name is shown beside this, so the label names only the
    // instrument the selected track carries.
    if (!row.value("hasInstrument").toBool()) return QStringLiteral("Choose an instrument");
    return row.value("instrument").toString();
}

std::vector<blokkily::InstrumentSlot> AppController::instruments() const {
    // `slots` is a Qt keyword, so the local carries the longer name.
    std::vector<blokkily::InstrumentSlot> assigned;
    if (song_ == nullptr) return assigned;
    for (const auto& track : song_->song().tracks)
        if (!track.instrument.format.empty()) assigned.push_back(track.instrument);
    return assigned;
}

void AppController::scanPlugins() {
    scanPluginPaths(blokkily::ClapCatalog::system_paths(),
                    blokkily::Vst3PluginInstance::system_paths(),
                    blokkily::SoundFontCatalog::system_paths());
}

void AppController::scanPluginPaths(
    const std::vector<std::filesystem::path>& clap_paths,
    const std::vector<std::filesystem::path>& vst3_paths,
    const std::vector<std::filesystem::path>& soundfont_paths) {
    // CLAP is the first-class format, so it leads the browser; every installed
    // instrument type is still presented through the same list.
    const auto clap = blokkily::ClapCatalog{}.scan_paths(clap_paths);
    const auto vst3 = blokkily::Vst3PluginInstance::scan_paths(vst3_paths);
    const auto soundfonts = blokkily::SoundFontCatalog::scan_paths(soundfont_paths);
    plugins_.clear();
    for (const auto& plugin : clap.plugins)
        plugins_.push_back(plugin_entry("CLAP", plugin.name, plugin.vendor,
                                        plugin.library.string(), plugin.id));
    for (const auto& plugin : vst3)
        plugins_.push_back(plugin_entry("VST3", plugin.name, plugin.manufacturer,
                                        plugin.bundle.string(), plugin.identifier,
                                        static_cast<int>(plugin.index)));
    for (const auto& soundfont : soundfonts)
        plugins_.push_back(plugin_entry("SF", soundfont.stem().string(),
                                        soundfont.parent_path().string(), soundfont.string()));
    soundfont_status_ = QString("%1 installed SoundFont%2")
                            .arg(soundfonts.size()).arg(soundfonts.size() == 1 ? "" : "s");
    status_ = QString("Scan: %1 CLAP, %2 VST3, %3 SoundFont, %4 failure%5")
                  .arg(clap.plugins.size()).arg(vst3.size())
                  .arg(soundfonts.size()).arg(clap.failures.size())
                  .arg(clap.failures.size() == 1 ? "" : "s");
    emit pluginsChanged();
    emit soundfontStatusChanged();
    emit statusChanged();
}

std::unique_ptr<blokkily::PluginInstance> AppController::createInstrument(
    const blokkily::InstrumentSlot& slot, std::string* error) const {
    if (slot.format == "CLAP")
        return blokkily::ClapPluginInstance::create(slot.path, slot.identifier, error);
    if (slot.format == "VST3") {
        std::size_t index = 0;
        const auto found = blokkily::Vst3PluginInstance::scan(slot.path);
        for (std::size_t candidate = 0; candidate < found.size(); ++candidate)
            if (found[candidate].identifier == slot.identifier) { index = candidate; break; }
        return blokkily::Vst3PluginInstance::create(slot.path, index, error);
    }
    if (slot.format == "SoundFont") {
        auto synth = std::make_unique<blokkily::SoundFontSynth>();
        if (synth->load(slot.path)) return synth;
        if (error != nullptr) *error = "could not load SoundFont";
        return nullptr;
    }
    if (error != nullptr) *error = "unknown instrument format";
    return nullptr;
}

void AppController::applyMix() {
    if (engine_ && song_ != nullptr) engine_->apply_mix(song_->song());
}

void AppController::pollMeters() {
    if (!engine_ || song_ == nullptr) return;
    if (transport_ != nullptr && transport_->playing())
        transport_->followSamples(engine_->sample_position(), engine_->sample_rate());
    std::vector<float> peaks(engine_->track_count(), 0.0F);
    for (std::size_t track = 0; track < peaks.size(); ++track)
        peaks[track] = engine_->track_peak(track);
    song_->setMeters(peaks, engine_->master_peak());
}

bool AppController::rebuildEngine() {
    if (song_ == nullptr || transport_ == nullptr) return false;
    const bool resume = transport_->playing();
    auto& song = song_->song();

    // Carry each instrument's own state across the rebuild, so recompiling the
    // arrangement never resets a synth a producer has already dialled in.
    if (engine_)
        for (std::size_t track = 0; track < song.tracks.size(); ++track)
            if (engine_->has_instrument(track)) {
                auto state = engine_->save_track_state(track);
                if (!state.empty()) song.tracks[track].instrument.state = std::move(state);
            }
    if (audio_output_) audio_output_->stop();
    engine_.reset();

    auto next = std::make_unique<blokkily::SongEngine>();
    std::string error;
    int loaded = 0;
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        const auto& slot = song.tracks[track].instrument;
        if (slot.format.empty()) continue;
        std::string reason;
        auto instrument = createInstrument(slot, &reason);
        if (!instrument) { if (error.empty()) error = reason; continue; }
        next->set_instrument(track, std::move(instrument));
        ++loaded;
    }
    if (!next->prepare(song, transport_->bpm(), 48000.0, 512, 0, &error)) {
        status_ = QString("Song could not prepare · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        emit activeInstrumentChanged();
        return false;
    }
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        const auto& state = song.tracks[track].instrument.state;
        if (!state.empty()) (void)next->load_track_state(track, state);
    }
    engine_ = std::move(next);
    transport_->setSongBars(song_->bars());

    // The engine exists even when the machine has no audio device, so a song
    // can still be arranged and bounced to a file on a silent host.
    if (!audio_output_) {
        auto output = std::make_unique<blokkily::RtAudioOutput>();
        std::string device_error;
        if (output->open(*engine_, 48000, 512, &device_error)) {
            audio_output_ = std::move(output);
        } else {
            status_ = QString("Audio device unavailable · %1")
                          .arg(QString::fromStdString(device_error));
            emit statusChanged();
            emit activeInstrumentChanged();
            return loaded > 0;
        }
    } else {
        audio_output_->rebind(*engine_);
    }
    engine_->set_playing(resume);
    if (resume && !audio_output_->start(&error)) {
        transport_->stop();
        engine_->set_playing(false);
        status_ = QString("Audio start failed · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    status_ = QString("Ready · %1 track%2, %3 instrument%4")
                  .arg(song.tracks.size()).arg(song.tracks.size() == 1 ? "" : "s")
                  .arg(loaded).arg(loaded == 1 ? "" : "s");
    emit statusChanged();
    emit activeInstrumentChanged();
    return true;
}

void AppController::assignInstrument(int track, const blokkily::InstrumentSlot& slot,
                                     const QString& label) {
    if (song_ == nullptr) return;
    while (song_->trackCount() <= track) song_->addTrack();
    song_->song().tracks[static_cast<std::size_t>(track)].name = label.toStdString();
    song_->setInstrument(track, slot);
}

bool AppController::selectInstrument(int index) {
    if (song_ == nullptr || index < 0 || index >= plugins_.size()) return false;
    const auto entry = plugins_.at(index).toMap();
    const QString format = entry.value("format").toString();
    blokkily::InstrumentSlot slot;
    slot.format = (format == "SF" ? QStringLiteral("SoundFont") : format).toStdString();
    slot.path = entry.value("path").toString().toStdString();
    slot.identifier = entry.value("identifier").toString().toStdString();
    song_->setInstrument(song_->selectedTrack(), slot);
    return engine_ != nullptr;
}

void AppController::togglePlayback() {
    if (!engine_ || !transport_) {
        status_ = "Load an instrument onto a track before pressing Play";
        emit statusChanged();
        return;
    }
    if (transport_->playing()) {
        engine_->set_playing(false);
        if (audio_output_) audio_output_->stop();
        meter_timer_.stop();
        transport_->releaseFollowing();
        transport_->stop();
        pollMeters();
        return;
    }
    std::string error;
    engine_->set_playing(true);
    if (audio_output_ && !audio_output_->start(&error)) {
        engine_->set_playing(false);
        status_ = QString("Audio start failed · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return;
    }
    meter_timer_.start();
    transport_->play();
}

void AppController::setTempo(double bpm) {
    if (!transport_) return;
    transport_->setBpm(bpm);
    if (engine_) (void)rebuildEngine();
}

bool AppController::exportAudioFile(const QString& path, const QString& depth) {
    if (!engine_) {
        export_status_ = "Load an instrument before exporting";
        emit exportStatusChanged();
        return false;
    }
    const auto format = depth == "PCM16"  ? blokkily::WaveFormat::pcm16
                      : depth == "PCM24"  ? blokkily::WaveFormat::pcm24
                                          : blokkily::WaveFormat::float32;
    std::string error;
    // Half a second of tail so the last note's release is part of the file.
    const auto report = bounce_song(*engine_, local_path(path).toStdString(), format,
                                    24000, &error);
    if (!report) {
        export_status_ = QString("Export failed · %1").arg(QString::fromStdString(error));
        emit exportStatusChanged();
        return false;
    }
    export_status_ = QString("%1 · %2 s · peak %3 dB%4")
                         .arg(QFileInfo(local_path(path)).fileName())
                         .arg(static_cast<double>(report->frames) / 48000.0, 0, 'f', 1)
                         .arg(blokkily::linear_to_db(report->peak), 0, 'f', 1)
                         .arg(report->clipped ? " · CLIPPED" : "");
    emit exportStatusChanged();
    return true;
}

bool AppController::saveProjectFile(const QString& path) {
    return saveProject(local_path(path));
}

bool AppController::loadProjectFile(const QString& path) {
    const bool loaded = loadProject(local_path(path));
    if (loaded && transport_) transport_->rewind();
    return loaded;
}

bool AppController::scanClapFile(const QString& path) {
    const auto result = blokkily::ClapCatalog{}.scan_file(path.toStdString());
    plugins_.clear();
    for (const auto& plugin : result.plugins)
        plugins_.push_back(plugin_entry("CLAP", plugin.name, plugin.vendor));
    status_ = QString("CLAP fixture: %1 plugin%2, %3 failure%4")
                  .arg(result.plugins.size()).arg(result.plugins.size() == 1 ? "" : "s")
                  .arg(result.failures.size()).arg(result.failures.size() == 1 ? "" : "s");
    emit pluginsChanged();
    emit statusChanged();
    return result.plugins.size() == 1 && result.failures.empty();
}

bool AppController::verifyClap(const QString& path) {
    if (!scanClapFile(path) || plugins_.isEmpty()) return false;
    std::string error;
    auto plugin = blokkily::ClapPluginInstance::create(
        path.toStdString(), "dev.blokkily.test", &error);
    bool valid = plugin != nullptr && plugin->activate(48000.0, 1, 256);
    std::vector<float> left(128), right(128);
    if (valid) {
        const blokkily::PluginEvent note{blokkily::PluginEvent::Type::note_on, 64, 60, 1.0};
        plugin->process({left, right}, std::span{&note, 1});
        valid = left[63] == 0.0F && left[64] != 0.0F;
        const auto state = plugin->save_state();
        valid = valid && !state.empty() && plugin->load_state(state);
    }
    if (valid) {
        auto transport_plugin = blokkily::ClapPluginInstance::create(
            path.toStdString(), "dev.blokkily.test", &error);
        blokkily::RealtimePlayback playback(std::move(transport_plugin));
        blokkily::Pattern pattern(1920, 480);
        blokkily::Trigger trigger;
        trigger.start = 120;
        trigger.duration = 120;
        trigger.musical_data = blokkily::Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
        valid = playback.prepare(pattern, 120.0, 48000.0, 8192);
        std::vector<float> transport_left(8192), transport_right(8192);
        playback.set_playing(true);
        playback.process({transport_left, transport_right});
        valid = valid && transport_left[5999] == 0.0F && transport_left[6000] != 0.0F;
    }
    if (valid) {
        auto slot_plugin = blokkily::ClapPluginInstance::create(
            path.toStdString(), "dev.blokkily.test", &error);
        if (slot_plugin != nullptr)
            assignInstrument(0, {"CLAP", path.toStdString(), "dev.blokkily.test",
                                 slot_plugin->save_state()}, "CLAP LEAD");
    }
    status_ = valid ? "CLAP → transport: sample-accurate"
                    : QString("CLAP processing failed · %1").arg(QString::fromStdString(error));
    emit statusChanged();
    return valid;
}

bool AppController::verifyVst3(const QString& path) {
    const auto descriptors = blokkily::Vst3PluginInstance::scan(path.toStdString());
    bool valid = descriptors.size() == 1;
    std::string error;
    if (valid) {
        auto plugin = blokkily::Vst3PluginInstance::create(path.toStdString(), 0, &error);
        valid = plugin != nullptr && plugin->activate(48000.0, 1, 256);
        if (valid) {
            std::vector<float> left(128), right(128);
            const blokkily::PluginEvent note{blokkily::PluginEvent::Type::note_on, 32, 60, 1.0};
            plugin->process({left, right}, std::span{&note, 1});
            valid = left[31] == 0.0F && left[32] != 0.0F;
            const auto state = plugin->save_state();
            valid = valid && !state.empty() && plugin->load_state(state);
        }
    }
    if (valid) {
        plugins_.push_back(plugin_entry("VST3", descriptors.front().name,
                                        descriptors.front().manufacturer));
        auto slot_plugin = blokkily::Vst3PluginInstance::create(path.toStdString(), 0, &error);
        if (slot_plugin != nullptr)
            assignInstrument(1, {"VST3", path.toStdString(), descriptors.front().identifier,
                                 slot_plugin->save_state()}, "VST3 PAD");
    }
    status_ = valid
        ? QString("CLAP + VST3 → transport: sample-accurate")
        : QString("VST3 processing failed · %1").arg(QString::fromStdString(error));
    emit pluginsChanged();
    emit statusChanged();
    return valid;
}

// Proves through the whole app that a parameter lock on a step actually
// changes what that step sounds like, rather than only being stored.
bool AppController::verifyParameterLocks(const QString& clap_path) {
    std::string error;
    auto plugin = blokkily::ClapPluginInstance::create(
        clap_path.toStdString(), "dev.blokkily.test", &error);
    if (plugin == nullptr) {
        status_ = "Parameter lock check could not create the CLAP instrument";
        emit statusChanged();
        return false;
    }
    blokkily::Pattern pattern(1920, 480);
    blokkily::Trigger trigger;
    trigger.start = 120;
    trigger.duration = 120;
    trigger.musical_data = blokkily::Note{60, 1.0F, 0.0F};
    trigger.locks = {{"level", 0, 0.75, blokkily::ParameterLock::Kind::automation},
                     {"level", 0, 0.125, blokkily::ParameterLock::Kind::modulation}};
    (void)pattern.add(trigger);

    blokkily::RealtimePlayback playback(std::move(plugin));
    bool valid = playback.prepare(pattern, 120.0, 48000.0, 8192);
    std::vector<float> left(8192), right(8192);
    playback.set_playing(true);
    playback.process({left, right});
    valid = valid && left[5999] == 0.0F && left[6000] > 0.874F && left[6000] < 0.876F;
    status_ = valid ? "Parameter locks → automation + modulation applied"
                    : "Parameter locks did not reach the instrument";
    emit statusChanged();
    return valid;
}

bool AppController::verifySoundFont(const QString& path) {
    blokkily::SoundFontSynth synth;
    bool valid = synth.load(path.toStdString()) && synth.activate(48000.0, 1, 2048);
    std::vector<float> left(2048), right(2048);
    if (valid) {
        const blokkily::PluginEvent note{blokkily::PluginEvent::Type::note_on, 0, 60, 1.0};
        synth.process({left, right}, std::span{&note, 1});
        double energy = 0.0;
        for (const float sample : left) energy += std::abs(sample);
        valid = energy > 0.001;
    }
    if (valid)
        assignInstrument(2, {"SoundFont", path.toStdString(), "", synth.save_state()},
                         "SF KEYS");
    soundfont_status_ = valid
        ? QString("Rendered audio · %1").arg(QFileInfo(path).fileName())
        : QString("SoundFont failed · %1").arg(QFileInfo(path).fileName());
    emit soundfontStatusChanged();
    return valid;
}

// The mixer must move real audio, not just numbers on a strip: mute has to
// remove a track from the bus and solo has to leave only its own.
bool AppController::verifyMixer() {
    if (song_ == nullptr || !engine_) return false;
    auto& song = song_->song();
    if (song.tracks.size() < 2) return false;
    const std::vector<blokkily::MixerStrip> before{song.tracks[0].mix, song.tracks[1].mix};
    const bool was_playing = engine_->is_playing();
    const auto resume_at = engine_->sample_position();

    std::vector<float> left(512), right(512);
    const auto render_at = [&](std::uint64_t position) {
        engine_->apply_mix(song);
        engine_->seek(position);
        engine_->set_playing(true);
        std::fill(left.begin(), left.end(), 0.0F);
        std::fill(right.begin(), right.end(), 0.0F);
        engine_->process({left, right});
    };

    song.tracks[0].mix = {0.0, 0.0, false, false};
    song.tracks[1].mix = {0.0, 0.0, false, false};

    // Tracks do not all play from bar one, so find a moment where the track
    // under test actually sounds and judge mute and solo at that same moment.
    std::uint64_t sounding_at = 0;
    bool sounds = false;
    const auto blocks = engine_->song_samples() / 512 + 1;
    for (std::uint64_t block = 0; block < blocks && !sounds; ++block) {
        render_at(block * 512);
        sounds = engine_->track_peak(1) > 0.0F;
        if (sounds) sounding_at = block * 512;
    }

    render_at(sounding_at);
    const float open_peak = engine_->track_peak(1);
    song.tracks[1].mix.mute = true;
    render_at(sounding_at);
    const float muted_peak = engine_->track_peak(1);
    song.tracks[1].mix.mute = false;
    song.tracks[0].mix.solo = true;
    render_at(sounding_at);
    const float shadowed_peak = engine_->track_peak(1);

    song.tracks[0].mix = before[0];
    song.tracks[1].mix = before[1];
    engine_->apply_mix(song);
    engine_->set_playing(was_playing);
    engine_->seek(resume_at);

    const bool valid = sounds && open_peak > 0.0F && muted_peak == 0.0F && shadowed_peak == 0.0F;
    status_ = valid ? "Mixer → mute and solo change the bus"
                    : "Mixer did not change what the bus carries";
    emit statusChanged();
    return valid;
}

// The export gate: a bounce must be a real, readable, non-silent stereo file of
// the arrangement's length, not an empty file that merely exists.
bool AppController::verifyBounce(const QString& path) {
    if (!engine_) return false;
    if (!exportAudioFile(path, "PCM24")) return false;
    std::string error;
    const auto rendered = blokkily::read_wave(local_path(path).toStdString(), &error);
    if (!rendered) {
        export_status_ = QString("Export unreadable · %1").arg(QString::fromStdString(error));
        emit exportStatusChanged();
        return false;
    }
    const bool valid = rendered->channels == 2 && rendered->sample_rate == 48000 &&
                       rendered->frames == engine_->song_samples() + 24000 &&
                       std::any_of(rendered->interleaved.begin(), rendered->interleaved.end(),
                                   [](float sample) { return std::abs(sample) > 0.0001F; });
    if (!valid) {
        export_status_ = "Export did not contain the arrangement";
        emit exportStatusChanged();
    }
    return valid;
}

bool AppController::saveProject(const QString& path) {
    if (song_ == nullptr) return false;
    auto& song = song_->song();
    // Ask every live instrument what it wants persisted before writing.
    if (engine_)
        for (std::size_t track = 0; track < song.tracks.size(); ++track)
            if (engine_->has_instrument(track)) {
                auto state = engine_->save_track_state(track);
                if (!state.empty()) song.tracks[track].instrument.state = std::move(state);
            }
    blokkily::Project project;
    project.name = "Blokkily Session";
    project.tempo = transport_ == nullptr ? 120.0 : transport_->bpm();
    project.song = song;
    std::string error;
    const bool valid = blokkily::ProjectFile::save(project, path.toStdString(), &error);
    project_status_ = valid ? "Saved" : "Save failed";
    project_detail_ = valid ? QString("%1 · %2 track%3")
                                  .arg(QFileInfo(path).fileName())
                                  .arg(song.tracks.size())
                                  .arg(song.tracks.size() == 1 ? "" : "s")
                            : QString::fromStdString(error);
    emit projectStatusChanged();
    return valid;
}

bool AppController::loadProject(const QString& path) {
    if (song_ == nullptr) return false;
    std::string error;
    auto project = blokkily::ProjectFile::load(path.toStdString(), &error);
    if (!project) {
        project_status_ = "Load failed";
        project_detail_ = QString::fromStdString(error);
        emit projectStatusChanged();
        return false;
    }
    if (audio_output_) audio_output_->stop();
    engine_.reset();
    if (transport_ != nullptr) transport_->setBpm(project->tempo);
    const auto tracks = project->song.tracks.size();
    const auto patterns = project->song.patterns.size();
    song_->replace(std::move(project->song));
    if (pattern_ != nullptr) pattern_->refresh();
    project_status_ = QString("Restored from disk");
    project_detail_ = QString("%1 pattern%2 · %3 track%4")
                          .arg(patterns).arg(patterns == 1 ? "" : "s")
                          .arg(tracks).arg(tracks == 1 ? "" : "s");
    emit projectStatusChanged();
    (void)rebuildEngine();
    return true;
}
