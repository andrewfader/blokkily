#pragma once

#include "song_model.hpp"

#include "blokkily/model/pattern.hpp"
#include "blokkily/plugins/clap_catalog.hpp"
#include "blokkily/plugins/plugin_scan.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariant>
#include <QVariantList>

#include <optional>

// The canonical pattern, projected for every editor at once. The step grid, the
// tracker, and the piano roll all read from this one object; `steps` is the
// shared row projection the grid and the tracker each render differently.
class PatternModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int eventCount READ rowCount NOTIFY patternChanged)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY patternChanged)
    // Where the cursor is, kept apart from what the pattern holds: moving the
    // cursor is not an edit, and nothing downstream may treat it as one.
    Q_PROPERTY(int selectedStep READ selectedStep NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    // Pitch window the piano roll draws, widened to whatever the pattern uses.
    Q_PROPERTY(int lowKey READ lowKey NOTIFY patternChanged)
    Q_PROPERTY(int highKey READ highKey NOTIFY patternChanged)

public:
    static constexpr int step_count = 16;
    static constexpr blokkily::Tick ticks_per_step = 120;
    enum Role { IdRole = Qt::UserRole + 1, StepRole, KeyRole, NameRole, DurationRole,
                VelocityRole, LockRole, VoiceKeysRole };

    explicit PatternModel(SongModel* song, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList steps() const;
    QVariantMap selected() const;
    int selectedStep() const noexcept { return selected_step_; }
    int lowKey() const;
    int highKey() const;

    Q_INVOKABLE void toggleStep(int step, int key = 60);
    Q_INVOKABLE bool hasStep(int step) const;
    Q_INVOKABLE int stepDuration(int step) const;
    Q_INVOKABLE int stepKey(int step) const;
    Q_INVOKABLE void selectStep(int step);
    // What the tracker and the piano roll write. Both are editors rather than
    // read-outs, so both put notes on the same canonical pattern the step grid
    // and the keyboards do, and every projection redraws from that one change.
    // Writing over a step keeps everything else the producer set on it.
    Q_INVOKABLE void setStepKey(int step, int key);
    // Takes the note off a step without putting one back, which is what a
    // tracker's clear key and the roll's erase gesture mean. Toggling would put
    // a note back on a step the producer has just emptied.
    Q_INVOKABLE void clearStep(int step);
    // How long the step sounds, in ticks. The piano roll draws that width and
    // dragging a note's right edge writes it here, so a long note is a long
    // note in every editor and in what the engine plays.
    Q_INVOKABLE void setStepDuration(int step, int ticks);
    Q_INVOKABLE void setSelectedDuration(int ticks);
    // Velocity from the tracker's VEL column, which is an input rather than a
    // readout of the inspector.
    Q_INVOKABLE void setStepVelocity(int step, double velocity);
    // Moves a step in time and pitch together, which is what dragging a note
    // on the piano roll means. `semitones` transposes every voice, so a chord
    // stays a chord.
    Q_INVOKABLE void relocateStep(int from, int to, int semitones);
    // The selected step, as a tracker copies a row: pitch, length, velocity,
    // locks and the rest. An empty row copies as empty, so pasting it clears.
    Q_INVOKABLE void copySelected();
    Q_INVOKABLE bool pasteSelected();
    // Puts a copy of the selected step onto the next row and moves the cursor
    // there, which is how a tracker fills a phrase without leaving the keys.
    Q_INVOKABLE bool duplicateSelected();
    // Inserts an empty row at the cursor and pushes later rows down; the last
    // row falls off the end of the pattern. Delete with shift pulls later rows
    // up into the hole.
    Q_INVOKABLE bool insertStep(int step);
    Q_INVOKABLE bool deleteAndShift(int step);
    // The pitches a step sounds, in the tuning it was written in. A chord is
    // one step with several voices, so this answers with all of them.
    [[nodiscard]] std::vector<blokkily::TunedPitch> pitchesAt(int step) const;
    // What a playable surface writes: the pitch or the chord that was pressed,
    // placed on a step of the same canonical pattern every editor projects.
    void placeNote(int step, const blokkily::Note& note);
    void placeChord(int step, const blokkily::Chord& chord);

    // Step inspector edits. Each one rewrites the selected trigger in place, so
    // every projection updates from the same change.
    Q_INVOKABLE void transposeSelected(int semitones);
    Q_INVOKABLE void setSelectedVelocity(double velocity);
    Q_INVOKABLE void setSelectedProbability(double probability);
    Q_INVOKABLE void setSelectedRatchets(int ratchets);
    Q_INVOKABLE void setSelectedMicroOffset(int ticks);
    Q_INVOKABLE void setSelectedPlayOnLoop(int loop);
    Q_INVOKABLE void setSelectedLock(int index, double value, bool modulation);
    Q_INVOKABLE void clearSelectedLock();

    const blokkily::Pattern& pattern() const;
    void replace(blokkily::Pattern pattern);
    // Re-reads the song after the current pattern or the whole session changed.
    void refresh();

signals:
    // What the editors must redraw: an edit, a reload, or the song opening a
    // different pattern.
    void patternChanged();
    // What the song actually plays. Only a real edit raises this, so looking
    // at another pattern or moving a fader never reaches the audio engine.
    void contentChanged();
    void selectionChanged();

private:
    const blokkily::Trigger* triggerAt(int step) const;
    // `merge` names a control whose moves arrive as a stream, so dragging it
    // is one step of history.
    void mutate(int step, const std::function<void(blokkily::Trigger&)>& edit,
                const QString& merge = {});

    blokkily::Pattern& pattern();
    SongModel* song_ = nullptr;
    int selected_step_ = -1;
    bool has_clipboard_ = false;
    std::optional<blokkily::Trigger> clipboard_;
};

// Drives the playhead every editor shares. Playback advances from a monotonic
// clock while playing; `locate` moves it deterministically without one.
class Transport final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY changed)
    Q_PROPERTY(int step READ step NOTIFY changed)
    Q_PROPERTY(double stepFraction READ stepFraction NOTIFY changed)
    Q_PROPERTY(QString position READ position NOTIFY changed)
    Q_PROPERTY(int bar READ bar NOTIFY changed)
    Q_PROPERTY(int bars READ bars NOTIFY changed)

