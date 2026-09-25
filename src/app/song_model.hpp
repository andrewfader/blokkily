#pragma once

#include "blokkily/model/song.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

// The session, as the interface sees it: named patterns, mixer tracks, and the
// clips that arrange one over the other. Every editor and the audio engine read
// this one object, so the arrangement, the mixer, and what is heard cannot
// drift apart.
class SongModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY songChanged)
    Q_PROPERTY(QVariantList patterns READ patterns NOTIFY songChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY songChanged)
    // The timeline as the arrangement draws it: one lane per track, one entry
    // per bar. A property rather than a lookup call, so the view re-reads it
    // whenever the song changes.
    Q_PROPERTY(QVariantList lanes READ lanes NOTIFY songChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY songChanged)
    Q_PROPERTY(int currentPattern READ currentPattern NOTIFY songChanged)
    Q_PROPERTY(int selectedTrack READ selectedTrack NOTIFY songChanged)
    Q_PROPERTY(int bars READ bars NOTIFY songChanged)
    // Every bar the timeline shows, laid out by the meter map (plan F-A): one
    // entry per bar, {start, ticks, numerator, denominator, x0, x1}, where
    // start and ticks are song ticks and x0..x1 is the bar's share of the
    // shown song, from 0 to 1. Clips, the ruler and automation lanes take
    // their geometry from here, so a 7/8 bar is drawn 7/8 as wide as a 4/4 one.
    Q_PROPERTY(QVariantList barLayout READ barLayout NOTIFY songChanged)
    // The tempo lane: every point of the tempo map, {at, bpm, ramp}, in tick
    // order. The first is always at tick 0.
    Q_PROPERTY(QVariantList tempoPoints READ tempoPoints NOTIFY songChanged)
    Q_PROPERTY(double masterGainDb READ masterGainDb NOTIFY mixChanged)
    Q_PROPERTY(double masterPeak READ masterPeak NOTIFY metersChanged)
    // Each track's meter, as the fraction of its bar to light. Kept apart from
    // `tracks` so thirty meter updates a second do not re-read the whole song.
    Q_PROPERTY(QVariantList meters READ meters NOTIFY metersChanged)
    // Edit history. Every change to what the song is — a step, a clip, a
    // track, a fader, the tuning — can be taken back and done again.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    // Whether the song differs from what was last saved or opened. Undoing
    // back to the saved state makes it clean again.
    Q_PROPERTY(bool dirty READ dirty NOTIFY historyChanged)
    // The tuning and scale the whole session is written in. Every editor, the
    // keyboards, and the engine read these, so nothing on screen can be in a
    // different key from the notes it is editing.
    Q_PROPERTY(QString tuningName READ tuningName NOTIFY tuningChanged)
    Q_PROPERTY(QStringList tuningNames READ tuningNames CONSTANT)
    Q_PROPERTY(int divisions READ divisions NOTIFY tuningChanged)
    Q_PROPERTY(QString scaleName READ scaleName NOTIFY tuningChanged)
    Q_PROPERTY(QStringList scaleNames READ scaleNames CONSTANT)
    Q_PROPERTY(int rootDegree READ rootDegree NOTIFY tuningChanged)
    Q_PROPERTY(QString rootName READ rootName NOTIFY tuningChanged)
    Q_PROPERTY(bool autoScale READ autoScale NOTIFY tuningChanged)
    // Audio clips (item 2.2), as the clip lane draws them: one entry per clip,
    // {id, track, file, name, startTick, endTick, fadeInTick, fadeOutTick,
    // gainDb, lengthSeconds, missing}. The ticks are song ticks through the
    // tempo map; the lane places them with barLayout.
    Q_PROPERTY(QVariantList audioClips READ audioClips NOTIFY audioClipsChanged)
    // How many of the song's audio files could not be found as the song
    // expects them.
    Q_PROPERTY(int missingAudioFiles READ missingAudioFiles NOTIFY audioClipsChanged)
    // How many tracks are armed. With none, what is played goes to the
    // selected track (decision 4).
    Q_PROPERTY(int armedCount READ armedCount NOTIFY songChanged)
    // How many inputs the audio device offers (item 3.2): the choices a
    // strip's input chip steps through. Two when the device has fewer.
    Q_PROPERTY(int audioInputChannels READ audioInputChannels NOTIFY inputChanged)
    // Effects (item 2.4). The rack is the insert chain being edited: the
    // selected track's, a return's, or the master's, as
    // {kind, bus, title, inserts: [{index, name, format, bypass}]}.
    Q_PROPERTY(QVariantMap rack READ rack NOTIFY songChanged)
    Q_PROPERTY(bool masterRacked READ masterRacked NOTIFY songChanged)
    // One row per return bus: {index, letter, name, gainDb, gainText, pan,
    // mute, inserts, racked, peakFraction}.
    Q_PROPERTY(QVariantList returns READ returns NOTIFY songChanged)
    Q_PROPERTY(QVariantList returnMeters READ returnMeters NOTIFY metersChanged)
    // The live values of the sends (per track, one entry per return:
    // {bus, letter, levelDb, levelText, pre, active}) and of the return
    // strips ({gainDb, gainText, pan, mute}). Notified by mixChanged alone, so
    // a send or return fader being dragged is not rebuilt under the pointer
    // by a change to the song's structure.
    Q_PROPERTY(QVariantList sendLevels READ sendLevels NOTIFY mixChanged)
    Q_PROPERTY(QVariantList returnMix READ returnMix NOTIFY mixChanged)
    // Automation (item 3.1; song_model_automation.cpp). The lanes of the
    // selected track, as the lane editor draws them: {index, name, kind,
    // minimum, maximum, points: [{at, value}]}, `at` in song ticks. The
    // lane the editor shows is `selectedLane` (-1 when the track has none).
    Q_PROPERTY(QVariantList automationLanes READ automationLanes NOTIFY songChanged)
    Q_PROPERTY(int selectedLane READ selectedLane NOTIFY songChanged)
    // The modes a track's automation can be in, in the order a click steps
    // through them.
    Q_PROPERTY(QStringList automationModes READ automationModes CONSTANT)

