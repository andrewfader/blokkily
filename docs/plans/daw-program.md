# DAW program plan

Status: design complete; building in waves. The architect's plan follows the
decisions below. **Where the two disagree, the decisions win.** Per-feature
design detail (designs 1–7) is summarised inside the plan; W0.1 (commit the
MIDI work) is done: commit 4a8c2ae.

## Product decisions (answers to plan §5)

1. Note-merging fix: merged voices keep **both** their own velocity **and**
   their own length. `Chord` gets per-voice velocity and per-voice duration
   (item 1.6 grows accordingly).
2. **Record the outputs too.** New item **3.5 Output recording**: track,
   return-bus and master taps (post-fader, post-inserts) can be recorded or
   bounced in place into new audio clips / stems on new audio tracks, through
   the same engine, sample-aligned, including resampling the master live.
3. One per-track R button arms notes and audio. Automation has its own
   per-track mode: off / read / touch / latch / write.
4. With no track armed, recording goes to the selected track (as today).
5. Arm and input state is persisted with the project but is not changed by
   undo/redo.
6. Overlapping audio clips on one track **sum** (every clip sounds).
7. **Time-stretching is allowed.** New item **3.6 Clip warp**: an audio clip
   can follow the song's tempo map (stretch without pitch change) using
   Rubber Band 4 (installed, GPL — note the licence), rendered off the audio
   thread into the asset cache so the render callback still only reads
   decoded memory. Clips also get a manual stretch ratio and a pitch shift.
   Audio stays fully decoded in RAM (no disk streaming).
8. Imported files are referenced in place; recordings are written into
   `<project>.audio/` (a temp session folder until the first save); "Collect
   audio" copies referenced files in.
9. Editing a meter keeps clips and tempo points on their bar numbers
   (`rebar`). No pickup/partial bars yet.
10. Returns are solo-safe; sends are post-fader, pre-pan; plugin delay
    compensation applies in live playback as well as in the bounce.
11. Effect parameters are automatable (3.3 is in scope).
12. Knob turns in a plugin's own window are undoable, one step per gesture.
13. Linking JUCE GUI code into the app is acceptable, subject to spike 1.7.
    CI may provide Xvfb; locally the pixel check skips without an X server.
14. **Metronome and count-in are in scope.** New item **3.7**: a click track
    that follows the tempo and meter maps (accented downbeats, level, on/off,
    audible live, excluded from bounces unless asked), and a count-in of N
    bars before recording starts, which does not move the song position.

## Build rules for every agent

- Work items, interfaces, tests and definitions of done are in the plan
  below; AGENTS.md applies in full.
- Parallel work happens in git worktrees branched from `master`. The
  `third_party/JUCE` and `third_party/clap` submodules may be empty in a
  worktree: symlink them to `/mnt/earth/workspace2026_good/blokkily/third_party/*`
  and **never stage those symlinks** — commit with explicit paths, never
  `git add -A`.
- Build with ccache and at most `-j6` when other agents may be building.
- Commit finished work on the worktree branch; integration merges into
  `master` and re-runs the full suite there.

---

# Blokkily build plan: audio clips, sampler, effects, record everything, note-merging fix, tempo and meter, native plugin windows

This plan is for the working tree at `/mnt/earth/workspace2026_good/blokkily`. I checked the claims in the designs against the code before writing it. The important findings:

- **Format version.** `ProjectFile::format_version` is 4 and `parse` accepts only an exact match (`src/project/project.cpp:250`).
- **The note-merging bug is real.** `write_played` drops velocity: `Voice` in `src/sequencer/take.cpp` has no velocity field, and the merged `Chord` has none either. `Scheduler::render` hard-codes `0.8F` (`src/sequencer/scheduler.cpp:67`).
- **Engine state today:**
  - `SongEngine::process_chunk` skips tracks that have no instrument.
  - It value-initialises a stack array of `768 × PluginEvent` (about 24 KB) for every track on every chunk.
  - It uses one constant `samples_per_tick`.
  - Track state (`TrackPlayback`) and the compiled arrangement (`Arrangement`) are private structs in `song_engine.hpp`.
- **CLAP adapter.** `host_extension` returns nullptr and output events are discarded (`src/plugins/clap_instance.cpp:42,67`).
- **CLAP fixture.** It already reacts to `PARAM_VALUE` id 0 (level) and id 1 (tone). It has no `clap.params` extension, and its state is only the 4-byte level.
- **VST3 adapter.** It uses `VST3PluginFormatHeadless`, and its automation and modulation vectors are plain `float`s.
- **Dependencies and display.** libsndfile 1.2.2, libsamplerate 0.2.2 and soxr are installed. There is no `xvfb-run` or `Xvfb`. This session has both `DISPLAY=:1` and `WAYLAND_DISPLAY` set.
- **Hotspots are monoliths:**
  - `gui_main.cpp` is 2,457 lines, with every scenario inside one lambda in `main`.
  - `ui/Main.qml` is 2,665 lines.
  - `pattern_model.cpp` is 2,104 lines and holds PatternModel, Transport and AppController.
  - `core_tests.cpp` is 1,715 lines inside a single `main()`.
- **Uncommitted work.** The MIDI input and recording work is uncommitted: part staged, part unstaged (`MM CMakeLists.txt`), plus an untracked feature file. Git worktrees do not see uncommitted changes.

---

## 0. Before any agent starts

1. **The user must approve committing the in-progress MIDI work** (W0.1). Designs 4, 5, 6 and 7 build on `TakeRecorder`, `write_played`, `MidiInput`, `connect_input` and the `--scenario midi` harness. None of that exists in a worktree until it is committed. Commit it onto an integration branch cut from `master`, for example `program/daw-v5`. Every feature worktree branches from that integration branch and merges back into it.
2. **Merge gate, per AGENTS.md.** Each worktree runs the full `cmake … && ctest` suite before it asks to merge. After each merge, the integration tree runs the full suite again and the agent inspects the relevant PNGs. A green result in the worktree does not count for the merged tree.
3. **Build cost.** Each worktree builds JUCE and Qt from scratch. Run at most 3 or 4 builds at once and turn on `CMAKE_CXX_COMPILER_LAUNCHER=ccache`, which W0.2 adds.
4. **Display isolation.** All offscreen gates and `blokkily_tests` must run with `DISPLAY=` and `WAYLAND_DISPLAY=` unset in the CTest `ENVIRONMENT`. Once JUCE GUI code is linked in, a stray `DISPLAY=:1` would make the headless tests depend on the display.

---

## 1. Shared foundations: each one is specified once and owned by one work item

These interfaces are binding. Feature items use them and may not redefine them.

### F-A. Timebase: tempo map, meter map, tick↔sample (owner: 1.2)

`include/blokkily/model/timebase.hpp`, `src/model/timebase.cpp`:

