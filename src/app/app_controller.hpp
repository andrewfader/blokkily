#pragma once

#include "pattern_model.hpp"
#include "song_model.hpp"
#include "transport.hpp"

#include "blokkily/model/pattern.hpp"
#include "blokkily/plugins/clap_catalog.hpp"
#include "blokkily/plugins/plugin_scan.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/sequencer/take.hpp"

#include "engine_graph.hpp"
#include "recompile_coalescer.hpp"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantList>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <thread>
#include <optional>
#include <utility>
#include <vector>

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList browserPlugins READ browserPlugins NOTIFY browserChanged)
    Q_PROPERTY(QString browserFilter READ browserFilter WRITE setBrowserFilter
                   NOTIFY browserChanged)
    Q_PROPERTY(QString soundfontStatus READ soundfontStatus NOTIFY soundfontStatusChanged)
    Q_PROPERTY(QString projectStatus READ projectStatus NOTIFY projectStatusChanged)
    Q_PROPERTY(QString projectDetail READ projectDetail NOTIFY projectStatusChanged)
    Q_PROPERTY(QString activeInstrument READ activeInstrument NOTIFY activeInstrumentChanged)
    Q_PROPERTY(bool audioReady READ audioReady NOTIFY activeInstrumentChanged)
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportStatusChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // The file the session was last saved to or opened from, so Save writes
    // back to it instead of asking again. Empty for a session never saved.
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectStatusChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectStatusChanged)
    // The rate the engine renders at: the one the audio device negotiated.
    Q_PROPERTY(double sampleRate READ sampleRate NOTIFY activeInstrumentChanged)
    // MIDI input: the ports the system offers, the one being played, and what
    // it last delivered, so a producer can see a keyboard is reaching the app.
    Q_PROPERTY(QStringList midiPorts READ midiPorts NOTIFY midiChanged)
    Q_PROPERTY(QString midiPort READ midiPort NOTIFY midiChanged)
    Q_PROPERTY(QString midiActivity READ midiActivity NOTIFY midiActivityChanged)
    Q_PROPERTY(int midiNotes READ midiNotes NOTIFY midiActivityChanged)
    // Armed, a running song records what is played into the pattern under the
    // playhead on the selected track.
    Q_PROPERTY(bool recordArmed READ recordArmed NOTIFY recordChanged)
    // Audio clips (app_controller_audio.cpp). Imports decode off the control
    // thread; `importsPending` counts the ones still decoding, and
    // `importStatus` says what the last one did. `assetRevision` moves when
    // the decoded audio behind the clips changes, so waveforms redraw.
    Q_PROPERTY(int importsPending READ importsPending NOTIFY importChanged)
    Q_PROPERTY(QString importStatus READ importStatus NOTIFY importChanged)
    Q_PROPERTY(qint64 lastImportedClip READ lastImportedClip NOTIFY importChanged)
    Q_PROPERTY(int assetRevision READ assetRevision NOTIFY assetsChanged)