public:
    // The arrangement is laid out in bars because that is how a producer reads
    // a timeline; how long each bar is comes from the song's meter map.
    static constexpr int minimum_visible_bars = 8;

    explicit SongModel(QObject* parent = nullptr);

    QVariantList tracks() const;
    QVariantList patterns() const;
    QVariantList clips() const;
    QVariantList lanes() const;
    int trackCount() const noexcept { return static_cast<int>(song_.tracks.size()); }
    int currentPattern() const noexcept { return current_pattern_; }
    int selectedTrack() const noexcept { return selected_track_; }
    int bars() const;
    QVariantList barLayout() const;
    QVariantList tempoPoints() const;
    // Where bar `bar` starts, and how long it is, in song ticks.
    [[nodiscard]] blokkily::Tick barStart(int bar) const;
    [[nodiscard]] blokkily::Tick barTicks(int bar) const;
    // The bar a tick of the song lies in.
    [[nodiscard]] int barAt(blokkily::Tick tick) const;
    double masterGainDb() const noexcept { return song_.master_gain_db; }
    double masterPeak() const noexcept { return master_peak_; }
    QVariantList meters() const;
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    bool dirty() const noexcept { return state_id_ != saved_id_; }
    QString tuningName() const;
    QStringList tuningNames() const;
    int divisions() const noexcept { return song_.tuning.divisions(); }
    QString scaleName() const;
    QStringList scaleNames() const;
    int rootDegree() const noexcept { return song_.root_degree; }
    QString rootName() const;
    bool autoScale() const noexcept { return song_.auto_scale; }

    Q_INVOKABLE void selectTrack(int track);
    Q_INVOKABLE void selectPattern(int pattern);
    Q_INVOKABLE void addTrack();
    // A new track that arrives already carrying an instrument, as one edit and
    // one rebuild of the audio graph rather than two.
    void addTrack(const blokkily::InstrumentSlot& instrument);
    // A new pattern as long as bar `bar` of the song (the first bar when none
    // is given), so a pattern made in a 7/8 bar has fourteen steps.
    Q_INVOKABLE void addPattern(int bar = -1);
    // Makes the open pattern `steps` sixteenths long (1 to 64). Steps past the
    // new end are dropped; one step of history.
    Q_INVOKABLE bool setPatternSteps(int steps);
    // Pattern and track housekeeping. A copy of the open pattern is how a
    // variation is started; a name is how a lane is found again.
    Q_INVOKABLE void duplicatePattern();
    Q_INVOKABLE void clearPattern();
    Q_INVOKABLE bool deletePattern(int pattern);
    Q_INVOKABLE bool deleteTrack(int track);
    // While structureChanged is being emitted for a deleted track: where each
    // track before the deletion went (entry i is old track i's new index, or
    // empty for the one removed). Null at any other time, so it can never be
    // applied to a later change.
    const std::vector<std::optional<std::size_t>>* pendingTrackRemap() const {
        return track_remap_ ? &*track_remap_ : nullptr;
    }
    Q_INVOKABLE void renamePattern(int pattern, const QString& name);
    Q_INVOKABLE void renameTrack(int track, const QString& name);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // Mixer moves. These change how the song sounds without changing what it
    // plays, so they are reported separately and never rebuild the engine.
    Q_INVOKABLE void setTrackGain(int track, double decibels);
    Q_INVOKABLE void setTrackPan(int track, double pan);
    Q_INVOKABLE void toggleMute(int track);
    Q_INVOKABLE void toggleSolo(int track);
    Q_INVOKABLE void setMasterGain(double decibels);
    // Arm and input (item 2.5). Saved with the song, never a step of history,
    // and left alone by undo and redo (decision 5).
    Q_INVOKABLE void toggleArm(int track);
    Q_INVOKABLE void setArmed(int track, bool armed);
    // 0 hears every MIDI channel; 1..16 hears that one.
    Q_INVOKABLE void setInputChannel(int track, int channel);
    // Steps the channel through ALL, 1 .. 16 and round, by `step`.
    Q_INVOKABLE void cycleInputChannel(int track, int step = 1);
    int armedCount() const;
    // --- Audio input (item 3.2; song_model_audio_input.cpp) -----------------
    // Like arm and channel, saved with the song and never history.
    int audioInputChannels() const noexcept { return audio_input_channels_; }
    void setAudioInputChannels(int channels);
    // What the track records and monitors: `channels` 0 makes it a MIDI
    // track again; 1 hears device input `first` (0-based), 2 the pair from it.
    Q_INVOKABLE void setAudioInput(int track, int first, int channels);
    // Steps the strip's input chip: MIDI, IN 1-2, IN 1, IN 2, IN 3-4, ...
    Q_INVOKABLE void cycleAudioInput(int track, int step = 1);
    // Steps the monitor through automatic (while armed), on and off.
    Q_INVOKABLE void cycleMonitor(int track, int step = 1);
    // What the chips say: "MIDI", "IN 1-2", "IN 3"; "MON AUTO", "MON", "MON OFF".
    static QString audioInputText(const blokkily::TrackInput& input);
    static QString monitorText(const blokkily::TrackInput& input);
    // Recordings moved into a project's audio folder on save (decision 8):
    // every file the song, and every step of its history, names at a key of
    // `moved` now names the value. Not a step of history.
    void relocateAudioFiles(const std::map<std::filesystem::path, std::filesystem::path>& moved);
    // Arrangement editing. An empty bar takes the open pattern; a filled bar
    // is opened rather than erased — the right button takes a clip away.
    Q_INVOKABLE void placeClip(int track, int bar);
    Q_INVOKABLE bool removeClip(int track, int bar);
    Q_INVOKABLE void toggleClip(int track, int bar);
    // Opens whatever clip covers that cell: selects its track and its pattern.
    // Returns the pattern index, or -1 when the cell is empty.
    Q_INVOKABLE int openClip(int track, int bar);
    // How many times the clip covering that bar runs back-to-back. Lengthening
    // a clip is not the same as placing another of the same pattern — each
    // repetition is the next loop for probability and loop conditions.
    Q_INVOKABLE bool setClipRepeats(int track, int bar, int repeats);
    // Moves the clip covering `bar` so it starts at `newBar` on the same track.
    Q_INVOKABLE bool moveClip(int track, int bar, int newBar);
    Q_INVOKABLE bool hasClip(int track, int bar) const;
    // Timebase edits (plan F-A). Each is one step of history; a tempo dragged
    // on a readout is one step however many moves it makes.
    // Sets the tempo of the tempo point in effect at `atTick`.
    Q_INVOKABLE bool setTempoAt(double bpm, double atTick = 0.0);
    // Adds or replaces the tempo point at `atTick`; `ramp` glides from it to
    // the next point.
    Q_INVOKABLE bool setTempoPoint(double atTick, double bpm, bool ramp = false);
    Q_INVOKABLE bool removeTempoPoint(double atTick);
    // The tempo sounding at a tick of the song, ramps included.
    Q_INVOKABLE double bpmAt(double atTick) const;
    // Makes num/den take effect from `bar`. Clips and tempo points keep their
    // bar numbers (decision 9).
    Q_INVOKABLE bool setMeter(int bar, int numerator, int denominator);
    // Tuning and scale moves. They change how the session is written and read,
    // never the notes already in it: a pattern keeps the pitches it was played
    // at, so changing tuning re-reads the music rather than rewriting it.
    Q_INVOKABLE void setTuning(const QString& name);
    Q_INVOKABLE void setScale(const QString& name);
    Q_INVOKABLE void setRootDegree(int degree);
    Q_INVOKABLE void toggleAutoScale();
    // What an instrument must be told to sound a degree, and what that degree
    // is called. The keyboards and the editors both ask through here so a note
    // has one name everywhere.
    Q_INVOKABLE QString degreeName(int degree) const;
    QString pitchName(int key, double cents) const;
    [[nodiscard]] blokkily::TunedPitch pitchForDegree(int degree) const;
    // The degree a played one becomes once the scale has its say.
    [[nodiscard]] int snapDegree(int degree) const;

    // --- Audio clips (song_model_audio.cpp) ---------------------------------
    // Every edit names its clip by AudioClip::id, which survives other clips
    // being added or removed; each is one step of history, and a drag wrapped
    // in beginGesture()/endGesture() is one step however many moves it makes.
    // Ticks are song ticks; frames are counted at the clip's file rate.
    QVariantList audioClips() const;
    int missingAudioFiles() const;
    // A new track with no instrument, named for the audio it will hold.
    // Returns its index.
    Q_INVOKABLE int addAudioTrack();
    // Places the whole of `file` on `track` from `start`: one step of history
    // that adds the file (unless the song already lists it) and the clip.
    // Returns the new clip's id, or 0 when the track or file is not valid.
    blokkily::AudioClipId addAudioClip(const blokkily::AudioFileRef& file, int track,
                                       blokkily::Tick start);
    // Moves a clip so it starts at `tick` on `track` (the same length of
    // file, the same fades).
    Q_INVOKABLE bool moveAudioClip(qint64 id, double tick, int track);
    // Moves the clip's start edge to `tick`, keeping its end where it is: the
    // file under the edge is revealed or hidden, never shifted in time.
    Q_INVOKABLE bool trimAudioClipStart(qint64 id, double tick);
    // Moves the clip's end edge to `tick`, keeping its start.
    Q_INVOKABLE bool trimAudioClipEnd(qint64 id, double tick);
    // The fade-in ends at `tick`; the fade-out begins at `tick`.
    Q_INVOKABLE bool setAudioClipFadeIn(qint64 id, double tick);
    Q_INVOKABLE bool setAudioClipFadeOut(qint64 id, double tick);
    Q_INVOKABLE bool setAudioClipGain(qint64 id, double decibels);
    Q_INVOKABLE bool removeAudioClip(qint64 id);
    // One clip as audioClips lists it, or an empty map.
    Q_INVOKABLE QVariantMap audioClip(qint64 id) const;
    // Which of the song's files the controller could not load as the song
    // expects them, indexed like Song::audio_files.
    void setMissingAudio(std::vector<bool> missing);

    // --- Effects (song_model_effects.cpp) -----------------------------------
    QVariantMap rack() const;
    bool masterRacked() const;
    QVariantList returns() const;
    QVariantList returnMeters() const;
    QVariantList sendLevels() const;
    QVariantList returnMix() const;
    // The bus whose inserts the rack shows (its slot is meaningless).
    [[nodiscard]] blokkily::ProcessorAddress rackBus() const;
    // Points the rack at "track", "return" or "master" `bus`. Choosing a
    // track selects it too.
    Q_INVOKABLE void selectRack(const QString& kind, int bus);
    // Graph edits: each is one step of history and rebuilds the engine.
    bool addInsert(blokkily::BusKind kind, int bus, const blokkily::PluginSlot& plugin);
    bool addInsertToRack(const blokkily::PluginSlot& plugin);
    Q_INVOKABLE bool removeInsert(const QString& kind, int bus, int slot);
    Q_INVOKABLE int addReturn();
    Q_INVOKABLE bool removeReturn(int bus);
    // Mixer moves: live on the running engine, never a rebuild.
    Q_INVOKABLE bool setInsertBypass(const QString& kind, int bus, int slot, bool bypass);
    // A send is created by giving it a level; -96 dB or below is silent.
    Q_INVOKABLE void setSendLevel(int track, int bus, double decibels);
    Q_INVOKABLE void setSendPreFader(int track, int bus, bool pre);
    Q_INVOKABLE void setReturnGain(int bus, double decibels);
    Q_INVOKABLE void setReturnPan(int bus, double pan);
    Q_INVOKABLE void toggleReturnMute(int bus);
    void setReturnMeters(const std::vector<float>& peaks);

    blokkily::Song& song() noexcept { return song_; }
    const blokkily::Song& song() const noexcept { return song_; }
    blokkily::Pattern& editPattern();
    const blokkily::Pattern& editPattern() const;
    void replace(blokkily::Song song);
    void setInstrument(int track, const blokkily::InstrumentSlot& slot);
    // A new state for the instrument a track already has, as one step of
    // history (moves sharing `merge` are one step, as for a fader). The
    // instrument is the same processor, so this is not a structure change:
    // the caller hands the state to the running processor itself.
    bool setInstrumentState(int track, std::vector<std::byte> state, const QString& merge = {});
    // Says that the tracks, their instruments, or the clips over them were
    // changed through `song()` directly. Several such changes made together
    // reach the editors and the engine as one, rather than as one rebuild per
    // track.
    void refreshStructure();
    void setMeters(const std::vector<float>& track_peaks, float master_peak);
    // Announces a change the audio engine has to be rebuilt for.
    void notifyStructureChanged();
    // Records the song as it is, before an edit changes it, so the edit can be
    // undone. Edits that arrive as a stream — a fader or a slider being dragged
    // — pass the same `merge` key for every move, and the whole gesture is
    // then one step of history rather than one per pixel.
    void checkpoint(const QString& merge = {});
    // A pointer gesture — a stroke drawn across the piano roll, a note dragged
    // up the tracker — is one step of history however many edits it makes.
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    // The song on disk now matches the song in memory.
    void markSaved();
    // A new or freshly opened session has no history to go back through.
    void clearHistory();
    // A plugin's own window moved its parameters (item 2.6, decision 12):
    // `state` is what the instrument on `track` now holds. One gesture in the
    // plugin's window is one step of history. The engine already plays the
    // new state, so nothing is rebuilt or recompiled. False, and no step,
    // when the state did not change.
    bool commitInstrumentState(int track, std::vector<std::byte> state);

    // --- Automation (song_model_automation.cpp) -----------------------------
    QVariantList automationLanes() const;
    int selectedLane() const;
    QStringList automationModes() const;
    // "OFF", "READ", "TOUCH", "LATCH" or "WRITE" (decision 3). One step of
    // history; the running engine is recompiled, never rebuilt.
    Q_INVOKABLE bool setAutomationMode(int track, const QString& mode);
    Q_INVOKABLE void cycleAutomationMode(int track, int step = 1);
    Q_INVOKABLE QString automationMode(int track) const;
    // Whether moves of `track`'s strip and plugins are recorded while the
    // transport plays: its mode is touch, latch or write.
    [[nodiscard]] bool captures(int track) const;
    // Shows lane `lane` of the selected track in the editor.
    Q_INVOKABLE void selectLane(int lane);
    // A gain, pan or mute lane ("GAIN", "PAN", "MUTE") on `track`'s strip,
    // starting flat at the strip's value. Returns the lane's index (the
    // existing one when the strip already has that lane), or -1.
    Q_INVOKABLE int addAutomationLane(int track, const QString& kind);
    Q_INVOKABLE bool removeAutomationLane(int track, int lane);
    // Lane editing: every edit is one step of history (a drag wrapped in
    // beginGesture()/endGesture() is one step) and is recompiled into the
    // running engine. A point is kept between its neighbours' ticks, so the
    // lane stays in order, and its value inside the lane's range. Each
    // returns the point's index afterwards, or -1.
    Q_INVOKABLE int addAutomationPoint(int track, int lane, double tick, double value);
    Q_INVOKABLE int moveAutomationPoint(int track, int lane, int point, double tick,
                                        double value);
    Q_INVOKABLE bool removeAutomationPoint(int track, int lane, int point);
    // A take (item 3.1): one step of history covers everything recorded
    // between newTake() and the next newTake() - notes, lanes, and the moves
    // of the controls that were recorded. checkpointTake() records the song
    // before the take's first change, once. While `capturing` (the transport
    // plays), a strip move on a track whose mode records is part of the take
    // rather than a step of its own.
    void newTake();
    void checkpointTake();
    void setCapturing(bool capturing);