```cpp
struct TempoPoint { Tick at = 0; double bpm = 120.0; bool ramp = false; };   // ramp: linear bpm in ticks to next point
struct TempoMap {
    std::vector<TempoPoint> points{TempoPoint{}};   // sorted, points[0].at==0, unique, 20<=bpm<=300
    [[nodiscard]] double bpm_at(Tick) const;
    [[nodiscard]] double seconds_at(Tick, Tick ticks_per_beat) const;          // closed form for ramps
    [[nodiscard]] double tick_at_seconds(double seconds, Tick ticks_per_beat) const;
    [[nodiscard]] bool valid() const;
    void set(TempoPoint); bool remove(Tick at);                                // refuses tick 0
};
struct MeterChange { std::int32_t bar = 0; std::int16_t numerator = 4; std::int16_t denominator = 4; };
struct MeterMap {
    std::vector<MeterChange> changes{MeterChange{}};
    [[nodiscard]] Tick bar_length(std::int32_t bar) const;   // num * 1920/den
    [[nodiscard]] Tick bar_start(std::int32_t bar) const;
    [[nodiscard]] std::int32_t bar_at(Tick) const;
    [[nodiscard]] Tick beat_length(Tick at) const;
    struct Position { int bar, beat, sixteenth; };
    [[nodiscard]] Position position_at(Tick) const;
    [[nodiscard]] bool valid() const;
};
void rebar(Song&, const MeterMap& before);             // policy pending open question Q9
class TickClock {                                        // immutable; built on the control thread
public:
    TickClock() = default;                               // 120 bpm, 48 kHz
    TickClock(const TempoMap&, Tick ticks_per_beat, double sample_rate);
    [[nodiscard]] double sample_at(Tick) const noexcept;
    [[nodiscard]] double tick_at(double sample) const noexcept;
    [[nodiscard]] double sample_rate() const noexcept;
    [[nodiscard]] double bpm_at_sample(double) const noexcept;
};
[[nodiscard]] Tick tick_at_sample(const TickClock&, std::uint64_t sample);   // floor; the ONE sample->tick path
```

- `Song` gains `TempoMap tempo; MeterMap meter;`.
- `Project::tempo` and `Transport::setBpm` are removed, and `AppController::ticksPerSample()` is deleted.
- Engine signatures become `prepare(const Song&, double rate, std::uint32_t max_block, std::uint64_t seed = 0, std::string* = nullptr, const AudioAssets& = {})` and `recompile(const Song&, std::uint64_t seed = 0, std::string* = nullptr, const AudioAssets& = {})`. The trailing `AudioAssets` parameter is added by 2.2.
- `Arrangement` carries `TickClock clock`. The control thread reads the last published clock through `SongEngine::published_clock()`.
- `take_queued_arrangement()` moves the playhead to the same musical tick under the new clock. If a seek was taken in the same block, it skips that move.
- `compile_timeline(const ScheduledEvents&, const TickClock&, std::uint64_t last_sample)` becomes the main overload. The `samples_per_tick` overload remains as a wrapper around a single-point clock, used by `RealtimePlayback`.
- **Audio in seconds:** `TempoMap::seconds_at` and `tick_at_seconds` let `Song::length()` place the end of an audio clip without knowing the sample rate. This removes the audio-clips design's `length(double bpm)` overload.
- `SongModel::barLayout` is a model property here, not a QML one. Each entry is `{start, ticks, numerator, denominator, x0, x1}`, where x0 and x1 are fractions of the song. Audio clips, the ruler and automation lanes all take their geometry from it.

### F-B. Audio files and the decoded-asset store (owner: 1.3)

Both the audio-clips and sampler designs defined their own `audio_file.hpp`. They are merged into this one:

```cpp
// include/blokkily/audio/audio_file.hpp   (libsndfile + libsamplerate, REQUIRED, PRIVATE to blokkily_core)
struct AudioFileInfo { std::uint64_t frames = 0; std::uint32_t sample_rate = 0; std::uint16_t channels = 0; };
struct LoopPoints { std::uint64_t start = 0, end = 0; };
struct DecodedAudio { AudioFileInfo info; std::vector<float> left, right;   // right empty => mono
                      std::optional<LoopPoints> loop; std::optional<int> root_key; };  // from SFC_GET_INSTRUMENT / smpl
std::optional<AudioFileInfo> probe_audio_file(const std::filesystem::path&, std::string* error);
std::optional<DecodedAudio>  decode_audio_file(const std::filesystem::path&, std::string* error); // WAV/FLAC/AIFF/OGG/MP3
std::vector<float> resample(std::span<const float>, double ratio);   // SRC_SINC_BEST_QUALITY, deterministic, control thread

// include/blokkily/audio/audio_asset.hpp
struct AudioAsset { std::uint32_t rate; std::uint64_t frames; std::vector<float> left, right;
                    std::optional<LoopPoints> loop; std::optional<int> root_key;
                    std::vector<std::pair<float,float>> peaks; };           // min/max per 256 frames
using AudioAssetPtr = std::shared_ptr<const AudioAsset>;
using AudioAssets   = std::vector<AudioAssetPtr>;                         // indexed like Song::audio_files; null = missing
class AudioAssetCache {                                                   // control thread only; owned by AppController
public:
    // target_rate 0 = native rate (sampler); engine rate = pre-resampled (clips).
    // `expect` set => mismatch in frames/rate/channels is reported as missing, never played wrong.
    AudioAssetPtr load(const std::filesystem::path&, double target_rate,
                       std::optional<AudioFileInfo> expect, std::string* error);
    void insert(const std::filesystem::path&, AudioAsset);                // recorder registers takes
    void purge_unused();
};
```

- **Retiring buffers:** the audio thread holds only raw pointers. `shared_ptr`s are released on the control thread. For clips this happens when an `Arrangement` slot is refilled. For sampler kits it happens by epoch.
- `read_wave()` keeps its signature. It remains the bounce verifier and is not rewritten on top of libsndfile, so the reader never verifies the writer.
- **Fixtures:** `tests/fixtures/make_audio_fixtures.cpp` builds the `blokkily_make_audio_fixtures` tool, which writes to `${CMAKE_BINARY_DIR}/audio-fixtures`. It writes RIFF bytes by hand for WAV (PCM16/24, float, EXTENSIBLE, `smpl`) and uses libsndfile for FLAC and AIFF. Content is deterministic: sines, a DC step marker, and hits8. No binary files are checked in.

### F-C. Plugin boundary v2 (owner: 1.4; `plugin.hpp` is frozen after this)

```cpp
enum class WindowApi { x11, wayland, win32, cocoa };
struct NativeParent { WindowApi api; std::uintptr_t handle; double scale = 1.0; };
struct EditorSize { std::uint32_t width = 0, height = 0; bool resizable = false; };
class EditorHost { public: virtual ~EditorHost() = default; virtual void request_resize(std::uint32_t, std::uint32_t) = 0;
                   virtual void request_show() = 0; virtual void request_hide() = 0; virtual void closed() = 0; };
struct PluginPorts { std::uint32_t audio_inputs = 0; bool note_input = true; };
struct ParameterInfo { std::int32_t id; std::string name; double min, max, default_value; bool automatable; };
struct ParameterEdit { enum class Kind : std::uint8_t { begin, value, end } kind;
                       std::int32_t parameter; double value; std::uint32_t sample_offset; };
struct TransportInfo { double bpm, beat; std::int32_t bar; std::int16_t numerator, denominator; bool playing; };

class PluginInstance {   // existing members unchanged; process() is now documented IN PLACE (entry = input, silence for instruments)
    virtual PluginPorts ports() const { return {}; }
    virtual std::uint32_t latency_samples() const noexcept { return 0; }
    virtual std::uint64_t tail_samples() const noexcept { return 0; }
    virtual bool latency_changed() noexcept { return false; }                 // control-thread poll
    virtual bool accepts_state_while_running() const noexcept { return false; }
    virtual std::vector<ParameterInfo> parameters() const { return {}; }
    virtual std::size_t take_parameter_edits(std::span<ParameterEdit>) noexcept { return 0; } // AUDIO thread, after process()
    virtual void set_transport(const TransportInfo&) noexcept {}
    virtual void idle() {}                                                    // main thread
    virtual bool has_editor() const { return false; }
    virtual bool supports_editor(WindowApi, bool floating) const { return false; }
    virtual bool open_editor(const NativeParent*, EditorHost&, EditorSize*, std::string*) { return false; }
    virtual bool resize_editor(std::uint32_t&, std::uint32_t&) { return false; }
    virtual void set_editor_scale(double) {}
    virtual void close_editor() {}
    virtual bool editor_open() const { return false; }
};
```