public:
    explicit AppController(SongModel* song = nullptr, PatternModel* pattern = nullptr,
                           Transport* transport = nullptr, QObject* parent = nullptr,
                           std::unique_ptr<blokkily::RtAudioOutput> output = {},
                           std::unique_ptr<blokkily::MidiInput> midi = {});
    ~AppController() override;
    QString status() const { return status_; }
    QVariantList plugins() const { return plugins_; }
    // What the browser lists right now: every instrument the filter admits,
    // closest match first, each entry carrying `source` — its place in the
    // unfiltered list — so choosing one out of a filtered browser loads the
    // instrument that was clicked and not the one that happens to sit at that
    // row of the full list.
    QVariantList browserPlugins() const;
    QString browserFilter() const { return browser_filter_; }
    // A few letters of a name, the way an instrument is found in a list
    // hundreds long.
    Q_INVOKABLE void setBrowserFilter(const QString& query);
    QString soundfontStatus() const { return soundfont_status_; }
    QString projectStatus() const { return project_status_; }
    QString projectDetail() const { return project_detail_; }
    QString activeInstrument() const;
    QString exportStatus() const { return export_status_; }
    bool audioReady() const noexcept { return engine_ != nullptr; }
    bool scanning() const noexcept { return scanning_; }
    QString projectPath() const { return project_path_; }
    QString projectName() const;
    double sampleRate() const noexcept { return engine_ ? engine_->sample_rate() : 0.0; }
    QStringList midiPorts() const { return midi_ports_; }
    QString midiPort() const;
    QString midiActivity() const { return midi_activity_; }
    int midiNotes() const noexcept { return static_cast<int>(midi_notes_); }
    bool recordArmed() const noexcept { return record_armed_; }
    // Asks the system again which MIDI inputs exist, for a keyboard plugged in
    // after launch.
    Q_INVOKABLE void refreshMidiPorts();
    // Plays from the port at `index` of midiPorts; -1 closes the input.
    Q_INVOKABLE bool selectMidiPort(int index);
    Q_INVOKABLE void toggleRecord();
    blokkily::MidiInput& midiInput() noexcept { return *midi_input_; }
    // A track added from the interface is given something to play: the bank
    // the session already uses, or the machine's General MIDI bank.
    Q_INVOKABLE void addTrack();
    // Writes to the file the session came from. False when there is none yet,
    // and the interface asks where to save instead.
    Q_INVOKABLE bool saveProjectInPlace();
    // A blank session: one empty pattern on one track with the default bank.
    Q_INVOKABLE void newProject();
    // Lets go of the notes a surface is holding, for the release of a key that
    // was held down.
    Q_INVOKABLE void releaseAudition();
    bool auditioning() const noexcept { return !sounding_.empty(); }
    // Discovery of the installed plugins. Both return immediately: candidates
    // are described by a helper process, one at a time, and results reach the
    // browser as they arrive, so no plugin can stall the interface.
    Q_INVOKABLE void scanPlugins();
    // The producer asking again means "try the ones that failed too", so an
    // explicit rescan forgets what the last scan learned.
    Q_INVOKABLE void rescanPlugins();
    // Loads the chosen instrument onto the selected mixer track.
    Q_INVOKABLE bool selectInstrument(int index);
    // Puts a General MIDI SoundFont on every track that has none, so the app
    // makes sound the moment it opens rather than after a plugin hunt. Reports
    // whether a bank was found; a host with no SoundFonts installed is left as
    // it was and says so.
    Q_INVOKABLE bool loadDefaultInstrument();
    // The same default, over search roots the caller names. Verification uses
    // it so the gate proves the behaviour against its own fixture rather than
    // against whatever banks happen to be installed on the machine.
    bool loadDefaultInstrument(const std::vector<std::filesystem::path>& roots);
    // Sounds whatever a step holds, so a note written in the tracker or drawn
    // in the piano roll is heard as it is entered, on a stopped song too.
    Q_INVOKABLE bool auditionStep(int step);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void rewindPlayback();
    // Locates the audio engine and the drawn playhead on a bar of the
    // arrangement, which is what clicking the timeline ruler means.
    Q_INVOKABLE void seekToBar(int bar);
    // Locates both playheads on an absolute sixteenth step of the song (a
    // step is 120 ticks), so a Ctrl-click on the step grid jumps into that
    // column of the bar the song is already in.
    Q_INVOKABLE void seekToStep(double step);
    // Locates both playheads on a tick of the song. The engine is sent to the
    // sample the song's tempo map places that tick at.
    void seekToTick(double tick);
    // Sounds one twelve-tone key through the selected track, so a press on the
    // piano roll's keyboard gutter is heard the way a key of the surface is.
    Q_INVOKABLE bool auditionKey(int key, bool held = false);
    Q_INVOKABLE bool saveProjectFile(const QString& path);
    Q_INVOKABLE bool loadProjectFile(const QString& path);
    // Sounds pitches straight through the running engine, so a keyboard is
    // audible on a stopped song. Returns false when no engine is live.
    // `held` notes last until releaseAudition(), with a generous limit so a
    // release that never arrives cannot leave a voice sounding for ever;
    // otherwise they are let go after a short beat.
    bool auditionPitches(const std::vector<blokkily::TunedPitch>& pitches, double velocity,
                         bool held = false);
    // --- Audio clips (app_controller_audio.cpp) -----------------------------
    int importsPending() const noexcept { return imports_pending_; }
    QString importStatus() const { return import_status_; }
    qint64 lastImportedClip() const noexcept { return last_imported_clip_; }
    int assetRevision() const noexcept { return asset_revision_; }
    // A new track with no instrument, for audio. Returns its index.
    Q_INVOKABLE int addAudioTrack();
    // Imports an audio file as a clip on `track` from `tick`. The file is
    // referenced where it is (decision 8) and decoded off the control thread
    // at the engine's rate; the clip is placed, as one step of history, only
    // once the decode has finished. Returns false when nothing was started.
    Q_INVOKABLE bool importAudio(const QString& path, int track, double tick);
    // The import the file dialog makes: onto the selected track, at the start
    // of the bar the playhead is in.
    Q_INVOKABLE bool importAudioFile(const QString& path);
    // The overview of a clip for its waveform: `buckets` min, max pairs over
    // the stretch of file it plays, relative to the file's loudest sample and
    // scaled by the clip's gain and fades. Empty for a clip whose file is
    // missing.
    Q_INVOKABLE QVariantList clipPeaks(qint64 id, int buckets) const;
    // The decoded audio store clips and samplers share.
    blokkily::AudioAssetCache& assetCache() noexcept { return assets_; }
    // Bounces the arrangement through the engine the speakers hear.
    Q_INVOKABLE bool exportAudioFile(const QString& path, const QString& depth = "FLOAT32");
    // Sets the tempo in effect at the playhead: the tempo readout's edit. The
    // tempo is the song's, so this is an undoable song edit, and the running
    // engine keeps the playhead on its bar.
    Q_INVOKABLE void setTempo(double bpm);
    // Scans the given search paths out of process; `scanFinished` reports the
    // empty queue. `forget_cache` retries candidates that previously failed.
    void beginScan(const std::vector<std::filesystem::path>& clap_paths,
                   const std::vector<std::filesystem::path>& vst3_paths,
                   const std::vector<std::filesystem::path>& soundfont_paths,
                   bool forget_cache = false);
    // The in-process scan, kept for fixtures the verification drives directly:
    // a known-good plugin is loaded here rather than through a helper process.
    void scanPluginPaths(const std::vector<std::filesystem::path>& clap_paths,
                         const std::vector<std::filesystem::path>& vst3_paths,
                         const std::vector<std::filesystem::path>& soundfont_paths);
    bool scanClapFile(const QString& path);
    bool verifyClap(const QString& path);
    bool verifyVst3(const QString& path);
    bool verifyParameterLocks(const QString& clap_path);
    bool verifySoundFont(const QString& path);
    bool verifyMixer();
    bool verifyBounce(const QString& path);
    bool saveProject(const QString& path);
    bool loadProject(const QString& path);
    // Every mixer track that carries an instrument.
    std::vector<blokkily::InstrumentSlot> instruments() const;
    blokkily::SongEngine* engine() const noexcept { return engine_.get(); }
    // Asks for the arrangement to be recompiled into the running engine. Any
    // number of requests in one turn of the event loop become one recompile of
    // the song as it is when that turn ends, retried while the engine has no
    // free arrangement slot.
    void requestRecompile();
    // Runs an owed recompile now, for a caller about to depend on the engine
    // playing the current song.
    void flushRecompile();
    // How many recompiles have run, and how many graphs have been built.
    int recompileCount() const noexcept { return recompiler_.recompiles(); }
    int rebuildCount() const noexcept { return rebuilds_; }
    // How many processors the last rebuild carried over from the engine before.
    int adoptedProcessors() const noexcept { return adopted_processors_; }