signals:
    void songChanged();
    // Raised only when the arrangement itself changed, so the audio engine is
    // rebuilt for a new clip or instrument but not for a fader move.
    void structureChanged();
    void mixChanged();
    void metersChanged();
    // The session changed tuning, scale, or root. Every projection that names
    // or draws a pitch re-reads itself from this.
    void tuningChanged();
    void historyChanged();
    // The tempo or meter map changed. Also covered by songChanged.
    void timebaseChanged();
    // Undo or redo gave these tracks back an earlier state of the instrument
    // they still carry (the same format, file and identifier). Raised after
    // the song holds the restored states and before structureChanged, so the
    // running processors can be given them while they are still the ones the
    // engine plays.
    void instrumentStatesRestored(const QList<int>& tracks);
    // The audio clips, or which of their files are missing, changed.
    void audioClipsChanged();
    // A track was armed or disarmed, or its input changed: where played notes
    // go. Also covered by songChanged.
    void inputChanged();
    // A strip control of `track` was moved from the interface: `control` 0
    // gain (dB), 1 pan, 2 mute (0/1), from `previous` to `value`. The
    // controller hands it to the engine, where automation records it.
    void stripMoved(int track, int control, double value, double previous);

private:
    [[nodiscard]] blokkily::AudioClip* findAudioClip(qint64 id);
    [[nodiscard]] const blokkily::AudioClip* findAudioClip(qint64 id) const;
    [[nodiscard]] QVariantMap audioClipRow(const blokkily::AudioClip& clip) const;
    std::vector<bool> missing_audio_;
    int audio_input_channels_ = 2;
    [[nodiscard]] bool validTrack(int track) const;
    // The insert chain of a bus, or nullptr when there is no such bus.
    std::vector<blokkily::EffectSlot>* chainAt(blokkily::BusKind kind, int bus);
    // A track's send to a return, made (silent) when `create` and missing.
    blokkily::Send* sendAt(int track, int bus, bool create);
    // A track's sends as its strip shows them: one entry per return.
    QVariantList sendsOf(std::size_t track) const;
    // The clip covering a bar on a track, if any. One definition of coverage,
    // shared by the lanes projection and by editing.
    [[nodiscard]] const blokkily::Clip* clipAt(int track, int bar) const;
    [[nodiscard]] blokkily::Clip* clipAt(int track, int bar);

    // One entry of history: the whole song, and which state of it this was,
    // so returning to the saved state is recognised as clean.
    struct Snapshot {
        blokkily::Song song;
        int current_pattern = 0;
        int selected_track = 0;
        std::uint64_t id = 0;
    };
    Snapshot snapshot() const;
    void restore(Snapshot snapshot);
    // Gives the tracks of `restored` the arm and input the same tracks have
    // in `live`, so a step of history never arms or disarms anything.
    static void keepInputs(const blokkily::Song& live, blokkily::Song& restored);

    blokkily::Song song_;
    int current_pattern_ = 0;
    int selected_track_ = 0;
    std::vector<float> peaks_;
    std::optional<std::vector<std::optional<std::size_t>>> track_remap_;
    float master_peak_ = 0.0F;
    std::vector<float> return_peaks_;
    // Which chain the effect rack edits; a track rack follows the selection.
    blokkily::BusKind rack_kind_ = blokkily::BusKind::track;
    int rack_return_ = 0;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    std::uint64_t state_id_ = 0;
    std::uint64_t saved_id_ = 0;
    std::uint64_t next_id_ = 1;
    // A strip edit that belongs to the take being recorded, or a step of its
    // own.
    void checkpointStrip(int track, const QString& merge);
    int selected_lane_ = 0;
    bool take_recorded_ = false;
    bool capturing_ = false;
    QString last_merge_;
    bool gesture_open_ = false;
    bool gesture_recorded_ = false;
    QElapsedTimer merge_clock_;
};
