# Phase 2 Program Plan: Foundations to Professional Hybrid DAW

This plan defines the architectural specifications and execution phases for the five core initiatives advancing Blokkily to compete with Ableton Live and Bitwig Studio:

1. **Item 1: Disk Streaming & Buffered Audio Assets**
2. **Item 2: Full Continuous MIDI, CC, Pitch Bend, Sustain & MPE**
3. **Item 3: Universal Dynamic Modulation Engine**
4. **Item 4: Inter-Track Sidechaining & Multi-Output Plugin Routing**
5. **Item 5: Non-Linear Clip & Scene Launcher**

---

## Architecture & Dependency Flow

```
Wave 4: Expressive & Memory Foundations
├── Wave 4.1: Continuous MIDI (Pitch Bend, CCs, Sustain, MPE)
└── Wave 4.2: Disk Streaming & Lookahead Buffer Engine

Wave 5: Sound Design & Routing Architecture
├── Wave 5.1: Universal Dynamic Modulation Engine (LFOs, Envelopes, Macros)
└── Wave 5.2: Inter-Track Sidechaining & Multi-Output Plugin Routing

Wave 6: Performance & Workflow Paradigm
└── Wave 6.1: Non-Linear Clip / Scene Launcher Matrix
```

---

## Wave 4.1: Continuous MIDI, Controllers & MPE (Item 2)

