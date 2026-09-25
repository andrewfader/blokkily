#include "app_controller.hpp"
#include "app_controller_internal.hpp"
#include "blokkily/instruments/soundfont_catalog.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QVariantMap>
#include <QUrl>

#include <cstring>
#include <system_error>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace {
// What the device is asked for. It may answer with another rate, and the engine
// follows the answer rather than the request.
constexpr unsigned int default_sample_rate = 48000;

QString local_path(const QString& value) {
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : value;
}

using app_detail::plugin_entry;
}

AppController::AppController(SongModel* song, PatternModel* pattern, Transport* transport,
                             QObject* parent, std::unique_ptr<blokkily::RtAudioOutput> output,
                             std::unique_ptr<blokkily::MidiInput> midi)
    : QObject(parent), song_(song), pattern_(pattern), transport_(transport),
      midi_input_(midi ? std::move(midi) : std::make_unique<blokkily::MidiInput>()),
      recompiler_(
          [this]() -> blokkily::RecompileCoalescer::Outcome {
              std::string error;
              if (refreshArrangement(&error)) return {true, {}};
              return {false, error};
          },
          // A turn of the event loop from now, so every request made in this
          // one shares the recompile.
          [this](std::function<void()> work) { QTimer::singleShot(0, this, std::move(work)); }),
      audio_output_(std::move(output)) {
    // What the browser lists follows both what the scan found and what the
    // producer has typed, so a plugin arriving mid-scan reaches a filtered
    // browser too.
    QObject::connect(this, &AppController::pluginsChanged,
                     this, &AppController::browserChanged);
    if (pattern_ != nullptr)
        QObject::connect(pattern_, &PatternModel::contentChanged, this, [this] {
            // Editing a step changes what the arrangement plays, not what plays
            // it, so the recompiled timeline is handed to the running engine.
            // The song keeps going from where it was, on the instruments it
            // already has, and the next block plays the edit.
            if (engine_) {
                if (builtFromCurrentGraph()) requestRecompile();
                else (void)rebuildEngine();
            }
        });
    if (song_ != nullptr) {
        QObject::connect(song_, &SongModel::structureChanged, this, [this] {
            // A clip moved on the timeline is the same kind of change as a
            // step edit. A track added or an instrument swapped is not: that
            // needs a graph the running engine does not have.
            if (engine_ && builtFromCurrentGraph()) {
                requestRecompile();
                return;
            }
            // A track that has just been given an instrument needs an engine
            // even if none existed: a keyboard must sound before playback. A
            // deleted track says where the others went, so each keeps its
            // own instrument instance.
            if (engine_ || !instruments().empty() || hasAudioClips()) {
                const auto* remap = song_->pendingTrackRemap();
                (void)rebuildEngine(remap);
            }
        });
        // A fader move only changes gains, so it reaches the running engine
        // without rebuilding the graph or interrupting playback.
        QObject::connect(song_, &SongModel::mixChanged, this, &AppController::applyMix);
        // The transport reads bars, beats and the tempo shown from the song's
        // own maps; a tempo edit reaches the engine as a recompile, through
        // structureChanged, like any other change to when events fall.
        QObject::connect(song_, &SongModel::timebaseChanged, this, &AppController::syncTimebase);
        syncTimebase();
        // Undo or redo put back instrument states: the running instances are
        // given them (a sampler's program, a knob turned in a plugin's window).
        QObject::connect(song_, &SongModel::instrumentStatesRestored, this,
                         &AppController::restoreInstrumentStates);
        // A controller plays the armed tracks - or, with none armed, the
        // selected one - in the song's own tuning and scale, from the next key
        // pressed.
        const auto follow = [this] {
            updateInputRoutes();
            updateKeyMap();
        };
        QObject::connect(song_, &SongModel::songChanged, this, follow);
        QObject::connect(song_, &SongModel::tuningChanged, this, follow);
        follow();
        connectSampler();
        connectAudioInput();
    }
    refreshMidiPorts();
    // A held note is let go on a timer rather than on a second press, so a
    // keyboard cannot leave a voice sounding after the finger has left it.
    audition_timer_.setSingleShot(true);
    audition_timer_.setInterval(450);
    QObject::connect(&audition_timer_, &QTimer::timeout, this,
                     &AppController::releaseSoundingNotes);
    meter_timer_.setInterval(33);
    QObject::connect(&meter_timer_, &QTimer::timeout, this, &AppController::pollMeters);
    // Plugins are served on the main thread while an engine exists: what they
    // asked of it, and the edits their windows made.
    editor_timer_.setInterval(30);
    QObject::connect(&editor_timer_, &QTimer::timeout, this, &AppController::serviceEditors);
    // A candidate that stops answering is given up on rather than waited for.
    scan_deadline_.setSingleShot(true);
    QObject::connect(&scan_deadline_, &QTimer::timeout, this, [this] {
        if (!scanner_) return;
        scan_expired_ = true;
        scanner_->kill();
    });
}