- **One outbound parameter channel.** The record-everything and plugin-windows designs each proposed their own (`ParameterChange` pulled by the engine, and `take_parameter_edit` pulled by the control thread). Only one survives:
  - The adapter queues edits internally. CLAP edits come from `out_events` on the audio thread. VST3 edits come from JUCE listeners on non-realtime threads into a mutex-guarded producer side with a lock-free consumer.
  - The engine drains `take_parameter_edits` on the audio thread straight after `process()`. It stamps each edit with the song sample and pushes it to one engine ring (see F-D).
  - VST3 edits therefore get block precision, not the ~33 ms from `pollMeters`.
- **Adapters (1.4):**
  - CLAP gains a real `host_extension` table: params with rescan and request_flush, thread-check, latency, tail, and audio-ports input wiring through a preallocated input copy. `out_events` feeds the edit ring.
  - VST3 gets an atomic automation base: `std::unique_ptr<std::atomic<float>[]>`. `load_state` re-seeds that base, which fixes a latent bug with a regression test. It also enables the stereo input bus, and gains the listener ring and latency/tail reporting.
  - The GUI, timer and posix-fd extensions and the JUCE GUI format come later, in 2.6.
- **Fixture changes, merged into one coherent set:**
  - `test_clap.cpp` gains `clap.params` with id 0 Level, id 1 Tone and id 2 VelocityMode. VelocityMode is added by 1.6 and declared in 1.4.
  - It also gains the exported hook `blokkily_test_gui_turn(clap_id, double)`. That hook emits GESTURE_BEGIN, PARAM_VALUE and GESTURE_END through `out_events` after `request_flush`. It replaces both `blokkily_test_move_parameter` from design 4 and design 7's version of the same hook.
  - The state stays backward-readable: the old 4-byte stream still loads.
  - `test_vst3.cpp` gains the exported `blokkily_test_vst3_turn(float)`. Design 7's hook wins over design 4's "note 127 changes a parameter", because it drives the real `IComponentHandler` path without odd note semantics.

### F-D. Engine seams, instance reuse, graph signature (owner: 1.1)

- **Internal engine files.** The engine splits into internal files so that features touch disjoint code:
  - `src/audio/engine/track_playback.hpp` (private) holds `TrackPlayback` with empty member structs `ClipPlayback clips; InsertChain chain; InputPlayback input; StripAutomation automation;`.
  - `Arrangement` gains `ArrangementClips clips; ArrangementAutomation automation;`.
  - Each struct is defined in its own header under `src/audio/engine/`, with stage functions in `engine_clips.cpp`, `engine_effects.cpp`, `engine_input.cpp` and `engine_automation.cpp`. All are no-ops at first.
- **Fixed per-chunk order.** This settles the order question between the audio-clips, effects and record-everything designs:
  1. Collect events: arrangement notes still owed a release, then live, performed (2.5), routed MIDI, and timeline events.
  2. Zero the track buffers. This happens even with no instrument; the `continue` is removed.
  3. `instrument->process` runs in place.
  4. `take_parameter_edits` goes to the engine ring.
  5. Audio-clip regions are summed in (2.2), only when the transport is playing and events come from the timeline.
  6. Input monitoring is added (3.2). Capture taps the raw input here, before anything else is applied.
  7. The insert chain runs in place (2.4), and each slot's edits are drained.
  8. The PDC compensation delay is applied (2.4).
  9. The chunk strip gain is computed: the automation envelope ramp × `solo_gate` (3.1), or the static atomics otherwise.
  10. Sends are mixed: pre-fader sends take the post-chain, pre-strip signal; post-fader sends take the signal after the strip gain and pan (2.4).
  11. `mix_into` or `mix_into_ramp` adds the track to the direct bus, and the peak is recorded.

  After all tracks: returns, then master compensation on the direct bus, then master inserts, then master gain.
- **Scratch buffers.** Per-track event scratch is preallocated in `prepare()`, replacing the per-chunk stack array, so larger event budgets (fan-out, perform, automation) are safe.
- **Addressing and the edit ring:**

```cpp
enum class BusKind : std::uint8_t { track, ret, master };
struct ProcessorAddress { BusKind kind = BusKind::track; std::uint32_t bus = 0; std::int32_t slot = -1; }; // -1 = instrument
struct PluginEditEvent { ProcessorAddress where; std::uint64_t song_sample; bool rolling; ParameterEdit edit; };
// SongEngine additions
void set_processor(ProcessorAddress, std::unique_ptr<PluginInstance>);        // set_instrument becomes a wrapper
PluginInstance* processor(ProcessorAddress) const;                            // control thread
struct ReleasedProcessor { ProcessorAddress where; std::unique_ptr<PluginInstance> instance; };
std::vector<ReleasedProcessor> release_processors();                          // only after RtAudioOutput::stop()
bool update_processor_state(ProcessorAddress, std::span<const std::byte>);    // only if accepts_state_while_running()
bool take_plugin_edit(PluginEditEvent&) noexcept;                             // SpscQueue<PluginEditEvent,1024>
```

- **App side:**
  - `src/app/processor_factory.{hpp,cpp}` replaces `createInstrument`. It provides `create_processor(const InstrumentSlot&, const ProcessorContext&, std::string*)`, where `ProcessorContext` carries the project dir and the asset cache. It also provides an internal registry, `register_internal(format, factory, catalog_entries)`. SoundFont registers now; the sampler (2.3) and built-ins (2.4) register later.
  - `GraphSignature` is `{per-address processor identity (format/path/identifier), return count}`. `builtFromCurrentGraph()` replaces `builtFromCurrentInstruments()`.
  - `rebuildEngine` adopts released instances whose identity is unchanged, in order and address by address, using the index map returned by `Song::remove_track`. It creates only what is new.
  - `requestRecompile()` coalesces recompiles to at most one per event-loop turn and retries on "no free arrangement slot". Tempo-lane, clip and automation drags all send many recompiles, and the triple buffer can refuse them.

### F-E. Program schema: one owner for `song.hpp` and new records (owner: 1.5)

All model additions from designs 1, 3 and 4 land together, with persistence and round-trip tests. Engines and UI arrive later.

- **Audio:**
  - `AudioFileRef{path, frames, sample_rate, channels}`.
  - `AudioClip{id, track, file, start, offset_frames, length_frames, gain_db, fade_in_frames, fade_out_frames}`. The `id` is new: it is a stable identity so drag gestures survive index shifts.
  - `Song::audio_files` and `Song::audio_clips`.
- **Effects:**
  - `using PluginSlot = InstrumentSlot`.
  - `EffectSlot{plugin, bypass, parameters}` and `Send{bus, level_db, pre_fader}`.
  - `Track::inserts` and `Track::sends`.
  - `ReturnBus{name, inserts, mix}`, plus `Song::returns` and `Song::master_inserts`.
- **Recording:**
  - `TrackInput{armed, source, midi_channel, audio_first_channel, audio_channels, monitor}` as `Track::input`.
  - `Song::record_offset_samples`.
  - `AutomationTarget{kind, parameter_index, parameter_id, ProcessorAddress processor}`. The `processor` field is new: it defaults to the track instrument and lets effect slots be automated later.
  - `AutomationLane` as `Track::automation`, with `value_at`, `write_pass` and `thin`.
