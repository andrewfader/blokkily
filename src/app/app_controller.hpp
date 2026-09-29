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
#include "blokkily/audio/clip_warp.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/take_writer.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/instruments/sampler_program.hpp"
#include "blokkily/sequencer/take.hpp"
#include "blokkily/sequencer/automation_take.hpp"

#include "editor_gestures.hpp"
#include "engine_graph.hpp"
#include "plugin_windows.hpp"
#include "recompile_coalescer.hpp"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList browserPlugins READ browserPlugins NOTIFY browserChanged)
    Q_PROPERTY(QString browserFilter READ browserFilter WRITE setBrowserFilter
                   NOTIFY browserChanged)
    // Which kind the browser lists: "instrument" or "effect" (item 2.4).
    // Choosing an effect inserts it on the rack's bus.
    Q_PROPERTY(QString browserKind READ browserKind WRITE setBrowserKind NOTIFY browserChanged)
    // How many entries of that kind there are, before the filter.
    Q_PROPERTY(int browserTotal READ browserTotal NOTIFY browserChanged)
    // How late the speakers hear the song, in samples: the plugin delay
    // compensation every path is aligned to.
    Q_PROPERTY(int outputLatency READ outputLatency NOTIFY activeInstrumentChanged)
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
    // The built-in sampler on the selected track, as its panel shows it:
    // `active` (the track carries one), `mode` ("keyed" or "kit"), `zones`,
    // the selected `zone` and its `sample`, `missing`, `rootKey`, `lowKey`,
    // `highKey`, `loop` ("off", "forward", "ping_pong"), `attack`, `decay`,
    // `sustain`, `release` (seconds, level), `trackPitch` and `oneShot`.
    Q_PROPERTY(QVariantMap sampler READ sampler NOTIFY samplerChanged)
    // Audio clips (app_controller_audio.cpp). Imports decode off the control
    // thread; `importsPending` counts the ones still decoding, and
    // `importStatus` says what the last one did. `assetRevision` moves when
    // the decoded audio behind the clips changes, so waveforms redraw.
    Q_PROPERTY(int importsPending READ importsPending NOTIFY importChanged)
    Q_PROPERTY(QString importStatus READ importStatus NOTIFY importChanged)
    Q_PROPERTY(qint64 lastImportedClip READ lastImportedClip NOTIFY importChanged)
    Q_PROPERTY(int assetRevision READ assetRevision NOTIFY assetsChanged)
    // Clip warp (item 3.6): renditions still rendering, and what the warp
    // worker last reported.
    Q_PROPERTY(int warpRendersPending READ warpRendersPending NOTIFY warpChanged)
    Q_PROPERTY(QString warpStatus READ warpStatus NOTIFY warpChanged)
    // Plugin editors (item 2.6): the tracks whose instrument has its own
    // window open, what the last attempt to open one said, and the parameter
    // the last knob turned in an editor moved, as "LEVEL 0.60".
    Q_PROPERTY(QVariantList openEditors READ openEditors NOTIFY editorsChanged)
    Q_PROPERTY(QString editorStatus READ editorStatus NOTIFY editorsChanged)
    Q_PROPERTY(QString editorReadout READ editorReadout NOTIFY editorReadoutChanged)
    // Audio input (item 3.2): what the last audio take did, as "2 audio takes
    // recorded" or with the frames the capture ring had to drop.
    Q_PROPERTY(QString audioTakeStatus READ audioTakeStatus NOTIFY audioTakeChanged)
    // A count-in is playing (item 3.7): Play was pressed with recording armed
    // and a count-in set, and the song has not started yet.
    Q_PROPERTY(bool countingIn READ countingIn NOTIFY countInChanged)
    // The scene launcher (phase 2, wave 6.1; app_controller_launcher.cpp):
    // one entry per track, {playing, queued, stopping, scene, queuedScene},
    // as of the last block the engine rendered; and whether what is launched
    // is being printed into the arrangement.
    Q_PROPERTY(QVariantList launcherState READ launcherState NOTIFY launcherChanged)
    Q_PROPERTY(bool launcherRecording READ launcherRecording NOTIFY launcherChanged)