AppController::~AppController() {
    // A take still being written is abandoned with the session, and so is the
    // temporary folder of a session that was never saved.
    discardAudioTake();
    if (!session_audio_dir_.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(session_audio_dir_, ignored);
    }
    // An import still decoding reports back to this object: it is waited for
    // here, and what it posts is discarded with the object.
    for (auto& [ticket, worker] : imports_)
        if (worker.joinable()) worker.join();
    if (scanner_) {
        scanner_->kill();
        scanner_->waitForFinished(1000);
    }
}

// Nothing is left holding a note across a rebuild: the engine that was told to
// sound it no longer exists, so the release would land on an instrument that
// never heard the press.
void AppController::forgetSoundingNotes() {
    sounding_.clear();
    performed_.clear();
    audition_timer_.stop();
}

// Lets go of every note the keyboard is holding, on the track that was told to
// sound it rather than on whichever one happens to be selected now.
void AppController::releaseSoundingNotes() {
    releasePerformed();
    if (engine_ != nullptr)
        for (const auto& held : sounding_)
            (void)engine_->play_live(static_cast<std::size_t>(audition_track_),
                                     {blokkily::PluginEvent::Type::note_off, 0, held.key,
                                      0.0, held.cents});
    sounding_.clear();
}

void AppController::releaseAudition() {
    audition_timer_.stop();
    releaseSoundingNotes();
}