- **Helpers:**
  - `Song::remove_track(i)` returns `std::vector<std::optional<std::size_t>>`, an old→new index map. It re-indexes clips, audio clips, sends and automation in one place. `SongModel::deleteTrack` uses it, and so do instance adoption (F-D) and editor-window re-keying (2.6).
  - `Song::prune_audio_files()` runs on save only.
  - `consistent()` checks every new invariant.

### F-F. Project format policy (owner: W0.6)

- **One version bump for the whole program, done once in Wave 0:** `format_version = 5`, and parse accepts `4 <= v <= 5`.
- **v5 is additive only.** Every feature adds new record types in its own `src/project/records_<feature>.cpp`, registered in a handler table. A missing record means the v4 default. This removes the design-1/3/4/5/6 race over `format_version` and the parse check.
- **Two resulting deviations from the designs:**
  - The chord velocities from design 5 do **not** extend the `chord` line. They get an additive `voicevel <pattern> <trigger-id> <velocity> <n> v0…` record, written only when it differs from the default (0.8, no per-voice values). It follows the same pattern as `lock`, and v4 chords are unaffected.
  - The legacy `tempo <bpm>` record from design 6 is accepted in any version and means a single point at tick 0. It is an error only if it appears together with `tempo_point` records, and it is never written. So there is still only one tempo source in memory, and v5 files saved during the program keep loading.
- **Paths.** Signatures become `serialize(const Project&, const std::filesystem::path& base_dir = {})` and `parse(const std::string&, std::string*, const std::filesystem::path& base_dir = {})`. `include/blokkily/project/paths.hpp` provides `to_project_relative` and `resolve_project_path`. Audio clips, sampler zones (through `SamplerInstrument::set_base_directory`) and, optionally, SoundFont slots all use it.

### F-G. Test and verification harness (owner: W0.3, W0.7)

- **Probe and allocation guard:**
  - `tests/support/audio_probe.hpp` provides `rms`, `peak`, `rising_edges`, `goertzel_energy`, `dominant_frequency` and `first_nonzero`.
  - `tests/support/alloc_guard.{hpp,cpp}` replaces global `operator new`/`delete` and counts allocations through a `thread_local` armed flag. It is linked only into `blokkily_realtime_checks`, a new executable with named cases.
  - Its first case, `engine_baseline`, proves the current `SongEngine::process` does not allocate. That validates the harness before any feature depends on it.
  - Every feature that touches `process()` adds a case in its own file, `tests/realtime/<feature>.cpp`, registered through a static table.
- **Verify driver.** A `VerifyContext` in `src/app/verify/harness.{hpp,cpp}` carries `named`, `click_at`, `mouse_at`, `type_key`, `chord_key`, `pump`, `settle`, `reached`, `check`, `usable(item, w, h)` and `save_screenshot`. Scenarios register with `register_scenario("midi", fn)`, one file each: `src/app/verify/scenario_<name>.cpp`.
- **Per-feature CMake fragments:** `cmake/feature_<name>.cmake` holds sources, tests, labels and CTest gates. `CMakeLists.txt` only `include()`s a fixed list, created in W0.2 with empty stubs for all seven features and the foundations.

---

## 2. Conflicts between designs and how they are resolved

| # | Conflict | Winner / resolution | Why |
|---|---|---|---|
| C1 | Designs 1, 3, 4, 5 and 6 each bump `format_version` to 5 | One bump in W0.6; additive records (F-F) | Stops a five-way race. Dev files keep loading throughout the program. |
| C2 | Design 5 changes the shape of the `chord` line | Additive `voicevel` record | A shape change would need version gating inside v5. |
| C3 | Design 6 rejects `tempo` in v5 | `tempo` accepted as legacy, error only when mixed with `tempo_point` | Files written between W0 and 1.2 contain `tempo`. |
| C4 | Two `audio_file.hpp` APIs (clips: engine-rate `DecodedAudio` plus `AudioAssetCache`; sampler: native `SampleBuffer` plus `SampleCache`) | Merged into F-B; the cache has a target-rate parameter | Clips need sample-exact playback at the engine rate. The sampler plays at a rate ratio, so it wants native rate plus the loop and root metadata from the sampler design. |
| C5 | libsndfile optional (sampler) or required (clips) | REQUIRED (clips) | Already installed and pulled in by FluidSynth. A WAV-only reader would mean two decoders. |
| C6 | Two outbound parameter channels (design 4 `ParameterChange`, design 7 `ParameterEdit`) | One `ParameterEdit`, drained by the engine on the audio thread (F-C, F-D) | Gives one consumer, song-sample stamping for automation, and one ring for undo commits. Design 7's control-thread pull would lose the stamp. |
| C7 | CLAP fixture edited by 4, 5 and 7 with overlapping param ids and hook names | 1.4 owns `clap.params` (0/1/2) and `blokkily_test_gui_turn`; 1.6 owns the velocity-mode DSP (merged first); 2.6 adds gui/timer/fd | One file, three sequential owners, and no duplicate hooks. |
| C8 | VST3 fixture parameter proof (4: note 127; 7: exported hook) | Design 7's exported hook | Uses the real listener path with no special-case note. |
| C9 | Instrument-less tracks (1 and 3 both remove `continue`) | F-D (1.1) | Done once. |
| C10 | Order in `process_chunk` (clips vs inserts vs automation envelope vs sends vs monitoring) | Fixed order in F-D | Effects process clips. Automation is post-FX and feeds post-fader sends. Capture is raw input. |
| C11 | Rebuild rule: effects' `GraphSignature`, windows' instance adoption, sampler's live state push | One F-D implementation: signature, adoption and `update_processor_state` | All three are the same "rebuild or go live" decision. |
| C12 | Processor creation: `createInstrument`, plus 3's `createProcessor` and 2's special case | `processor_factory` with an internal registry (F-D) | The sampler and built-ins register there, and format details stay behind adapters. |
| C13 | Browser: sampler `appendInternalInstruments` vs effects `browserKind` plus built-in list | Catalog entries come from the factory registry with a `kind`; 2.4 owns `browserKind`; the scan counts in `pattern_model.cpp:1114` are fixed to count by format | Avoids counting samplers as SoundFonts and keeps one list of internal entries. |
| C14 | `Song::length(bpm)` overload (1) vs tempo map (6) | The timebase lands first (1.2). `Song::length()` places audio ends via `TempoMap::tick_at_seconds` | There is no bpm parameter anywhere, so there is no second tempo source. |
| C15 | Clip lane geometry with uniform `cellPitch` (1) vs meter-aware bar widths (6) | Clips use `songModel.barLayout` (F-A) | 7/8 bars would otherwise misplace clips. |
| C16 | Where the allocation counter lives (1: `audio_regressions`; 2: `audio_probe.hpp`; 4: a separate exe) | `blokkily_realtime_checks` with `alloc_guard` (F-G) | A global `operator new` override belongs in exactly one binary. |
| C17 | Arm state as model (4) vs session state | `Track::input` in the model, persisted. Undo behaviour depends on Q5 | Keeping it on the track avoids a second track list. |
| C18 | Sampler's relative-path rewrite inside `AppController::saveProject` | The sampler uses F-F's `paths.hpp` through `set_base_directory`; AppController never parses the blob | Format details stay behind adapters. |
| C19 | CLAP `host_extension`, touched by 3 (latency/tail), 4 (params) and 7 (gui/timer/fd/thread-check) | 1.4 builds the table; 2.6 adds gui/timer/fd | One function, two sequential owners. |
| C20 | Tempo-synced delay: prepare-time bpm (3) vs tempo map | `set_transport(TransportInfo)`, declared in 1.4 and called per chunk from `live_->clock` (2.4). The delay reads it | The timebase exists before effects. |
| C21 | Latency of recorded audio (3 vs 4) | Start = first_song_sample − (device round-trip + engine `output_latency()` + `record_offset_samples`) | The monitored path includes master-insert PDC. |
| C22 | Audio-clip index identity during gestures | `AudioClip::id` (F-E) | Design 1 raised this risk itself. |
| C23 | Sampler's epoch retire vs clips' slot-recycle retire | Both kept, each local. Shared rule: shared_ptrs are dropped only on the control thread | They have different lifetimes (voice-held kits vs arrangement-held regions). |