public:
    explicit AppController(SongModel* song = nullptr, PatternModel* pattern = nullptr,
                           Transport* transport = nullptr, QObject* parent = nullptr,
                           std::unique_ptr<blokkily::RtAudioOutput> output = {},
                           std::unique_ptr<blokkily::MidiInput> midi = {});
    ~AppController() override;
    QString status() const { return status_; }
    // Everything the browser can offer: what the scan found, then what the
    // application provides itself (the built-in effects), each entry with
    // its format and kind.
    QVariantList plugins() const;
    QString browserKind() const { return browser_kind_; }
    Q_INVOKABLE void setBrowserKind(const QString& kind);
    int browserTotal() const;
    int outputLatency() const noexcept {
        return engine_ ? static_cast<int>(engine_->output_latency()) : 0;
    }
    // Inserts the effect at `index` of plugins() on the rack's bus (the
    // selected track, a return, or the master). False for an instrument.
    Q_INVOKABLE bool addEffect(int index);
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
    // A strip control of `track` ("gain", "pan" or "mute") taken hold of or
    // let go in the interface (item 3.1). While held, a move of it overrides
    // its automation lane in touch mode and is recorded as one pass.
    Q_INVOKABLE void touchStrip(int track, const QString& control, bool touching);
    // Whether a strip control is being held.
    Q_INVOKABLE bool stripHeld(int track, const QString& control) const;
    // Whether a take is being recorded against the running transport: armed
    // and playing. The on-screen surfaces perform into the take then, rather
    // than writing onto the selected step.
    Q_INVOKABLE bool recordingLive() const noexcept;
    // Plays pitches into every track the surfaces are routed to - the armed
    // tracks, or the selected one with none armed - as input the take records
    // where it was heard. Only while recordingLive(); false otherwise, and the
    // caller writes steps the way it does on a stopped song. Each note is
    // released on the tracks it went down on, by releaseAudition() or, for a
    // press that is not held, after a short beat.
    bool performPitches(const std::vector<blokkily::TunedPitch>& pitches, double velocity,
                        bool held);
    // A twelve-tone key performed that way: the tracker's note keys.
    Q_INVOKABLE bool performKey(int key, bool held = true);
    // Lets go of what the surfaces performed, on the tracks it went down on.
    Q_INVOKABLE void releasePerformed();
    // Where a MIDI keyboard's channels are played, from the song's arm and
    // input settings and the selected track.
    void updateInputRoutes();
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
    // Locates both playheads on step `step` of the open pattern where it is
    // playing now: the pass of the selected track's clip of that pattern under
    // the playhead, or the bar the playhead is in when no such clip is there.
    // This is what a Ctrl-click on the step grid means in any meter and for a
    // pattern of any length.
    Q_INVOKABLE void seekToPatternStep(int step);
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
    blokkily::AudioAssetCache& assetCache() noexcept { return *assets_; }
    // Bounces the arrangement through the engine the speakers hear. The
    // metronome is left out unless `withClick` asks for it (item 3.7).
    Q_INVOKABLE bool exportAudioFile(const QString& path, const QString& depth = "FLOAT32",
                                     bool withClick = false);
    // --- Clip warp (app_controller_warp.cpp) ---------------------------------
    int warpRendersPending() const noexcept { return warp_pending_; }
    QString warpStatus() const { return warp_status_; }
    // Switches a clip's tempo following on or off. Switched on for a clip
    // with no source tempo yet, the tempo is detected from its audio first
    // (120 when no steady beat is found), in the same step of history.
    Q_INVOKABLE bool setClipFollowTempo(qint64 id, bool follow);
    // The tempo of the clip's audio, detected from its onsets, or 0.
    Q_INVOKABLE double detectClipTempo(qint64 id);
    // Blocks until every rendition the song needs has been rendered and
    // handed to the engine. An export does this first, so it never bounces a
    // clip that is still silent while it renders.
    Q_INVOKABLE void waitForWarpRenders();
    // Renditions the warp worker has finished since the controller started.
    std::uint64_t warpRendered() const noexcept { return warp_ ? warp_->rendered() : 0; }
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
    // A new state for the instrument on `track`, as one step of history, handed
    // to the running processor without rebuilding the graph or recompiling
    // the arrangement: the engine, the playhead and every other instrument
    // carry on. A processor that takes state while it runs (the sampler) gets
    // it through update_processor_state; any other is given it while the
    // device is briefly stopped. False for a track with no instrument.
    bool setInstrumentState(int track, std::vector<std::byte> state, const QString& merge = {});
    Q_INVOKABLE bool setInstrumentState(int track, const QByteArray& state);
    // The sampler panel's edits (app_controller_sampler.cpp). Each rewrites the
    // program on the selected track through setInstrumentState, so it is heard
    // at the next note, and is one step of history; a control dragged or
    // stepped is one step however many moves it makes.
    QVariantMap sampler() const;
    Q_INVOKABLE bool selectSamplerZone(int zone);
    Q_INVOKABLE bool setSamplerRootKey(int key);
    Q_INVOKABLE bool setSamplerKeyRange(int low, int high);
    Q_INVOKABLE bool setSamplerLoop(const QString& mode);
    // `stage` is "attack", "decay", "release" (seconds) or "sustain" (0..1).
    Q_INVOKABLE bool setSamplerEnvelope(const QString& stage, double value);
    // "keyed": zones follow the key's pitch and stop at note-off. "kit": each
    // zone is a pad that plays its file at its own pitch to the end.
    Q_INVOKABLE bool setSamplerMode(const QString& mode);
    // A keyed sampler plays the file across the keyboard from the root key the
    // file names; a kit gains a pad for it on the next free key from C2.
    Q_INVOKABLE bool loadSamplerSample(const QString& path);
    // Chops the selected zone's whole file into `count` equal pads on
    // consecutive keys from C2 (36); the sampler becomes a kit.
    Q_INVOKABLE bool sliceSampler(int count);
    // How many recompiles have run, and how many graphs have been built.
    int recompileCount() const noexcept { return recompiler_.recompiles(); }
    int rebuildCount() const noexcept { return rebuilds_; }
    // How many processors the last rebuild carried over from the engine before.
    int adoptedProcessors() const noexcept { return adopted_processors_; }

    // --- Plugin editors (item 2.6; app_controller_editors.cpp) ----------------
    QVariantList openEditors() const;
    QString editorStatus() const { return editor_status_; }
    QString editorReadout() const { return editor_readout_; }
    // Opens the editor of the instrument on `track` in its own window. False,
    // with editorStatus saying why, when there is no instrument, it has no
    // editor, or this display cannot show it.
    Q_INVOKABLE bool openEditor(int track);
    Q_INVOKABLE void closeEditor(int track);
    Q_INVOKABLE bool toggleEditor(int track);
    Q_INVOKABLE bool editorOpen(int track) const;
    Q_INVOKABLE bool toggleInsertEditor(const QString& kind, int bus, int slot);
    Q_INVOKABLE bool insertEditorOpen(const QString& kind, int bus, int slot) const;
    Q_INVOKABLE QVariantMap parameterRange(const QString& kind, int bus, int slot, int parameter) const;
    Q_INVOKABLE QVariantList insertParameters(const QString& kind, int bus, int slot) const;
    Q_INVOKABLE bool automateInsert(const QString& kind, int bus, int slot, int parameter);
    // Modulation (phase 2, wave 5.1): every parameter a modulator can aim at
    // on the selected track - its instrument's, then each insert's - as
    // {kind, bus, slot, parameter, label}, read from the processors the engine
    // runs; and one parameter's name, or "P<n>" when the engine has none.
    Q_INVOKABLE QVariantList modulationTargets() const;
    Q_INVOKABLE QString parameterName(const QString& kind, int bus, int slot, int parameter) const;
    Q_INVOKABLE bool collectAudio();
    Q_PROPERTY(bool outputRecordingArmed READ outputRecordingArmed NOTIFY outputRecordingChanged)
    bool outputRecordingArmed() const { return output_source_.has_value(); }
    Q_INVOKABLE void toggleRackOutputRecording();
    Q_INVOKABLE bool bounceRackInPlace(bool muteSource = false);
    void startOutputTake();
    void finishOutputTake();
    bool openProcessorEditor(blokkily::ProcessorAddress where);
    // Whether the instrument on `track` has an editor to open.
    Q_INVOKABLE bool hasEditor(int track) const;
    PluginWindows& pluginWindows();
    // Moves the plugins' own parameter edits out of the engine: each gesture
    // finished in a plugin's window becomes one step of history holding the
    // instrument's new state. Runs on a timer; callable directly.
    void drainPluginEdits();
    // Serves what plugins asked of the main thread (idle()) and drains their
    // edits: what the editor timer does every turn.
    void serviceEditors();

    // --- Audio input and audio takes (item 3.2; app_controller_record_audio.cpp)
    QString audioTakeStatus() const { return audio_take_status_; }
    // Hands every track's audio-input route to the running engine, and the
    // device's input count to the strips' input chips.
    void updateAudioInputs();
    // Whether any track is hearing its audio input through the engine now.
    bool monitorsAudioInput() const;
    // Whether any track of the song takes audio input: the device is opened
    // duplex only then.
    bool songTakesAudioInput() const;
    // Where a take is written: <project>.audio/ beside a saved project, or the
    // session's own temporary folder until the first save (decision 8).
    std::filesystem::path recordingDirectory() const;
    // What a take is moved earlier by to sound where it was heard (plan C21):
    // the device's round trip, the engine's output latency, and the song's
    // record offset.
    std::uint64_t takeCompensation() const;
    // Frames of input the capture ring dropped in the last take, and the
    // clip the last take made.
    std::uint64_t droppedInputFrames() const noexcept { return dropped_input_frames_; }
    qint64 lastRecordedClip() const noexcept { return last_recorded_clip_; }
    // --- Scene launcher (phase 2, wave 6.1; app_controller_launcher.cpp) ----
    QVariantList launcherState() const { return launcher_state_; }
    bool launcherRecording() const noexcept { return launcher_recording_; }
    // Launches a cell, a whole scene, or stops a track or every track, at the
    // quantization the song's grid says. Launching on a stopped transport
    // starts it, and the launch plays from its first block. The grid's edits
    // of this turn reach the engine first. False when there is nothing to
    // launch or no engine to launch it in.
    Q_INVOKABLE bool launchCell(int scene, int track);
    Q_INVOKABLE bool launchScene(int scene);
    Q_INVOKABLE bool stopLauncherTrack(int track);
    Q_INVOKABLE bool stopLauncher();
    // Arrangement recording on or off: while on, every stretch a track plays
    // from one cell is printed into the arrangement when it ends, one step of
    // history per take.
    Q_INVOKABLE void toggleLauncherRecording();
    // Reads the launcher's status and prints the takes that came back. Runs
    // with the meters; callable directly.
    void pollLauncher();
    // Takes printed since the controller started.
    int launcherTakesPrinted() const noexcept { return launcher_takes_; }

    // --- Metronome and count-in (item 3.7; app_controller_metronome.cpp) ----
    bool countingIn() const noexcept { return counting_in_; }
    // Reads whether the engine is counting in, for the transport to show.
    void pollCountIn();

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
    void samplerChanged();
    void importChanged();
    void assetsChanged();
    void warpChanged();
    void editorsChanged();
    void outputRecordingChanged();
    void editorReadoutChanged();
    void audioTakeChanged();
    void countInChanged();
    void launcherChanged();