bool AppController::auditionPitches(const std::vector<blokkily::TunedPitch>& pitches,
                                    double velocity, bool held) {
    if (song_ == nullptr) return false;
    // A press while an engine exists is heard immediately; without one there is
    // nothing to sound through, and the surface still writes what was played.
    if (!engine_ && !instruments().empty()) (void)rebuildEngine();
    if (!engine_) return false;
    // Whatever was still sounding is let go first, so a run of presses does not
    // pile voices up on the instrument. It is released on the track that was
    // told to sound it: a producer who changes track mid-press would otherwise
    // leave a voice held on the instrument they just left.
    releaseSoundingNotes();
    const auto track = static_cast<std::size_t>(song_->selectedTrack());
    if (track >= engine_->track_count() || !engine_->has_instrument(track)) return false;
    std::string error;
    if (audio_output_ && !audio_output_->start(&error)) {
        status_ = QString("Audio start failed · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    meter_timer_.start();
    audition_track_ = song_->selectedTrack();
    for (const auto& pitch : pitches) {
        if (!engine_->play_live(track, {blokkily::PluginEvent::Type::note_on, 0, pitch.key,
                                        velocity, pitch.cents}))
            break;
        sounding_.push_back(pitch);
    }
    // A held key rings until it is let go; the limit only catches a release
    // that never arrives.
    audition_timer_.start(held ? 8000 : 450);
    return !sounding_.empty();
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

blokkily::ProcessorContext AppController::processorContext() const {
    blokkily::ProcessorContext context;
    if (!project_path_.isEmpty())
        context.project_dir = std::filesystem::path(project_path_.toStdString()).parent_path();
    context.assets = assets_.get();
    return context;
}

void AppController::applyMix() {
    if (engine_ && song_ != nullptr) engine_->apply_mix(song_->song());
}

void AppController::pollMeters() {
    if (!engine_ || song_ == nullptr) return;
    drainTake();
    const auto received = midi_input_->notes_received();
    if (received != midi_notes_) {
        midi_notes_ = received;
        const int key = midi_input_->last_key();
        midi_activity_ = QString("%1 · %2")
                             .arg(song_->degreeName(song_->snapDegree(key)))
                             .arg(midi_input_->last_velocity());
        emit midiActivityChanged();
    }
    if (transport_ != nullptr && transport_->playing())
        transport_->followSamples(engine_->sample_position() %
                                      std::max<std::uint64_t>(1, engine_->song_samples()),
                                  engine_->sample_rate());
    std::vector<float> peaks(engine_->track_count(), 0.0F);
    for (std::size_t track = 0; track < peaks.size(); ++track)
        peaks[track] = engine_->track_peak(track);
    song_->setMeters(peaks, engine_->master_peak());
    std::vector<float> returns(engine_->return_count(), 0.0F);
    for (std::size_t bus = 0; bus < returns.size(); ++bus) returns[bus] = engine_->return_peak(bus);
    song_->setReturnMeters(returns);
}

bool AppController::builtFromCurrentGraph() const {
    if (song_ == nullptr) return false;
    return engine_signature_ == blokkily::graph_signature(song_->song());
}

void AppController::requestRecompile() { recompiler_.request(); }

void AppController::flushRecompile() { recompiler_.flush(); }

bool AppController::refreshArrangement(std::string* failure) {
    if (!engine_ || song_ == nullptr || transport_ == nullptr) return true;
    // The graph changed between the request and now: that is a rebuild, which
    // compiles the song itself.
    if (!builtFromCurrentGraph()) return rebuildEngine();
    std::string error;
    if (!engine_->recompile(song_->song(), 0, &error, clipAssets(engine_->sample_rate()))) {
        if (failure != nullptr) *failure = error;
        // A busy engine is tried again on the next turn; only a refusal that
        // will not clear by itself is worth telling the producer about.
        if (error != blokkily::RecompileCoalescer::busy_reason) {
            status_ = QString("Arrangement unchanged · %1").arg(QString::fromStdString(error));
            emit statusChanged();
        }
        return false;
    }
    transport_->setSongBars(song_->bars());
    return true;
}

bool AppController::auditionStep(int step) {
    if (pattern_ == nullptr) return false;
    const auto pitches = pattern_->pitchesAt(step);
    if (pitches.empty()) return false;
    return auditionPitches(pitches, 0.9);
}

// Banks that are General MIDI and are the ones a Linux, macOS, or Windows host
// is most likely to already have. The list is a preference, not a requirement:
// when none of them is installed the first SoundFont on the machine is used.
namespace {
constexpr const char* preferred_banks[] = {
    "FluidR3_GM.sf2",   "FluidR3_GM.sf3",   "GeneralUser.sf2",
    "GeneralUser GS.sf2", "default-GM.sf2", "default.sf2",
    "TimGM6mb.sf2",     "FatBoy.sf2",       "SGM-V2.01.sf2",
    "Arachno.sf2",      "gm.sf2",           "freepats-general-midi.sf2",
};

// What a track should be set to when nothing has said otherwise. A drum track
// wants the percussion bank rather than a grand piano, which is the difference
// between the opening song sounding like music and sounding like two pianos.
struct DefaultPreset {
    int bank = 0;
    int program = 0;
};

DefaultPreset preset_for_track(const std::string& name) {
    QString label = QString::fromStdString(name).toUpper();
    if (label.contains("DRUM") || label.contains("PERC") || label.contains("KIT"))
        return {128, 0};                      // the General MIDI percussion bank
    if (label.contains("BASS")) return {0, 33};   // electric bass, fingered
    if (label.contains("PAD") || label.contains("STRING")) return {0, 48};
    if (label.contains("LEAD") || label.contains("SYNTH")) return {0, 81};
    return {0, 0};                            // acoustic grand
}
} // namespace

bool AppController::loadDefaultInstrument() {
    return loadDefaultInstrument(blokkily::SoundFontCatalog::system_paths());
}

std::filesystem::path AppController::findDefaultBank(
    const std::vector<std::filesystem::path>& roots) {
    for (const char* wanted : preferred_banks)
        for (const auto& root : roots) {
            std::error_code ignored;
            const auto candidate = root / wanted;
            if (std::filesystem::is_regular_file(candidate, ignored)) return candidate;
        }
    // No familiar bank installed, so whatever this machine does have will do.
    const auto found = blokkily::SoundFontCatalog::scan_paths(roots);
    return found.empty() ? std::filesystem::path{} : found.front();
}

blokkily::InstrumentSlot AppController::defaultSlot(const std::filesystem::path& bank,
                                                    const std::string& track_name) {
    const auto preset = preset_for_track(track_name);
    blokkily::InstrumentSlot slot;
    slot.format = "SoundFont";
    slot.path = bank.string();
    // The preset travels as the instrument's own state, which is the same road
    // a saved project takes, so an opening session and a reloaded one reach
    // the synth through one path.
    const std::string state = slot.path + '\n' + std::to_string(preset.bank) + '\n' +
                              std::to_string(preset.program);
    slot.state.resize(state.size());
    std::memcpy(slot.state.data(), state.data(), state.size());
    return slot;
}

bool AppController::loadDefaultInstrument(const std::vector<std::filesystem::path>& roots) {
    if (song_ == nullptr) return false;
    auto& song = song_->song();
    const bool anything_loaded =
        std::any_of(song.tracks.begin(), song.tracks.end(),
                    [](const blokkily::Track& track) { return !track.instrument.format.empty(); });
    if (anything_loaded) return false;

    const auto bank = findDefaultBank(roots);
    if (bank.empty()) {
        soundfont_status_ = "No SoundFont installed — load a plugin to hear the song";
        emit soundfontStatusChanged();
        return false;
    }
    for (auto& track : song.tracks) track.instrument = defaultSlot(bank, track.name);
    song_->refreshStructure();
    soundfont_status_ = QString("%1 · %2 track%3")
                            .arg(QString::fromStdString(bank.stem().string()))
                            .arg(song.tracks.size())
                            .arg(song.tracks.size() == 1 ? "" : "s");
    emit soundfontStatusChanged();
    return engine_ != nullptr;
}

void AppController::addTrack() {
    if (song_ == nullptr) return;
    // The bank the session already plays is the one a new track most likely
    // wants, and it is already loaded, so it costs nothing to reach for.
    std::filesystem::path bank;
    for (const auto& track : song_->song().tracks)
        if (track.instrument.format == "SoundFont") { bank = track.instrument.path; break; }
    if (bank.empty()) bank = findDefaultBank(blokkily::SoundFontCatalog::system_paths());
    // A numbered track names no instrument family, so it opens on the piano.
    song_->addTrack(bank.empty() ? blokkily::InstrumentSlot{} : defaultSlot(bank, "TRACK"));
}

QString AppController::projectName() const {
    return project_path_.isEmpty() ? QStringLiteral("Untitled")
                                   : QFileInfo(project_path_).completeBaseName();
}

bool AppController::saveProjectInPlace() {
    if (project_path_.isEmpty()) return false;
    return saveProject(project_path_);
}

void AppController::newProject() {
    if (song_ == nullptr) return;
    blokkily::Song blank;
    blank.patterns.front().name = "PATTERN 1";
    blank.tracks.front().name = "TRACK 1";
    // The session keeps the tuning it was written in: a producer working in
    // nineteen tones starts the next song in nineteen tones too.
    blank.tuning = song_->song().tuning;
    blank.scale = song_->song().scale;
    blank.root_degree = song_->song().root_degree;
    // The instrument the first track carried stays on it, so a new song
    // sounds like the last one did until something else is chosen.
    blank.tracks.front().instrument = song_->song().tracks.front().instrument;
    if (audio_output_) audio_output_->stop();
    if (transport_ != nullptr) transport_->stop();
    forgetSoundingNotes();
    // A take in progress belonged to the song being replaced.
    takes_.clear();
    discardAudioTake();
    engine_.reset();
    // The audio the last song played is let go unless the next one plays it.
    clip_assets_.clear();
    assets_->purge_unused();
    song_->replace(std::move(blank));
    if (song_->song().tracks.front().instrument.format.empty()) (void)loadDefaultInstrument();
    else (void)rebuildEngine();
    if (transport_ != nullptr) transport_->rewind();
    project_path_.clear();
    project_status_ = "New session";
    project_detail_ = "Not saved yet";
    song_->markSaved();
    emit projectStatusChanged();
}

bool AppController::rebuildEngine(const blokkily::TrackRemap* remap) {
    if (song_ == nullptr || transport_ == nullptr) return false;
    const bool resume = transport_->playing();
    auto& song = song_->song();
    ++rebuilds_;
    // The new engine compiles the song itself, so an owed recompile is moot.
    recompiler_.cancel();
    // A gesture finished in a plugin's window before the rebuild is a step of
    // history; the old engine's ring goes with it.
    drainPluginEdits();

    // State streams belong to the control thread while the processor is idle.
    if (audio_output_) audio_output_->stop();

    // Every processor whose identity is unchanged is carried into the new
    // graph as the same instance, so rebuilding never reloads, resets or
    // re-reads a synth a producer has already dialled in. Its state is copied
    // into the song too, which is what a save writes.
    const auto wanted = blokkily::graph_signature(song);
    std::vector<blokkily::ReleasedProcessor> adopted;
    if (engine_)
        adopted = blokkily::adopt_processors(engine_->release_processors(), engine_signature_,
                                             wanted, remap);
    // Instruments and effects alike: an effect a producer has dialled in is
    // saved with the song it now sits in, at the slot it moved to.
    for (const auto& kept : adopted) {
        auto* slot = blokkily::song_slot(song, kept.where);
        if (slot == nullptr) continue;
        auto state = kept.instance->save_state();
        if (!state.empty()) slot->state = std::move(state);
    }
    forgetSoundingNotes();
    if (engine_) engine_->connect_input(nullptr);
    engine_.reset();
    engine_signature_ = {};

    auto next = std::make_unique<blokkily::SongEngine>();
    std::string error;
    const auto build = blokkily::populate_graph(*next, song, std::move(adopted),
                                                processorContext());
    error = build.error;
    const int loaded = build.loaded;
    adopted_processors_ = build.adopted;
    // The device is opened before the engine is prepared, because the server
    // and not this code decides the rate: a sink locked to 44.1 kHz answers a
    // request for 48 kHz with 44.1 kHz, and an engine built for the rate that
    // was asked for would then play every note flat and every bar slow. A host
    // with no device still gets an engine, at the rate a bounce is written at.
    QString device_failure;
    // The device opens duplex only while a track takes audio input (item
    // 3.2), and is opened again when that changes.
    const bool wants_input = songTakesAudioInput();
    if (audio_output_ && !audio_output_->opened_for_input(wants_input)) audio_output_->close();
    if (!audio_output_ || !audio_output_->is_open()) {
        auto output = audio_output_ ? std::move(audio_output_)
                                    : std::make_unique<blokkily::RtAudioOutput>();
        output->set_input_wanted(wants_input);
        std::string device_error;
        // Nothing is rendering yet: the stream is opened, not started, so the
        // callback cannot reach the engine before it is prepared.
        if (output->open(*next, default_sample_rate, 512, &device_error))
            audio_output_ = std::move(output);
        else
            device_failure = QString::fromStdString(device_error);
    }
    const bool device_open = audio_output_ && audio_output_->is_open();
    const double rate = device_open && audio_output_->device_info().sample_rate != 0
                            ? static_cast<double>(audio_output_->device_info().sample_rate)
                            : static_cast<double>(default_sample_rate);
    if (!next->prepare(song, rate, 512, 0, &error, clipAssets(rate))) {
        status_ = QString("Song could not prepare · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        emit activeInstrumentChanged();
        return false;
    }
    blokkily::load_fresh_state(*next, song, build);
    engine_ = std::move(next);
    // Open editors follow their tracks; one whose instance was replaced has
    // already been closed with it.
    reconcileEditors(remap);
    editor_timer_.start();
    // The controller's keys reach whichever engine is live, and a take being
    // recorded carries on into it.
    engine_->connect_input(&midi_input_->queue());
    engine_->set_recording(record_armed_);
    // Recorded audio input reaches the take writer through its ring, and
    // every track hears the inputs it is routed to (item 3.2).
    engine_->connect_capture(&take_writer_->ring());
    updateAudioInputs();
    engine_signature_ = wanted;
    transport_->setSongBars(song_->bars());

    // The engine exists even when the machine has no audio device, so a song
    // can still be arranged and bounced to a file on a silent host.
    if (!device_open) {
        status_ = QString("Audio device unavailable · %1").arg(device_failure);
        emit statusChanged();
        emit activeInstrumentChanged();
        return loaded > 0;
    }
    audio_output_->rebind(*engine_);
    engine_->set_playing(resume);
    if (resume && !audio_output_->start(&error)) {
        transport_->stop();
        engine_->set_playing(false);
        status_ = QString("Audio start failed · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    // A keyboard that was being played is heard through the new engine too.
    // So is an armed audio input that is being monitored (item 3.2).
    if (midi_input_->is_open() || monitorsAudioInput()) (void)ensureAudioRunning();
    // Instruments and effects are counted apart: "3 instruments" must not
    // mean one synth and two inserts.
    int instruments = 0;
    for (std::size_t track = 0; track < song.tracks.size(); ++track)
        if (engine_->has_instrument(track)) ++instruments;
    const int effects = loaded - instruments;
    status_ = QString("Ready · %1 track%2, %3 instrument%4")
                  .arg(song.tracks.size()).arg(song.tracks.size() == 1 ? "" : "s")
                  .arg(instruments).arg(instruments == 1 ? "" : "s");
    if (effects > 0)
        status_ += QString(", %1 effect%2 · latency %3")
                       .arg(effects).arg(effects == 1 ? "" : "s")
                       .arg(engine_->output_latency());
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

QVariantList AppController::browserPlugins() const {
    const auto query = browser_filter_.trimmed().toStdString();
    // A ranked row and where it came from, kept together so the browser can
    // load what it lists.
    struct Ranked {
        int score;
        int source;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(static_cast<std::size_t>(plugins_.size()));
    const auto entries = plugins();
    for (int index = 0; index < entries.size(); ++index) {
        const auto fields = entries.at(index).toMap();
        // One kind at a time: instruments for a track to play, or effects to
        // insert after one.
        if (fields.value("kind").toString() != browser_kind_) continue;
        // Name, maker and format are all searched, so an instrument is found
        // by who made it or by what kind of plugin it is as readily as by its
        // own name.
        const auto haystack = QString("%1 %2 %3")
                                  .arg(fields.value("name").toString(),
                                       fields.value("vendor").toString(),
                                       fields.value("format").toString())
                                  .toStdString();
        const int score = blokkily::browser_match_score(query, haystack);
        if (score >= 0) ranked.push_back({score, index});
    }
    // Closest match first; entries the query cannot separate keep the order
    // the scan found them in, so an unfiltered browser is the plain list.
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Ranked& left, const Ranked& right) {
                         return left.score > right.score;
                     });
    QVariantList listed;
    listed.reserve(static_cast<qsizetype>(ranked.size()));
    for (const auto& entry : ranked) {
        auto fields = entries.at(entry.source).toMap();
        fields.insert("source", entry.source);
        listed.push_back(fields);
    }
    return listed;
}

void AppController::setBrowserFilter(const QString& query) {
    if (browser_filter_ == query) return;
    browser_filter_ = query;
    emit browserChanged();
}

bool AppController::selectInstrument(int index) {
    const auto entries = plugins();
    if (song_ == nullptr || index < 0 || index >= entries.size()) return false;
    const auto entry = entries.at(index).toMap();
    // An effect chosen from the browser goes after the instrument, not in
    // its place.
    if (entry.value("kind").toString() == "effect") return addEffect(index);
    const QString format = entry.value("format").toString();
    blokkily::InstrumentSlot slot;
    slot.format = (format == "SF" ? QStringLiteral("SoundFont") : format).toStdString();
    slot.path = entry.value("path").toString().toStdString();
    slot.identifier = entry.value("identifier").toString().toStdString();
    song_->setInstrument(song_->selectedTrack(), slot);
    return engine_ != nullptr;
}

void AppController::togglePlayback() {
    // Pressing Play is a request to hear the song. A session that has not been
    // given an instrument yet gets the default bank here rather than being told
    // to go and find one.
    // A song of audio clips alone needs no instrument to be heard.
    if (!engine_ && hasAudioClips()) (void)rebuildEngine();
    if (!engine_ && transport_ != nullptr) (void)loadDefaultInstrument();
    if (!engine_ || !transport_) {
        status_ = "No instrument could be loaded — pick one from the plugin browser";
        emit statusChanged();
        return;
    }
    if (transport_->playing()) {
        engine_->set_playing(false);
        // Keep the callback alive for note-offs, instrument releases, meters,
        // and the next key played on the stopped transport.
        transport_->stop();
        pollMeters();
        // Stopping ends the take: a key still held is written as released.
        finishTake();
        return;
    }
    // Every pass the song is played through is a take of its own, so undo
    // takes back one pass rather than everything recorded since arming.
    takes_.clear();
    take_checkpointed_ = false;
    if (record_armed_) startAudioTake();
    // Play starts on the song as it is now, edits of this turn included.
    flushRecompile();
    std::string error;
    engine_->set_playing(true);
    if (audio_output_ && !audio_output_->start(&error)) {
        engine_->set_playing(false);
        status_ = QString("Audio start failed · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return;
    }
    meter_timer_.start();
    transport_->followSamples(engine_->sample_position() %
                                  std::max<std::uint64_t>(1, engine_->song_samples()),
                              engine_->sample_rate());
    transport_->play();
}

void AppController::rewindPlayback() {
    if (engine_) engine_->rewind();
    if (transport_) transport_->rewind();
}

void AppController::seekToBar(int bar) {
    if (bar < 0 || song_ == nullptr) return;
    // A bar starts where the meter map says, whatever the bars before it hold.
    seekToTick(static_cast<double>(song_->barStart(bar)));
}

void AppController::seekToStep(double step) {
    if (step < 0.0) return;
    seekToTick(step * static_cast<double>(PatternModel::ticks_per_step));
}

void AppController::syncTimebase() {
    if (song_ == nullptr || transport_ == nullptr) return;
    const auto& song = song_->song();
    transport_->setTimebase(song.tempo, song.meter, song.ticks_per_beat());
}

void AppController::seekToTick(double tick) {
    if (!(tick >= 0.0)) return;
    if (transport_ != nullptr) transport_->locateTick(tick);
    if (engine_ == nullptr) return;
    // The seek is placed with the clock of the song as it is now: an edit of
    // this turn is compiled first, so the engine never takes the seek under
    // one tempo map and the arrangement under another.
    flushRecompile();
    if (engine_ == nullptr || engine_->sample_rate() <= 0.0) return;
    // Rounded as the timeline's events are, so a seek to a tick lands on the
    // sample of the events on that tick and plays them.
    engine_->seek(blokkily::sample_for_tick(engine_->published_clock(), tick));
}

bool AppController::auditionKey(int key, bool held) {
    return auditionPitches(
        {{static_cast<std::int16_t>(qBound(0, key, 127)), 0.0}}, 0.9, held);
}

void AppController::setTempo(double bpm) {
    if (song_ == nullptr) return;
    // The tempo readout is dragged, so this arrives once per pointer move. A
    // tempo change is a change to when the arrangement's events fall, not to
    // the instruments playing them: the song model reports it as a structure
    // change, which reaches the running engine as a coalesced recompile.
    (void)song_->setTempoAt(bpm, transport_ != nullptr ? transport_->tick() : 0.0);
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
    // The bounce is of the song as it is now, edits of this turn included.
    flushRecompile();
    std::string error;
    const bool resume_device = audio_output_ && audio_output_->is_running();
    if (audio_output_) audio_output_->stop();
    releaseSoundingNotes();
    // Half a second of tail so the last note's release is part of the file.
    const auto tail = static_cast<std::uint64_t>(engine_->sample_rate() / 2.0);
    const auto report = bounce_song(*engine_, local_path(path).toStdString(), format,
                                    tail, &error);
    if (resume_device) {
        std::string resume_error;
        if (!audio_output_->start(&resume_error)) {
            engine_->set_playing(false);
            if (transport_) transport_->stop();
            status_ = QString("Audio restart failed · %1").arg(QString::fromStdString(resume_error));
            emit statusChanged();
        }
    }
    if (!report) {
        export_status_ = QString("Export failed · %1").arg(QString::fromStdString(error));
        emit exportStatusChanged();
        return false;
    }
    export_status_ = QString("%1 · %2 s · peak %3 dB%4")
                         .arg(QFileInfo(local_path(path)).fileName())
                         .arg(static_cast<double>(report->frames) / engine_->sample_rate(),
                              0, 'f', 1)
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
    const bool valid = rendered->channels == 2 &&
                       rendered->sample_rate == static_cast<std::uint32_t>(engine_->sample_rate()) &&
                       rendered->frames == engine_->song_samples() +
                           static_cast<std::uint64_t>(engine_->sample_rate() / 2.0) &&
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
    // Takes recorded before the first save move into the project's audio
    // folder, so the project never names a temporary file (decision 8).
    relocateRecordings(path);
    auto& song = song_->song();
    // Samples inside the folder the project is going to are named relative to
    // it, wherever the session was saved before.
    rebaseSamplers(std::filesystem::path(path.toStdString()).parent_path());
    // Ask every live processor, instrument or effect, what it wants
    // persisted before writing.
    if (engine_) (void)blokkily::capture_processor_states(*engine_, song, engine_signature_);
    blokkily::Project project;
    project.name = "Blokkily Session";
    project.song = song;
    // Files no clip plays any more are not written. The song in memory keeps
    // them, so undoing a deleted clip still finds its file.
    (void)project.song.prune_audio_files();
    std::string error;
    const bool valid = blokkily::ProjectFile::save(project, path.toStdString(), &error);
    if (valid) {
        project_path_ = path;
        song_->markSaved();
    }
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
    forgetSoundingNotes();
    takes_.clear();
    discardAudioTake();
    engine_.reset();
    // The audio the last song played is let go unless the next one plays it.
    clip_assets_.clear();
    assets_->purge_unused();
    const auto tracks = project->song.tracks.size();
    const auto patterns = project->song.patterns.size();
    // The project's folder is known before its instruments are created, so a
    // sample named relative to it is found by the first engine built for it.
    project_path_ = path;
    song_->replace(std::move(project->song));
    if (pattern_ != nullptr) pattern_->refresh();
    song_->markSaved();
    project_status_ = QString("Restored from disk");
    project_detail_ = QString("%1 pattern%2 · %3 track%4")
                          .arg(patterns).arg(patterns == 1 ? "" : "s")
                          .arg(tracks).arg(tracks == 1 ? "" : "s");
    emit projectStatusChanged();
    (void)rebuildEngine();
    return true;
}