---

## 3. Build waves

Legend: **∥** means the item can run in its own worktree alongside the others. **→** means it must follow the named item on the same lane.

### Wave 0: Commit the baseline and split the hotspots

| Item | Lane | Files | Parallel? |
|---|---|---|---|
| W0.1 Commit the MIDI input and recording work | — | the current working tree | first, alone (needs user approval) |
| W0.2 CMake fragment skeleton plus ccache | cmake | `CMakeLists.txt`, `cmake/feature_*.cmake` (empty stubs) | → W0.1, alone, tiny |
| W0.3 Verify-harness split | verify | `gui_main.cpp` → `src/app/verify/{harness,scenario_default,scenario_midi}.cpp` | ∥ |
| W0.4 QML componentisation | qml | `ui/Main.qml` → `ArrangementView.qml`, `MixerPanel.qml`, `MixerStrip.qml`, `InstrumentPanel.qml`, `Inspector.qml`, `TransportBar.qml`, `StepEditors.qml` (+ `qt_add_qml_module` list) | ∥ |
| W0.5 App C++ split | app | `pattern_model.cpp` → `pattern_model.cpp`, `transport.cpp`, `app_controller.cpp`, `app_controller_scan.cpp`, `app_controller_take.cpp`; `song_model.cpp` split by concern; the headers get marked sections | ∥ |
| W0.6 Project format policy (F-F) | project | `project.{hpp,cpp}`, `records_core.cpp`, `paths.hpp` | ∥ |
| W0.7 Test support (F-G) | tests | `tests/support/*`, `tests/realtime/*`, `blokkily_realtime_checks` | ∥ |

**Goal:** no behaviour change. Afterwards every later feature adds files instead of editing the big ones.

**Tests:**
- W0.1: capture reference PNGs of every existing gate into untracked `build/artifacts/baseline/`.
- W0.3–W0.5: every existing gate and the full suite still pass, the object names are unchanged, and the agent inspects each gate's PNG next to its baseline.
- W0.6: an existing v4 text parses; v5 serializes byte-identically twice in a row; `blokkily-project 3` is still rejected; a relative path is written under `base_dir` and resolved back to absolute.
- W0.7: `engine_baseline` reports zero allocations across 1,000 `process()` calls with a seek and a loop wrap. As a self-check of the harness, a deliberate allocation inside an armed region is detected.

**Definition of done:** the full suite passes twice, before and after the final edit. PNGs are inspected against the baseline. `ctest -N` lists the same tests plus `realtime_engine_baseline`.

**Size:** about 800 new lines plus about 5,000 moved. 2–3 agent-days, about 1.5 days of wall time. **Risk:** W0.4. Moving QML can break `SplitView` sizing and anchors. The existing gates and the usable-size checks are the guard.

### Wave 1: Foundations, plus the note-merging fix

| Item | Lane | Depends | Parallel? |
|---|---|---|---|
| 1.1 Engine seams, instance reuse, graph signature, recompile coalescing (F-D) | **engine** | W0 | first on the engine lane |
| 1.5 Program schema (F-E) | model | W0 | ∥ with 1.1 |
| 1.2 Timebase core (F-A): model, engine, Transport, AppController conversions, records; no tempo-lane UI | **engine** | → 1.1, rebases on 1.5 | sequential |
| 1.3 Audio files and asset cache (F-B) plus fixture generator | new files | W0 | ∥ |
| 1.4 Plugin boundary v2 (F-C): adapters, fixture params and hooks | plugins | W0; merges after 1.6 | ∥ |
| 1.6 **Chord velocity, the note-merging fix (feature 5, complete)** | sequencer + UI | W0 | ∥; merge first in this wave |
| 1.7 Spike: JUCE GUI module in core under offscreen, DISPLAY unset and set (throwaway branch; output is a decision note) | spike | W0 | ∥ |
| 1.8 Built-in effect DSP (eq3, delay, reverb, compressor, `builtin.hpp`, `delay_line.hpp`) plus effect fixtures (`test_clap_effect.cpp`, `test_vst3_effect.cpp`) | new files | the 1.4 header | ∥ (late in the wave) |
| 1.9 Sampler DSP core (`sampler_program.*`, `sampler.*`, core tests only) | new files | 1.3 and 1.4 headers | ∥ (late in the wave) |

The work items:

**1.1 Engine seams.**
- **Goal:** everything in F-D.
- **Tests:**
  - An instrument-less track renders through the strip. Before 2.2 lands, this is proved with a test-only source stage that injects DC, and the rendered bus is checked.
  - `release_processors` → adopt → render: the same instance pointer, no `destroy` in the fixture log (via `blokkily_test_observe_process`), and the level is preserved.
  - `update_processor_state` returns false for the CLAP, VST3 and SoundFont adapters.
  - Coalescing: 50 recompile requests in one turn make one recompile and always converge on the latest song.
  - Realtime case `engine_seams`.
  - All existing gates stay green, and the `bdd_edit_once_see_everywhere` PNG is inspected.
- **Done when:** the whole suite passes, no behaviour changes, and the fixture log proves adoption.

**1.5 Program schema.**
- **Goal:** F-E types, `consistent()`, `remove_track`, and records `audiofile`, `audioclip`, `return`, `insert`, `send`, `input`, `record-offset` and `automation` in `records_*.cpp`.
- **Tests:**
  - A v5 round trip of a song using every new field is byte-identical.
  - Each malformed-record case in the designs fails loudly.
  - A v4 text parses with defaults.
  - `remove_track` re-indexes all four lists.
  - `value_at`, `write_pass` and `thin` unit tests from design 4, test 9.
- **Done when:** the suite passes and no engine or UI file changed apart from `SongModel::deleteTrack`.

**1.2 Timebase core.**
- **Goal:** F-A end to end. The engine, Transport (`setTimebase`, `bar()`, `position()`, a read-only `bpm`), AppController (seek, drainTake and finishTake through `tick_at_sample`) and SongModel (`ticks_per_bar` becomes the meter; `barLayout`) all use it. The `tempo_point` and `meter` records are added.
- **Tests:** design 6 tests 1–13, using CLAP DC-edge onsets across a step, a ramp and 7/8. That includes a bounce read back and compared with the live edges, a recording across a tempo change, a non-silent SF2 check and a non-silent VST3 check.
- **Regression test:** `realtime`/`regression` case `tempo_edit_keeps_bar`. It must fail on the pre-1.2 tree first (setTempo while playing moves the musical position).
- The existing default-gate assertions ("2.1.1", the 240/bpm arithmetic) must still pass.
- **Done when:** the full suite passes, the regression is shown failing first, and `bdd_edit_once_see_everywhere` and `bdd_midi_recording` PNGs are inspected.