private:
    struct ImportResult;
    // Decodes the audio every clip of the song plays, at `rate`, through the
    // asset store; flags the files that are missing on the song model.
    blokkily::AudioAssets clipAssets(double rate);
    // The renditions of the song's warped clips at `rate` that are ready, from
    // the asset cache. Asks the warp worker for any that are not, drops
    // requests no clip needs any more, and marks the clips still rendering.
    // Call after clipAssets(rate), whose sources it renders from.
    blokkily::ClipRenditions clipRenditions(double rate);
    // Back on the control thread: moves finished renditions into the cache
    // and recompiles so the clips waiting for them are heard.
    void collectWarpRenders();
    // Back on the control thread: places a finished import.
    void finishImport(int ticket, const ImportResult& result);
    // Whether the song needs an engine even without an instrument.
    bool hasAudioClips() const;
    // Undo or redo put back instrument states on these tracks: each running
    // instance that is still the instrument the song names, and whose state
    // differs, is given the song's (a sampler live, a plugin that takes no
    // state while running with the device stopped for the load).
    void restoreInstrumentStates(const QList<int>& tracks);
    // After a rebuild: editors follow their tracks and stay on adopted
    // instances.
    void reconcileEditors(const blokkily::TrackRemap* remap);
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
    // Automation (item 3.1; app_controller_automation.cpp). A strip move from
    // the song model, handed to the engine.
    void stripMoved(int track, int control, double value, double previous);
    // Records the strip moves the engine played into the take's passes.
    void drainAutomation();
    // A plugin's own edit, recorded when its track's mode records.
    void captureParameterEdit(const blokkily::PluginEditEvent& event);
    // Writes the passes that have closed into the song, as part of the take.
    void commitAutomation();
    // Play: a new take, with a pass opened on every lane of a track in write
    // mode. Stop: every pass closes where the transport stopped.
    void startAutomationTake();
    void finishAutomationTake();
    // The song tick the engine renders now.
    blokkily::Tick renderTick() const;
    // Hands the song's tempo and meter maps to the transport.
    void syncTimebase();
    void assignInstrument(int track, const blokkily::InstrumentSlot& slot,
                          const QString& label);
    // Hands `state` to the processor the running engine has for `track`.
    // `stop_if_needed` lets a processor that cannot take state while it runs
    // be given it with the device stopped for the moment of the load.
    bool pushInstrumentState(int track, std::span<const std::byte> state, bool stop_if_needed);
    // Wires the sampler panel and undo to the song (app_controller_sampler.cpp).
    void connectSampler();
    // Audio takes (app_controller_record_audio.cpp). A take is written while
    // the song records and plays: started when recording is armed or the
    // song starts, finished, and placed as clips in the same step of history
    // as the notes, when the song stops or recording is disarmed.
    void connectAudioInput();
    void startAudioTake();
    void finishAudioTake();
    // Stops writing a take that belongs to a song being replaced.
    void discardAudioTake();
    void commitAudioTakes(std::vector<blokkily::RecordedTake> takes);
    // Moves the takes still in the session's temporary folder into the audio
    // folder of the project saved at `path` (decision 8).
    void relocateRecordings(const QString& path);
    // The program the sampler on `track` plays, or nullopt when it has none.
    std::optional<blokkily::SamplerProgram> samplerProgram(int track) const;
    // Applies `edit` to the selected track's sampler program and, if the
    // result is valid, makes it the track's state.
    bool editSampler(const QString& merge,
                     const std::function<bool(blokkily::SamplerProgram&)>& edit);
    // Rewrites every sampler's sample paths for a project that now lives in
    // `directory`, so a Save As elsewhere still finds them.
    void rebaseSamplers(const std::filesystem::path& directory);
    // The General MIDI bank this machine offers, preferring the familiar ones.
    static std::filesystem::path findDefaultBank(const std::vector<std::filesystem::path>& roots);
    // A SoundFont slot on `bank`, set to the preset a track of that name wants.
    static blokkily::InstrumentSlot defaultSlot(const std::filesystem::path& bank,
                                                const std::string& track_name);

    QString status_ = "Ready — CLAP native";
    QVariantList plugins_;
    QString browser_filter_;
    QString browser_kind_ = QStringLiteral("instrument");
    QString soundfont_status_ = "No instrument loaded";
    QString project_status_ = "Not saved";
    QString project_detail_ = "No project on disk";
    QString export_status_ = "Not exported";
    QString project_path_;
    SongModel* song_ = nullptr;
    PatternModel* pattern_ = nullptr;
    Transport* transport_ = nullptr;
    // Decoded audio shared by every processor that plays files and by the
    // song's audio clips (plan F-B). Declared before the engine so it outlives
    // every sampler reading it.
    std::unique_ptr<blokkily::AudioAssetCache> assets_ =
        std::make_unique<blokkily::AudioAssetCache>();
    // The sampler zone the panel edits.
    int sampler_zone_ = 0;
    // Declared before the engine and the device so it outlives both: the render
    // callback reads its queue until the device has stopped.
    std::unique_ptr<blokkily::MidiInput> midi_input_;
    // What the engine captures recorded input into, and the thread that
    // writes it to disk. Declared before the engine and the device so it
    // outlives both: the callback writes into its ring until the device stops.
    std::optional<blokkily::OutputTap> output_source_;
    std::uint32_t output_take_latency_ = 0;
    std::unique_ptr<blokkily::TakeWriter> output_writer_ = std::make_unique<blokkily::TakeWriter>();
    std::unique_ptr<blokkily::TakeWriter> take_writer_ = std::make_unique<blokkily::TakeWriter>();
    // The session's temporary take folder, made when first needed.
    mutable std::filesystem::path session_audio_dir_;
    QString audio_take_status_ = QStringLiteral("No audio recorded");
    std::uint64_t dropped_input_frames_ = 0;
    qint64 last_recorded_clip_ = 0;
    QStringList midi_ports_;
    QString midi_activity_ = QStringLiteral("—");
    std::uint64_t midi_notes_ = 0;
    bool record_armed_ = false;
    // The take being recorded, one recorder per track so a key held on one
    // track is never paired with a release on another. The first note written
    // checkpoints history, so a take is one step of undo.
    std::vector<blokkily::TakeRecorder> takes_;
    // Automation passes of the take being played (item 3.1), and the strip
    // controls being held.
    blokkily::AutomationTake automation_take_;
    std::vector<std::pair<int, int>> held_controls_;
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
    // Notes the surfaces performed into a take, with the tracks each went
    // down on, so the release reaches those tracks whatever is armed by then.
    struct PerformedNote {
        blokkily::TunedPitch pitch;
        std::vector<std::size_t> tracks;
    };
    std::vector<PerformedNote> performed_;
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
    // What the engine was last given for the song's clips, indexed like
    // Song::audio_files: the owners of the audio the waveforms draw.
    blokkily::AudioAssets clip_assets_;
    int asset_revision_ = 0;
    // Imports decoding off the control thread, by ticket. Joined when they
    // report back, and in the destructor, so none outlives the controller.
    std::map<int, std::thread> imports_;
    int next_import_ = 1;
    int imports_pending_ = 0;
    // Renders warped clips off the control and audio threads (item 3.6).
    // Created on first use; declared after the cache it renders for, so it
    // stops before the cache goes.
    std::unique_ptr<blokkily::WarpRenderer> warp_;
    int warp_pending_ = 0;
    // Plans the worker could not render: not asked for again, so a clip
    // that cannot be rendered stays silent instead of rendering for ever.
    std::set<std::string> warp_failed_;
    QString warp_status_ = QStringLiteral("No warped clips");
    QString import_status_ = QStringLiteral("No audio imported");
    qint64 last_imported_clip_ = 0;
    // Plugin editors. Declared last so the windows close while the engine and
    // the instances they edit still exist.
    blokkily::EditGestures gestures_;
    QString editor_status_;
    QString editor_readout_;
    QTimer editor_timer_;
    std::unique_ptr<PluginWindows> windows_;
    bool counting_in_ = false;
    QVariantList launcher_state_;
    bool launcher_recording_ = false;
    int launcher_takes_ = 0;
};
