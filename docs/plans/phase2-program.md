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

---

## Wave 4.2: Disk Streaming & Lookahead Buffer Engine (Item 1)

### 1. Problem
All audio files and clip waveforms are held fully in RAM via [`AudioAssetCache`](file:///home/andrew/workspace/blokkily/include/blokkily/audio/audio_asset.hpp). Large multi-track sessions exhaust memory.

### 2. Design
* **Reader Thread Pool:** Background worker pool pre-buffers audio blocks (64 KB–256 KB) from disk.
* **Lock-Free Ring Buffers:** Each streaming clip channel maintains a dual-buffer ring.
* **Audio Thread Safety:** The audio callback (`SongEngine::process_chunk`) only reads from pre-filled buffers. If starvation occurs, it softly fades out without blocking or allocating.

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

---

## Wave 5.2: Inter-Track Sidechaining & Multi-Output Routing (Item 4)

### 1. Problem
Audio flows strictly track-by-track. There is no sidechain key input into compressor inserts, and multi-out instruments cannot break out into discrete mixer channels.

### 2. Design
* **Sidechain Taps:** Allow plugins with `audio_inputs > 2` to define a sidechain source (track tap post-insert, pre-fader).
* **Multi-Out Channels:** Plugins declaring multiple stereo buses expose auxiliary channels directly in `MixerPanel`.
* **Topological Sort:** Tracks with sidechain dependencies are topologically sorted to ensure sources are rendered before consumers.

---

## Wave 6.1: Non-Linear Clip / Scene Launcher (Item 5)

### 1. Problem
Arranging music in trackers or linear arrangement timelines can hinder spontaneous jamming and iterative composition.

### 2. Design
* **Scene Grid:** Matrix of clip slots across tracks.
* **Launch Quantization:** Quantized launches on bar/beat boundaries via `TickClock`.
* **Follow Actions:** Configurable next actions (play next, random, repeat $N$).
* **Record to Arrangement:** Launcher playback prints clips directly into the linear arrangement timeline.