**1.3 Audio assets.**
- **Tests:** design 1 tests 1–2 and sampler test 1:
  - The WAV decode is bit-exact.
  - FLAC and AIFF decode to within 1 LSB.
  - EXTENSIBLE float files and `smpl` loop and root metadata are read.
  - 44.1→48 kHz resampling gives the right frame count, 1 kHz ± 1 Hz, RMS within 0.1 dB, and the step marker within ±2 frames.
  - Garbage and truncated files return an error.
  - A cache hit does not decode again (checked with a decode counter); an `expect` mismatch reports the file as missing.
- **Done when:** the suite passes and CMake `pkg_check_modules(... REQUIRED)` covers sndfile and samplerate.

**1.4 Plugin boundary v2.**
- **Tests:**
  - CLAP: `parameters()` lists 3 entries.
  - `blokkily_test_gui_turn(0, 0.6)` yields begin, value and end edits, and the rendered level is 0.6.
  - The thread-check answers correctly from the main thread and from `process()`.
  - `request_callback` from a `std::thread` runs `on_main_thread` only after `idle()`.
  - The input-port wiring is proved on the 1.8 CLAP effect once that merges; before then, on a fixture assertion that no input was received.
  - VST3: `blokkily_test_vst3_turn(0.7)` yields edits within a bounded 2 s poll, a note renders at 0.7, and a +0.1 modulation renders **0.8**. This regression test must fail on today's code.
  - The old 4-byte CLAP state still loads.
  - Realtime case `plugin_edits`.
- **Done when:** the suite passes and the regression is shown failing first.

**1.6 Chord velocity, the note-merging fix.**
- **Goal:** everything in design 5, except that velocities persist through the additive `voicevel` record (C2).
- **Tests:**
  - The `write_played` regression, shown failing first: `Note{62,0.6}` merged with a played 66 at 0.9 gives `{0.6, 0.9}`.
  - Scheduler end to end on the CLAP velocity-mode plateau: 0.425, then 0.575 after a one-voice edit.
  - FluidSynth with a real SF2: the Goertzel ratio between C4 and G4 flips by more than 4× between the two renders.
  - Project `voicevel` round trip; a v4 chord plays at 0.8.
  - Gate `bdd_chord_velocity` (`scenario_chords.cpp`): a MIDI take at velocities 40 and 120; `voiceVel0` is at least 18×48; dragging it changes the rendered peak to 0.25×(1+120/127); undo and redo; save and load; the screenshot `chord-velocity.png` is inspected.
- **Done when:** AGENTS.md steps 1–5 are met, and the `features/chord_velocity.feature` and `midi_input_and_recording.feature` amendments are in place.

**1.7 Spike.**
- **Goal:** answer three questions.
  - Does linking `juce_audio_processors` (the GUI module) into `blokkily_core` keep `blokkily_tests`, `blokkily_scan` and the offscreen gates deterministic with `DISPLAY` unset and set?
  - Does `ScopedJuceInitialiser_GUI` try to connect to X?
  - Does embedding a JUCE editor under an offscreen fake `winId` crash, or does it have to be refused?
- **Output:** "core links GUI" or "separate `blokkily_plugin_gui` library". This decides 2.6.

**1.8 / 1.9.**
- **Tests:** the built-in DSP tests from design 3, test 9: EQ ±1 dB, delay echoes at 250 and 500 ms with a 0.5 ratio, a reverb tail, compressor output around −16.5 dBFS, and bit-identical determinism. The effect fixtures go through `ClapPluginInstance` and `Vst3PluginInstance` (design 3, tests 2–3). The sampler core tests are design 2 tests 1–8, 10 and 11, run directly on `SamplerInstrument`.
- **Realtime cases:** `builtin_effects` and `sampler_voices`.
- **Done when:** the suite passes. Neither item has UI yet, so no GUI gate is claimed.

**Merge order:** 1.6 → 1.3 → 1.4 → 1.1 → 1.5 → 1.2 → 1.8 → 1.9. 1.7 is not merged.

**Size:** about 6,500 lines. Timebase core is about 1,300, seams 600, assets 900, plugin v2 900, schema 700, chord velocity 550, built-in DSP and fixtures 1,200, sampler core 1,100. With 5–6 agents that is about 4–6 days of wall time, and the engine lane is the critical path.

### Wave 2: Features on top of the foundations

| Item | Main files (post-W0 layout) | Shared hot files | Parallel? |
|---|---|---|---|
| 2.1 Tempo and meter UI (rest of feature 6) | `song_model_timebase.cpp`, PatternModel `stepCount` (about 15 sites), `ui/TempoLane.qml`, `ArrangementView.qml` ruler | `ArrangementView.qml`, `StepEditors.qml` | ∥; merge **first** |
| 2.2 Audio clips (feature 1) | `engine_clips.cpp`, `app_controller_audio.cpp`, `song_model_audio.cpp`, `waveform_item.*`, `ui/AudioClipLane.qml`, `scenario_audio.cpp` | `song_engine.hpp` (prepare/recompile `AudioAssets`), `ArrangementView.qml` (one line), `app_controller.hpp` | ∥; merge after 2.1 |
| 2.3 Sampler app and UI (rest of feature 2) | `app_controller_sampler.cpp`, `ui/SamplerPanel.qml`, `scenario_sampler.cpp`, factory registration | `InstrumentPanel.qml` (one line), `processor_factory.cpp` (one line), `song_model.cpp` `instrument_label` | ∥ |
| 2.4 Effects (rest of feature 3) | `engine_effects.cpp`, `mixer.*`, `bounce.cpp`, `plugin_scan.*`, `scan_main.cpp`, `app_controller_effects.cpp`, `song_model_effects.cpp`, `ui/EffectRack.qml`, `SendDials.qml`, `ReturnStrip.qml`, `scenario_effects.cpp` | `MixerStrip.qml`, `MixerPanel.qml`, `InstrumentPanel.qml` (browser chips), `app_controller_scan.cpp`, `song_engine.hpp` | ∥; merge **last** in this wave (largest rebase, but it owns the mixer layout) |
| 2.5 Record stage A: multi-arm routing and on-screen surfaces (feature 4a) | `midi_input.*`, `event_queue.hpp`, `engine_input.cpp` (perform queue), `keyboard_model.*`, `app_controller_take.cpp`, `song_model_input.cpp`, `ui/TrackInputControls.qml`, `scenario_record.cpp` | `MixerStrip.qml` (arm chip), `StepEditors.qml` (tracker `Keys`) | ∥ |
| 2.6 Native plugin windows (feature 7) | `plugin_run_loop.*`, `clap_instance.cpp` (gui/timer/fd), `vst3_instance.cpp` (GUI format and editor), `src/app/plugin_windows.*`, `app_controller_editors.cpp`, `song_model_plugins.cpp` (`commitInstrumentState`), `scenario_plugin_windows.cpp`, fixtures (gui ext, VST3 editor), `editor_nodisplay_check.cpp` | `MixerStrip.qml` (E button), `InstrumentPanel.qml` (EDITOR), `CMakeLists` JUCE link (per 1.7), app main (install the run loop) | ∥ |

**Merge order:** 2.1 → 2.3 → 2.2 → 2.5 → 2.6 → 2.4. Conflicts in `MixerStrip.qml` and `InstrumentPanel.qml` are one-line component instantiations, because each feature puts its UI in its own component file. `song_engine.hpp` conflicts stay within the sections marked in 1.1.