### 1. Problem
[`TakeRecorder`](file:///home/andrew/workspace/blokkily/src/sequencer/take.cpp) captures notes, but pitch bend, sustain, mod wheel, CCs, and aftertouch are not yet recorded. Notes are timestamped to the callback block.

### 2. Design
* **Event Types:** Add `ContinuousEvent` to `Pattern`:
  * `enum class ContinuousType { PitchBend, Controller, ChannelPressure, PolyPressure };`
  * Values are normalized `float` or 14-bit unsigned integers.
* **MidiInput & TakeRecorder:**
  * Route incoming MIDI CC, Pitch Bend, and Channel Pressure events through `TakeRecorder`.
  * Support sustain pedal (CC 64) note-hold latching during live input.
* **Playback & Chasing:**
  * `EventTimeline` emits parameter changes and pitch bends at sample-accurate offsets.
  * Seeking invokes `chase()` to restore current controller values.

### 3. Status: done except MPE
* **Model.** `ContinuousEvent` (`include/blokkily/model/event.hpp`): pitch bend (14-bit), control change (0..119 except 64) or channel pressure, at a pattern tick, stored in the canonical `Pattern` (`add_continuous`, one value per controller per tick) beside the triggers; `with_length` keeps what still fits; `Song::consistent` checks them. Saved as additive `control` records (`src/project/records_continuous.cpp`); older files load with none. Poly pressure is played live but not stored.
* **Playback.** The scheduler emits them every loop (no probability or loop condition), `Song::arrange` offsets and cuts them, and the timeline turns them into `midi_raw` events at rank 0 (before a note on the same sample) on channel 1. The launcher's loops carry them too. Automation (`parameter_value`) and modulation (`parameter_modulation`) stay distinct from them.
* **Chase.** Every jump of the playhead (seek, wrap, transport start) marks each track; its next chunk first plays every controller its timeline moves at the value it had reached, or at the controller's rest value (wheel centred, pressure/mod wheel 0, expression 127) when nothing before moved it.
* **Recording.** `MidiInput` forwards the wheel, CCs and pressure as `midi_raw` to the routed tracks; the engine captures them with notes; the controller's take (`TakeRecorder::control`, `write_continuous`) writes them into the pattern under the playhead, one undo step with the notes.
* **Sustain.** CC 64 is handled at the input (released keys held until the pedal comes up) for every instrument, so takes record the heard lengths; the sampler also honours a CC 64 that reaches it as MIDI.
* **Instruments.** SoundFont: channel-wide messages go to all 16 FluidSynth channels (shared and retuned), poly pressure to the key's channel. VST3: channel one plus each retuned voice channel, the wheel added to the note's retune bend. CLAP: channel 0, where notes are sent. Sampler: wheel (±2 semitones) and pedal.
* **Interface.** The piano roll draws a read-only CTRL strip of the open pattern's movements.
* **Not done.** MPE; editing controller movements; recording poly pressure; sub-block timestamps for live input.

---

## Wave 4.2: Disk Streaming & Lookahead Buffer Engine (Item 1)

### 1. Problem
All audio files and clip waveforms are held fully in RAM via [`AudioAssetCache`](file:///home/andrew/workspace/blokkily/include/blokkily/audio/audio_asset.hpp). Large multi-track sessions exhaust memory.

### 2. Design
* **Reader Thread Pool:** Background worker pool pre-buffers audio blocks (64 KB–256 KB) from disk.
* **Lock-Free Ring Buffers:** Each streaming clip channel maintains a dual-buffer ring.
* **Audio Thread Safety:** The audio callback (`SongEngine::process_chunk`) only reads from pre-filled buffers. If starvation occurs, it softly fades out without blocking or allocating.

### 3. Status: done for unwarped audio clips
* **Where it applies.** `AudioAssetCache::load(..., Residency::stream_if_large)` (used by `load_clip_assets` and the import worker) returns a streamed asset (`AudioAsset::streamed`, no samples, frames and overview read through the stream) when the decoded size at the engine rate exceeds the threshold (default 128 MiB, `set_stream_threshold`, `BLOKKILY_STREAM_THRESHOLD_BYTES` in the app). Warped clips, tempo detection and the sampler decode into memory.
* **Engine.** `compile_clip_regions` gives each streamed clip a `DiskStream` from the engine's `StreamPool` (one per clip, kept across recompiles, owned by the arrangement slots that reference it, released on the control thread). The region applies gain and fades exactly as for a clip in memory. Streams are cued two seconds ahead of a clip's start, including across the song's wrap, and to the frame under a parked playhead.
* **Handshake.** The consumer (callback) owns the ring's read side and requests positions with one atomic word (generation + frame); the producer (`DiskStreamService` worker, or a blocking reader) seeks, publishes where that generation begins in the ring, and the consumer skips to it. The producer mutex is never taken by the live callback. `StreamReader` resamples with libsamplerate's best sinc, one mono converter per channel like `resample()`, starting a converter on an exactly aligned input frame 8192 frames before a seek target; it matches the in-memory resample to within 2e-5, and exactly at the engine's own rate.
* **Starvation.** 128-sample fades on a dry ring, on recovery and on relocation; the ring keeps 128 frames in reserve so a fade-out is always audio. A late stream skips ahead to stay in time with the song.
* **Export.** `bounce_song` sets `SongEngine::set_blocking_disk_reads`, so the bounce thread fills each ring before reading it: same engine path, never starved.
* **Not done.** A single clip that spans the song's wrap point relocates at the wrap (brief dip) because one stream cannot be in two places. Streaming is not used for warped clips.

---

## Wave 5.1: Universal Dynamic Modulation Engine (Item 3)

### 1. Problem
Blokkily has step parameter locks and static automation curves, but lacks real-time runtime modulators (LFOs, envelope followers, macro knobs).

### 2. Design
* **Modulation Layer:** Introduce `Modulator` interfaces:
  * `Lfo`: Sinusoidal, triangle, square, saw, and random S&H with tempo-sync and free-running rates.
  * `EnvelopeFollower`: Attack/release envelope detector driven by track audio.
  * `Macro`: 0.0–1.0 control assigned to multiple targets with min/max scaling.
* **Engine Injection:** `SongEngine` computes modulator frames per chunk and feeds them to `PluginInstance::process` via `PluginEvent::kind = modulation`.

### 3. Status: done (LFO, macro, envelope follower)
* **In the song, not the engine.** `Song::modulators` (`include/blokkily/model/modulation.hpp`) holds each modulator and its targets; they are saved (`modulator`/`modtarget` records, additive, older files load with none), validated by `Song::consistent` (ranges, targets on processors that exist, followers of tracks that exist, at most 32 modulators of 8 targets), kept pointing at the right processors when an insert, a return or a track is removed, and undone with the song.
* **Semantics.** A modulation event carries an offset: signal (LFO -1..+1, macro 0..1, follower 0..1) x target depth (-1..+1) x the parameter's range (max - min, read from the processor). Offsets on one parameter are summed into one `parameter_modulation` event, which stays distinct from automation (`parameter_value`).
* **Engine.** `src/audio/engine/engine_modulation.cpp`: routes are compiled with the arrangement and published through the same handoff as the timelines; rates, shapes, depths and macro values reach the callback as atomics from `apply_mix` (no recompile, no released notes). LFOs and macros are evaluated once per chunk at its first sample; each processor receives its modulation at offset 0 ahead of its other events, so events stay time-ordered. A follower reads its source track's buffer after that track's inserts and before its fader, as soon as it has rendered; the render order puts sources first. While the transport rolls an LFO's phase comes from the song position, so a bounce equals playback. A parameter whose modulation disappears is sent a last modulation of 0.
* **Interface.** The MODULATION panel adds LFOs, macros and followers, aims them from a menu of the selected track's parameters, and turns rate, shape, value and depth.
* **Not done.** Tempo-synced LFO rates exist in the model and engine (`sync_beats`) but have no control in the panel. Modulation is block-rate, not per-sample.

---

## Wave 5.2: Inter-Track Sidechaining & Multi-Output Routing (Item 4)

### 1. Problem
Audio flows strictly track-by-track. There is no sidechain key input into compressor inserts, and multi-out instruments cannot break out into discrete mixer channels.

### 2. Design
* **Sidechain Taps:** Allow plugins with `audio_inputs > 2` to define a sidechain source (track tap post-insert, pre-fader).
* **Multi-Out Channels:** Plugins declaring multiple stereo buses expose auxiliary channels directly in `MixerPanel`.
* **Topological Sort:** Tracks with sidechain dependencies are topologically sorted to ensure sources are rendered before consumers.

### 3. Status: done (sidechain keys for built-in and plugin inserts; multi-output instruments)
* **Sidechain.** `EffectSlot::sidechain` names the key track, saved (`sidechain` record), validated (no key from the insert's own track, no loops, no missing track) and fixed up when tracks are removed. The engine compiles keys and a render order (key, follower and instrument-output sources first) into the arrangement; the key is the source track's buffer after its inserts and before its fader, handed to the insert (`PluginInstance::set_sidechain`) as exactly the chunk's frames; built-in effects hand each render segment its own part of the key.
* **Plugin sidechain inputs.** The plugin boundary reports `PluginPorts::sidechain_inputs`. The CLAP adapter reads every audio port (`clap.audio-ports`): the port flagged main (or the first) takes the block, the first other input with channels is the sidechain and gets the key (silence without one), any further input silence; every port gets a buffer of its own, sized in `activate()`. The VST3 adapter asks JUCE for the main input stereo and bus 1 (the kAux input) stereo or mono beside it, and feeds that bus from the key in each parameter-split segment. The rack's SIDECHAIN line appears for the built-in compressor and for every insert whose processor declares a sidechain input (`AppController::publishProcessorPorts` after every rebuild). Proved from rendered audio through both real hosts with the suite-built fixtures, which duck their input by the key's level (`sidechain_clap_key`, `sidechain_vst3_key`, the export in `sidechain_plugin_key_bounce`, the GUI in `bdd_plugin_routing`, real-time rules in `realtime_plugin_routing`).
* **Multi-output instruments.** `Track::source` (`InstrumentOutput{track, output}`, output 1 = the first after the main one) makes a track the mixer channel of another track's instrument output. It has no instrument of its own; clips, input, inserts, fader, pan, mute, solo, sends and automation work on it as on any track. Saved as an additive `auxsource` record (older files load with none); `Song::consistent` refuses a missing or own source, output 0, an instrument on the channel, one output on two channels, and loops through keys and outputs; `Song::remove_track` re-indexes sources and turns the channel of a removed source into a plain track; undo is the song's checkpoint. The engine compiles `ArrangementRouting::feeds` and puts each source before its channel in the render order; before the source's instrument processes, each channel's buffer is cleared and handed to it through `PluginInstance::set_aux_output`, and the channel keeps what was written this chunk instead of starting silent. An output with no channel is rendered into scratch and dropped, never summed into the main output. CLAP: every non-main output port; VST3: every aux output bus JUCE can enable (stereo or mono) beside a stereo main output. The strip's + OUT chip (strips whose instrument declares aux outputs) makes the channel, one step of history (a rebuild, since the track list changes); fader, pan, mute and solo on the channel are live moves. Proved with the CLAP and VST3 synth fixtures, whose aux output is minus half their main output (`multiout_clap_multi_out`, `multiout_vst3_multi_out`, `multiout_multi_out_order`, the export in `multiout_multi_out_bounce`, the schema and records in `multiout_multi_out_song_model`, the GUI in `bdd_plugin_routing`).
* **Not done.** Only an effect's first auxiliary input is fed; an instrument with a sidechain input (a vocoder) is not keyed. VST3 aux buses are used only beside a stereo main bus. Soloing a source does not solo its channels (they are tracks like any other).

---

## Wave 6.1: Non-Linear Clip / Scene Launcher (Item 5)

### 1. Problem
Arranging music in trackers or linear arrangement timelines can hinder spontaneous jamming and iterative composition.

### 2. Design
* **Scene Grid:** Matrix of clip slots across tracks.
* **Launch Quantization:** Quantized launches on bar/beat boundaries via `TickClock`.
* **Follow Actions:** Configurable next actions (play next, random, repeat $N$).
* **Record to Arrangement:** Launcher playback prints clips directly into the linear arrangement timeline.

### 3. Status: done (grid, quantized launch, follow actions, arrangement recording, LAUNCH view)
* **In the song.** `Song::launcher` (`include/blokkily/model/scene_launcher.hpp`) holds the scenes, each cell naming a pattern by index with its loops, follow action and launch quantization, and the grid's own quantization for scene launches and stops. Saved as `launcher`/`scene`/`sceneslot` records with escaped names and range-checked values; older files (unescaped names, a scene tempo after the name) still load. `Song::consistent` refuses a cell naming a missing pattern; deleting a pattern empties its cells and moves later ones down (`SceneMatrix::remove_pattern`), removing a track removes its column.
* **Engine.** `src/audio/engine/engine_launcher.cpp`: the grid and every used pattern's loops are compiled with the arrangement and published through the same handoff. Launch and stop commands cross to the callback on a lock-free queue; each track's status comes back as one atomic word; takes come back on a second queue. The launcher counts session ticks that run on through song wraps and seeks, so a launched loop keeps its phase. Every quantized launch or stop and every loop turn is a chunk boundary: it falls on a chunk's first sample, and a transition releases what the old cell held there. A launched track plays its cell instead of its arrangement (whose notes are released as it takes over); stopped in the launcher it stays silent until the transport stops. Stopping the transport stops every launched track; a bounce resets the launcher (an export is the arrangement). The random follow action draws from a generator seeded by `prepare()`.
* **Record to arrangement.** A take is one stretch a track played from one cell. `Song::print_take` prints it as clips of the pattern, split into runs of the loop cycle the launcher compiled (1 for a pattern whose loops are alike, the loop conditions' least common multiple, or 16 to 64 with probability) so each clip's loops number as the launcher's did, the last cut where the take ended (`Clip::length`, a new optional sixth field of the `clip` record). The printed arrangement plays and exports the launched performance sample for sample (`scene_launcher_record_prints_arrangement`). The controller prints each take as it arrives, one step of undo each.
* **Interface.** The LAUNCH view (view switcher) shows the grid: clicking an empty cell fills it with the open pattern, a filled cell launches, a scene chip launches the row, stop chips stop a track or all; a right click selects a cell for its pattern, loops, follow action and quantization pickers; REC ARRANGEMENT switches recording.
* **Not done.** Scenes have no tempo (the field was removed; the song's tempo map is the only tempo). Launched tracks stop on an engine rebuild. A take printed over a clip that ran past its end cuts that clip where the take began, losing its tail. A pattern whose loop-condition cycle exceeds 64 loops repeats its launched loops every 64.