signals:
    void statusChanged();
    void pluginsChanged();
    void browserChanged();
    void soundfontStatusChanged();
    void projectStatusChanged();
    void activeInstrumentChanged();
    void exportStatusChanged();
    void scanningChanged();
    void scanFinished();
    void midiChanged();
    void midiActivityChanged();
    void recordChanged();
    void importChanged();
    void assetsChanged();

private:
    struct ImportResult;
    // Decodes the audio every clip of the song plays, at `rate`, through the
    // asset store; flags the files that are missing on the song model.
    blokkily::AudioAssets clipAssets(double rate);
    // Back on the control thread: places a finished import.
    void finishImport(int ticket, const ImportResult& result);
    // Whether the song needs an engine even without an instrument.
    bool hasAudioClips() const;
    // Drives the scan queue: one helper process per candidate, each with a
    // deadline, results appended to the browser as they land.
    void scanNext();
    void startScanner(const blokkily::ScanCandidate& candidate);
    void completeCandidate(bool ok, const QString& failure,
                           std::vector<blokkily::ScanRecord> records);
    void finishScan();
    void appendRecords(const std::vector<blokkily::ScanRecord>& records);
    void reportScanProgress();
    blokkily::ScanCacheEntry* cachedScan(const blokkily::ScanCandidate& candidate);
    void loadScanCache();
    void saveScanCache() const;
    QString scanHelperPath() const;
    QString scanCachePath() const;

    // What processors are created with: the project folder and asset store.
    blokkily::ProcessorContext processorContext() const;
    // Rebuilds the audio graph for the current arrangement, adopting every
    // processor whose identity is unchanged and creating only what is new.
    // `remap` says where each old track went, when tracks were removed. Mixer
    // moves do not come through here: they are applied to the running engine.
    bool rebuildEngine(const blokkily::TrackRemap* remap = nullptr);
    // Hands an edited arrangement to the engine that is already playing it.
    // Returns false when the change is one the running graph cannot express —
    // a track added, an instrument swapped — and the caller must rebuild.
    bool refreshArrangement(std::string* error = nullptr);
    // Whether the live engine was built from the graph the song now names.
    // Only the identity of each processor counts: a plugin's own state moves
    // as it is played and is not a reason to rebuild.
    bool builtFromCurrentGraph() const;
    void applyMix();
    // Drops what the keyboard was holding, because the engine that heard the
    // press is about to be replaced.
    void forgetSoundingNotes();
    // Releases what the keyboard is holding, on the track that sounded it.
    void releaseSoundingNotes();
    void pollMeters();
    // Tells the MIDI input how each key sounds in the song's tuning and scale.
    void updateKeyMap();
    // Starts the device if it is idle, so a key played on a stopped song is
    // heard. False when there is no engine or the device will not start.
    bool ensureAudioRunning();
    // Writes what the engine captured since the last call into the song.
    void drainTake();
    // Ends the take: whatever is still held is written as released now.
    void finishTake();
    void commitTake(std::vector<std::pair<std::size_t, blokkily::PlayedNote>> notes);
    // Hands the song's tempo and meter maps to the transport.
    void syncTimebase();
    void assignInstrument(int track, const blokkily::InstrumentSlot& slot,
                          const QString& label);
    // The General MIDI bank this machine offers, preferring the familiar ones.
    static std::filesystem::path findDefaultBank(const std::vector<std::filesystem::path>& roots);
    // A SoundFont slot on `bank`, set to the preset a track of that name wants.
    static blokkily::InstrumentSlot defaultSlot(const std::filesystem::path& bank,
                                                const std::string& track_name);

    QString status_ = "Ready — CLAP native";
    QVariantList plugins_;
    QString browser_filter_;
    QString soundfont_status_ = "No instrument loaded";
    QString project_status_ = "Not saved";
    QString project_detail_ = "No project on disk";
    QString export_status_ = "Not exported";
    QString project_path_;
    SongModel* song_ = nullptr;
    PatternModel* pattern_ = nullptr;
    Transport* transport_ = nullptr;
    // Declared before the engine and the device so it outlives both: the render
    // callback reads its queue until the device has stopped.
    std::unique_ptr<blokkily::MidiInput> midi_input_;
    QStringList midi_ports_;
    QString midi_activity_ = QStringLiteral("—");
    std::uint64_t midi_notes_ = 0;
    bool record_armed_ = false;
    // The take being recorded, one recorder per track so a key held on one
    // track is never paired with a release on another. The first note written
    // checkpoints history, so a take is one step of undo.
    std::vector<blokkily::TakeRecorder> takes_;
    bool take_checkpointed_ = false;
    std::unique_ptr<blokkily::SongEngine> engine_;
    // The graph the live engine was built from, so an edit that leaves it
    // alone is handed to the running engine instead of rebuilding.
    blokkily::GraphSignature engine_signature_;
    blokkily::RecompileCoalescer recompiler_;
    int rebuilds_ = 0;
    int adopted_processors_ = 0;
    std::unique_ptr<blokkily::RtAudioOutput> audio_output_;
    QTimer meter_timer_;
    // Notes the keyboard is holding, released together when their time is up
    // so a press cannot leave a voice sounding for ever.
    QTimer audition_timer_;
    std::vector<blokkily::TunedPitch> sounding_;
    int audition_track_ = 0;
    // Scan state. The queue is what is left to describe; the cache is what
    // earlier scans learned, including which plugins must not be tried again.
    std::vector<blokkily::ScanCandidate> scan_queue_;
    std::vector<blokkily::ScanCacheEntry> scan_cache_;
    std::size_t scan_index_ = 0;
    int scan_failures_ = 0;
    bool scanning_ = false;
    bool scan_expired_ = false;
    bool scan_cache_loaded_ = false;
    std::unique_ptr<QProcess> scanner_;
    QTimer scan_deadline_;
    // Decoded audio shared by clips (and processors that play files). Mutable
    // because handing out the store is not a change to the controller.
    mutable blokkily::AudioAssetCache assets_;
    // What the engine was last given for the song's clips, indexed like
    // Song::audio_files: the owners of the audio the waveforms draw.
    blokkily::AudioAssets clip_assets_;
    int asset_revision_ = 0;
    // Imports decoding off the control thread, by ticket. Joined when they
    // report back, and in the destructor, so none outlives the controller.
    std::map<int, std::thread> imports_;
    int next_import_ = 1;
    int imports_pending_ = 0;
    QString import_status_ = QStringLiteral("No audio imported");
    qint64 last_imported_clip_ = 0;
};