**2.1 Tempo and meter UI.**
- **Goal:** the tempo lane with step and ramp handles, a meter menu on the ruler, proportional `rulerBar` widths (`preferredWidth` computed from `barLayout`; do not rely on `horizontalStretchFactor` without checking the Qt version), the meter and tempo readouts, `setPatternSteps` with the LEN spinner, and `addPattern` defaulting to the bar length.
- **Tests:** gate `bdd_tempo_meter` covers design 6 GUI steps (a)–(i):
  - `rulerBar1` is about 7/8 as wide as `rulerBar0`, and both are at least 12 px.
  - Seeking gives `sample_position` equal to `sample_at(3600)` and the readout "3.1.1".
  - The pump edge of the bar-3 downbeat is right; a record lands on the correct step; save, load and undo work.
  - `tempo-meter.png` is inspected.
  - A 14-step pattern shows 14 columns in the grid, the tracker and the roll.
  - The existing `bdd_edit_once_see_everywhere` gate stays green: this is the main regression surface.

**2.2 Audio clips.**
- **Goal:** design 1 on top of F-A, F-B, F-D and F-E: the region stage, assets through `prepare`/`recompile`, `Song::length()` including audio ends, SongModel clip operations keyed by `AudioClip::id`, async import that checkpoints on completion, `WaveformItem`, the lane overlay using `barLayout`, `+AUD`, the missing-file flag, and `collectAudio`.
- **Tests:**
  - Design 1 core tests 3–9: exact start sample, offset, length, −6.02 dB, fades, mute/pan/solo from rendered audio with no `prepare`, recompile continuity, and CLAP-plus-audio sum within 1e-6.
  - A bounce read back and compared sample by sample.
  - A folder move with relative paths; missing and changed files.
  - A tempo-map case: a clip at a tick after a tempo step starts at `clock.sample_at(start)`.
  - Realtime case `audio_clips`, covering a recompile from a second thread, a seek and a wrap.
  - Gate `bdd_audio_clips` covers design 1 GUI steps 1–8, including the usable-size check, drag and trim through synthesized mouse events, and inspection of `audio-clips.png`.

**2.3 Sampler app and UI.**
- **Goal:** register "Sampler" and "Drum Sampler" in the factory and catalog, the `sampler` property and invokables, `setInstrumentState` pushing through `update_processor_state` (no rebuild), the undo push after `instrumentStatesRestored`, and `SamplerPanel` at a minimum height of 220 px.
- **Tests:**
  - Design 2 test 9: a live swap through a real `SongEngine` pump, with the playhead continuous and no recompile.
  - Test 12: bounce parity.
  - Gate `bdd_sampler` covers steps (a)–(i): 880 Hz from the pump, a root-key edit through the rendered spinbox with the same `engine_` pointer, undo, chopping hits8 into 8 slices with 200 Hz and 1000 Hz at the step positions, save and load, export read-back, and inspection of `sampler.png`.

**2.4 Effects.**
- **Goal:** design 3 minus the parts already delivered in 1.4 and 1.8:
  - the engine chain, returns, sends and PDC;
  - `set_transport` called per chunk;
  - `output_latency` and `tail_samples`;
  - a bounce that trims latency and extends the tail;
  - scanner `kind` (cache header 2);
  - `browserKind`;
  - graph changes going through the 1.1 signature, and live changes through atomics and per-slot queues;
  - saving CLAP/VST3 effect state on rebuild and on save.
- **Tests:**
  - Design 3 tests 1 and 4–11: the insert chain renders −0.0625; bypass is live and keeps latency, with no `prepare`; sends and returns follow send level, mute and pre-fader; PDC impulses coincide; master insert; project v5 records (records already from 1.5; the test now exercises the engine); the bounce is compared with the render after dropping the latency frames, its tail is non-silent, and the first impulse is at sample 0.
  - A tempo-synced delay follows a tempo step (uses F-A).
  - Realtime case `effects`.
  - Gate `bdd_effects` covers design 3 steps 1–10. The existing `browser.size()==2` check in `scenario_default.cpp` changes to count instruments only. `effects.png` is inspected.

**2.5 Record stage A.**
- **Goal:**
  - `MidiInput::set_routes` with 16 atomic channel masks, where a note-off follows the mask captured at note-on and a `pending_release` retries;
  - `SongEngine::perform()` with its `PerformQueue`;
  - a larger `InputQueue`;
  - `KeyboardModel` and the tracker `Keys` capturing while recording;
  - `updateInputRoutes`;
  - arm and channel UI.
- **Tests:**
  - Design 4 tests 1–5 on panned CLAP tracks, proved from rendered left and right energy.
  - Realtime case `record_fanout` (64 armed tracks).
  - Gate `bdd_record_everything`, part 1 (design 4 steps 1–4): `arm0` and `arm1` are at least 18 px; both patterns hold the step; a piano click and a tracker key land with micro-timing; the cursor does not advance.
  - A screenshot is taken and inspected.

**2.6 Native plugin windows.**
- **Goal:** design 7, with the outbound edits coming from the F-D ring (C6), adoption coming from F-D, and linkage following the 1.7 decision.
- **Additional rule:** under the offscreen QPA, embedding a VST3 (JUCE) editor is refused ("Plugin window needs an X11 display"). The CLAP fixture still embeds, because it only records the handle.
- **Tests:**
  - Design 7 core tests 1–8, with `ManualRunLoop` and the separate `blokkily_editor_nodisplay_check`.
  - Gate `bdd_plugin_windows`, steps 1–10: a 320×200 window, `set_parent(winId)`, `LEVEL 0.60` heard in the pump, undo to 0.25 and redo, `addTrack` keeping the same window with no destroy, an instrument swap closing it, save and load opening no windows, and inspection of `plugin-windows.png`.
  - `plugin_window_display_check` (xcb, skips with code 77) runs here on XWayland :1. The centre pixel must be `#C8FF3C`, and the agent inspects `plugin-window-x11.png`.
  - Record honestly that this check skips wherever there is no X server.

**Size:** about 11,000 lines. Tempo UI about 900, clips about 1,800, sampler app and UI about 1,300, effects about 3,300, record A about 1,300, windows about 2,400. With 6 worktrees (no more than 4 builds at once) that is 6–9 days of wall time, plus about 2 days of serialized merging and re-verification.

### Wave 3: Features that need Wave 2, and integration

| Item | Depends | Parallel? |
|---|---|---|
| 3.1 Record stage B: automation playback, capture and lane editor (feature 4b) | 2.5; 2.4 for the post-FX position; C6 edits | ∥ with 3.2; merge first; owns the `drainTake` restructure |
| 3.2 Record stage C: audio input into audio clips (feature 4c) | 2.2, 2.5, 1.2 | ∥; rebases on 3.1 |
| 3.3 Cross-feature: editors for effect slots (EditorTarget = `ProcessorAddress`), effect-parameter automation targets, "Collect audio" for recordings, `features/*` and docs consolidation | 2.4, 2.6, 3.1 | → 3.1 |
| 3.4 Integration gate `bdd_session_everything`: one song with a tempo ramp, 7/8, a sampler, CLAP and VST3, clips, inserts and a return, automation, and a recorded audio take; bounce read-back parity; screenshot | all | last |

**3.1 Record stage B.**
- **Goal:**
  - `ArrangementAutomation` strip envelope: StripGain breakpoints precomputed at compile time, densified every 256 samples; `mix_into_ramp`.
  - Parameter lanes as timeline `parameter_value` events, with a chase on seek.
  - `SongEngine::move()` with its moves ring, touch bits and `solo_gate`.
  - Capture of strip moves and plugin edits through `take_plugin_edit`, with `write_pass` and `thin`.
  - The lane editor in `ui/AutomationLane.qml`.
  - One undo per take covering notes and automation.
