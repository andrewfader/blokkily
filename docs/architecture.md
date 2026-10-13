# Architecture

Blokkily keeps the musical model independent of the interface framework. The
tracker, piano roll and step grid project the same canonical musical data.
A song owns its named sections, their per-track pattern parts, tracks, mixer
settings and arrangement clips.

The interface calls a section such as VERSE or CHORUS a pattern. In the model,
a `Section` is a named entry, and each `PatternSlot` holds one track's `Pattern`
of events and records the section and track it belongs to. A song loaded
without sections gets them from `Song::adopt_sections()`. Selecting a track
changes which part the editors show.
A clip places one part on one track; placing a whole section creates clips
across the tracks that have room for them.

## Main components

| Component | Responsibility |
| --- | --- |
| `blokkily_core` | Canonical project/event model and deterministic scheduler |
| `Tuning`/`Scale`/`KeyboardSpec` | Cents-based tunings, scales and playable surfaces |
| `Song` | Sections and per-track pattern parts, mixer tracks and arrangement clips |
| `SongEngine` | Allocation-free multi-track playback through one mixer bus |
| `PluginInstance` | Format-neutral real-time plugin boundary, preserving modulation |
| `MidiInput` | RtMidi decoding, tuning and routing through a lock-free queue; deterministic input mode for tests |
| `TakeRecorder` | Writes the input heard by the engine into the pattern under the playhead |

Other components include the Qt Quick editors and plugin browser, an RtAudio
hardware-output adapter, offline bounce and a WAVE writer, plugin adapters,
and a versioned project format. A Tracktion playback/recording adapter is the
next planned engine layer.

The canonical model does not use Tracktion MIDI clips. Adapters compile it
into engine and plugin events, preserving sequencer semantics such as semantic
chords, inversions, strum, microtiming, probability, ratchets, loop conditions
and parameter locks.

## Scheduling and real-time audio

The transport compiles pattern ticks into sample events, loops patterns and
drives instruments through the plugin boundary. RtAudio uses a low-latency,
non-interleaved stereo callback. The engine renders at the audio device's
negotiated rate, so a device fixed at 44.1 kHz plays at the correct pitch and
tempo.

The song engine compiles each track's whole timeline and renders without
allocating, locking or doing I/O. Each clip repetition advances the pattern's
loop count. Playback splits dense timelines at event boundaries so large
device buffers do not drop notes or releases. Each track supports up to 256
simultaneous scheduled events at one sample. An edit that exceeds this limit
reports an error and preserves the last playable arrangement.

An edit recompiles the arrangement and hands it to the callback at the next
block boundary. This does not allocate on the audio thread, rebuild the graph,
reload instruments or move the playhead. Erasing a sounding note releases it.
Mixer changes reach the engine without rebuilding the audio graph.

The mixer sums tracks with gain in decibels, constant-power pan, mute, solo and
peak metering through a master fader. Offline bounce uses the playback engine
and writes 16-bit, 24-bit or 32-bit float WAVE. The float writer preserves the
rendered samples; integer writers quantize them and clamp to full scale. Tests
compare float exports with deterministic playback renders sample by sample
and check integer exports within quantization tolerances. A bounce first resets
instruments, effects, LFOs and followers to rest, so it matches a pass played
from the start; plugin state that a reset does not clear can still differ.

MIDI input arrives on the port's thread and reaches the render callback through
a lock-free queue, avoiding delays from a busy interface. The recorder uses
the position at which the engine heard the input. Offline bounce excludes live
keyboard input and recording.

## Plugin adapters

Format-specific code stays behind the shared real-time boundary. Parameter
locks compile into sample-accurate parameter events. Automation sets a
parameter's value; modulation offsets it. Both remain distinct and can apply
together.

### CLAP

CLAP is a required native format. The catalog loads modules, initializes entry
points, queries factories and records plugin descriptors and features. The
instance adapter handles creation, initialization, activation, sample-accurate
note and parameter events, stereo processing and state streams.

### VST3

The JUCE-backed adapter scans bundles and default system locations and creates
instances through the shared boundary. It splits processing blocks at parameter
offsets for sample-accurate automation. Both the CLAP and VST3 adapters apply
scheduled parameter locks and support native plugin windows.

### SoundFont

The internal FluidSynth adapter loads real SF2/SF3 files, accepts sample-offset
note events, renders stereo audio, selects presets and persists project state.
It also supports offline rendering.

FluidSynth starts a voice only at the start of one of its 64-frame blocks. The
adapter keeps a small pool of renderers sharing the bank's sample data. Each
note uses a renderer whose block starts on that note's frame. Pattern passes
therefore stay in phase regardless of their length in samples. A bank's chorus
keeps its own clock, like a free-running LFO.

## Audio clip streaming

Large unwarped clips use a background worker to fill one lock-free ring per
clip. The worker resamples to the engine rate with the same converter as the
in-memory path; the callback only reads the ring. Export reads streamed clips
with blocking I/O on its own thread. Tests compare its rendered samples with
streamed playback when the data is available; export does not reproduce live
underruns. The default threshold and restrictions are documented in the
[usage guide](usage.md#audio-clips-and-files) and
[limitations](limitations.md#audio-and-sampling).

## Project state

The versioned project file stores the whole song: every named pattern with its
chords, locks, ratchets, microtiming and loop conditions; tracks, channel strips
and clips; and the opaque state of every CLAP, VST3 and SoundFont instrument
and effect. Editors and mixer controls operate on that state rather than
keeping separate copies.

## Tests and coverage

Behaviour specifications live in `features/`. See
[verification](verification.md) for the BDD, integration, end-to-end and
screenshot workflow, and [AGENTS.md](../AGENTS.md) for completion rules.

For measured C++ line coverage with GCC, configure a separate build with
`cmake -S . -B build-coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBLOKKILY_COVERAGE=ON`,
then run `cmake --build build-coverage`,
`ctest --test-dir build-coverage --output-on-failure`, and
`python3 scripts/coverage.py build-coverage`.
The report and uncovered line numbers are written to `build-coverage/artifacts/coverage/`.
Vendored frameworks, generated code, QML and the embedded GUI verification
driver are excluded from the percentage.