public:
    // One bar is sixteen steps: the step grid is a bar of sixteenth notes.
    static constexpr int steps_per_bar = PatternModel::step_count;

    explicit Transport(QObject* parent = nullptr);
    bool playing() const noexcept { return playing_; }
    double bpm() const noexcept { return bpm_; }
    // Where the editors' shared playhead sits inside the bar they show.
    int step() const noexcept { return static_cast<int>(step_position_) % steps_per_bar; }
    double stepFraction() const noexcept { return step_position_; }
    // Where the song playhead sits in the arrangement.
    int bar() const noexcept { return static_cast<int>(step_position_) / steps_per_bar; }
    int bars() const noexcept { return bars_; }
    QString position() const;
    void setSongBars(int bars);
    // Slaves the playhead to the audio engine, so what is drawn is where the
    // song actually is rather than a second clock running alongside it.
    void followSamples(std::uint64_t samples, double sample_rate);
    void releaseFollowing();

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void rewind();
    Q_INVOKABLE void locate(double step);
    void setBpm(double bpm);

signals:
    void changed();

private:
    void tick();

    QTimer timer_;
    QElapsedTimer clock_;
    bool playing_ = false;
    bool following_ = false;
    int bars_ = 8;
    double bpm_ = 120.0;
    double step_position_ = 0.0;
};

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

public:
    explicit AppController(SongModel* song = nullptr, PatternModel* pattern = nullptr,
                           Transport* transport = nullptr, QObject* parent = nullptr,
                           std::unique_ptr<blokkily::RtAudioOutput> output = {});
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
    // Locates both playheads on an absolute step of the song — bar * 16 +
    // step — so a Ctrl-click on the step grid jumps into that column of the
    // bar the song is already in.
    Q_INVOKABLE void seekToStep(double step);
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
    // Bounces the arrangement through the engine the speakers hear.
    Q_INVOKABLE bool exportAudioFile(const QString& path, const QString& depth = "FLOAT32");
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

private:
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

    // Instantiates one track's instrument through the format adapters.
    std::unique_ptr<blokkily::PluginInstance> createInstrument(
        const blokkily::InstrumentSlot& slot, std::string* error) const;
    // Rebuilds the audio graph for the current arrangement. Mixer moves do not
    // come through here: they are applied to the running engine.
    bool rebuildEngine();
    // Hands an edited arrangement to the engine that is already playing it.
    // Returns false when the change is one the running graph cannot express —
    // a track added, an instrument swapped — and the caller must rebuild.
    bool refreshArrangement();
    // Whether the live engine was built from the instruments the song now
    // names. Only the identity of each slot counts: a plugin's own state moves
    // as it is played and is not a reason to rebuild.
    bool builtFromCurrentInstruments() const;
    void applyMix();
    // Drops what the keyboard was holding, because the engine that heard the
    // press is about to be replaced.
    void forgetSoundingNotes();
    // Releases what the keyboard is holding, on the track that sounded it.
    void releaseSoundingNotes();
    void pollMeters();
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
    std::unique_ptr<blokkily::SongEngine> engine_;
    // The instrument slots the live engine was built from, so an edit that
    // leaves them alone is handed to the running engine instead of rebuilding.
    std::vector<blokkily::InstrumentSlot> engine_slots_;
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
};