- **Tests:**
  - Design 4 tests 10–15: per-quarter RMS of a gain lane is monotonic in the bounce; pan and mute lanes work; touch override has no recompile; strip moves become a lane and replay the same way; plugin moves via `blokkily_test_gui_turn` become a lane and are silenced in the bounce; chase on seek; round trip.
  - A solo combined with an automated lane (the solo_gate risk).
  - A post-fader send follows the automated gain.
  - Realtime case `automation`.
  - Gate part 2: fader drag leads to `automationPoint0_*` items.

**3.2 Record stage C.**
- **Goal:**
  - `AudioSource::process(StereoBlock, InputBlock)`.
  - Duplex `RtAudioOutput` that falls back to output-only.
  - The deterministic pump taking injected input, with loopback latency.
  - `SampleRing` and `TakeWriter`.
  - Monitoring.
  - Commit through the 2.2 API, compensated as in C21, using `AudioAssetCache::insert`.
  - Bounces suspend performance and use the input-free overload.
- **Tests:**
  - Design 4 tests 6–8: loopback L=256 places the recorded click within ±1 sample of the original, checked by bouncing and reading back; overflow is reported and the pump never stalls; monitoring is heard but not bounced.
  - An effects-PDC variant: a latent master insert with alignment still within ±1.
  - Realtime case `audio_input`.
  - `audio_duplex_device_check` (label `device`, skips with 77).
  - Gate part 3: an Audio In 1-2 take becomes a clip; one undo removes notes, lane and clip; `record_everything.png` is inspected.

**Size:** about 4,000 lines. Automation about 1,800, audio input about 1,400, cross-feature about 500, integration about 300. That is 4–5 days of wall time.

**Program total:** roughly 22,000–23,000 new lines plus the W0 moves. With disciplined parallelism that is about 17–23 days of wall time. Serialized merges and the full-suite rerun after every merge are a real part of that.

---

## 4. Definition of done for every item (on top of the item-specific tests)

1. `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build` and `ctest --test-dir build --output-on-failure` pass in the worktree, **after the final edit**, and pass again in the integration tree after the merge.
2. Any user-visible change has a `features/*.feature` scenario in Given/When/Then form, mapped to a named `reached()` label or core test.
3. Audio claims are proved from rendered audio through the production `process()` or the deterministic pump. Exports are read back and compared with the engine render. Mixer claims are proved from the bus.
4. Plugin claims use the real `.clap` fixture, the real `.vst3` bundle and a real SF2 through FluidSynth.
5. Each new panel or control passes a usable-size check. Each gate's PNG is opened and described in the hand-back.
6. Each regression fix shows its test failing before the fix.
7. Any change to `process()` adds a case to `blokkily_realtime_checks` that reports zero allocations.
8. The hand-back lists exactly what ran and what did not: the display check skips without an X server, and the duplex device check skips without an input device.

---

## 5. Open product questions (only those whose answer changes the design)

1. **What "the note merging fix" covers.** The code confirms that per-voice velocity is lost when a take merges into a step, and the chord plays at a hard-coded 0.8. Merged steps also take `max(duration)`, so each voice's held length is lost too. Should voices keep individual lengths? If so, `Chord` needs per-voice durations, which is a bigger model change than 1.6.
2. **What "records everything" includes.** Design 4 covers multi-track MIDI, on-screen surfaces, audio input and automation. Should it also record **outputs**, meaning track, bus or master resampling or stem capture into audio clips? That adds tap points after step 11 of the chunk order and changes 3.2.
3. **Arm semantics.** Does the per-track R arm notes, audio *and* automation together, or should automation have its own read/touch/latch mode?
4. **Nothing armed.** Should record with no armed track still record into the selected track, as today?
5. **Arm and input state in undo.** Proposed: persisted but excluded from undo restores.
6. **Overlapping audio clips and loop-recorded takes.** Should overlaps sum (v1), or should the newest clip cover the older one? This decides how per-pass takes are handled.
7. **Scope limits for audio.** Is it acceptable that audio clips are never time-stretched and are fully decoded into RAM, about 23 MB per stereo minute, with no disk streaming?
8. **Imported and recorded files.** Reference the original files or copy them into `<project>.audio/` straight away? Where do an unsaved project's takes live, and are unused takes deleted on undo or close?
9. **Editing a meter.** Should clips and tempo points keep their bar numbers (`rebar`) or their absolute ticks? Are pickup or partial bars needed?
10. **Returns and latency.** Are returns solo-safe? Should post-fader sends be post-pan? Is playback pre-rolled for plugin latency (PDC) in live play, or only compensated in the bounce?
11. **Automating effect parameters.** Should effect parameters be automatable, or pattern-lockable, in this program? That would pull 3.3 in scope and extend `ParameterLock` with a `ProcessorAddress`.
12. **Undo of plugin-window edits.** Should knob turns in a plugin's own window be undoable, one step per gesture? If so, restore by `load_state` while the plugin runs, or by replaying parameter values, which risks becoming a second copy of state?
13. **Display requirements.** Is it acceptable for `blokkily_scan` and `blokkily_cli` to link JUCE GUI code, subject to the 1.7 spike? Must CI provide Xvfb so the only real-pixel proof of plugin windows never skips?
14. **Metronome and count-in.** Are they in scope? Without them, recording against the running transport is awkward.

---

## 6. Main technical risks

- **Native plugin windows under the offscreen harness.**
  - Offscreen `winId`s are fake. The CLAP fixture only records them, but a real JUCE peer reparented onto a fake id would raise an X BadWindow error if `DISPLAY` is set. Refuse the embed under offscreen and unset `DISPLAY` in every offscreen gate.
  - The VST3 fixture statically links its **own** JUCE copy, so its message thread and singletons are separate from the host's. Edits arrive asynchronously, which is why the tests need bounded polling.
  - Nothing dispatches JUCE messages today. The `LinuxEventLoop` bridge is new code on the critical path.
  - Qt and JUCE keep two separate X connections, so focus and XEmbed behaviour may be flaky.
  - The session runs Wayland, so VST3 editors float through XWayland.
  - The real-pixel check skips without X. Spike 1.7 derisks linkage before 2.6 starts.
- **Audio-input determinism.** All proof goes through the pump with injected input and modelled loopback latency. RtAudio duplex on PipeWire may open at mismatched rates. `getStreamLatency()` is often wrong, hence `record_offset_samples`. The stream can only be reopened while stopped.
- **Realtime safety.**
  - Automation must never call `pow` or trig on the audio thread: breakpoints are precomputed, and dense ramps cost memory.
  - Built-in biquads recompute coefficients on the audio thread with `sin`/`cos` only.
  - Parameter and edit rings drop rather than block. A VST3 listener takes a mutex only on the producer side, never the consumer.
  - `shared_ptr` refcounts are never touched on the audio thread; clip regions and sampler kits hold raw pointers.
  - The stack event array must move to preallocated scratch (1.1) before fan-out, perform and automation raise the budgets.
  - Third-party plugins that allocate are outside our control. The allocation guard covers only our adapters, built-ins and fixtures.
- **Triple-buffered arrangement pressure.** Tempo, clip and automation drags can hit "no free arrangement slot". The coalescing in 1.1 is mandatory.
- **Rebuild glitches.** Adding or removing inserts still stops the stream. Adoption (1.1) reduces reloads but not the stop.
- **Wave 0 QML split and the `stepCount` change (2.1).** These are the largest regression surfaces. The existing gates and the baseline PNGs are the guard.
- **State loads while a plugin processes.** Undoing sampler or plugin-window state pushes state into a running plugin. That is allowed for the sampler (`accepts_state_while_running`) and needs a decision for CLAP and VST3 (Q12).
- **Parallel build load.** Six worktrees with JUCE, Qt and FluidSynth builds each. Cap concurrency and use ccache.