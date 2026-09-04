# Blokkily

Blokkily is an experimental pattern-first DAW. A single event model drives three
synchronized editors — a tracker, a piano roll, and a step grid — which are in
turn one lane of a multi-track song. CLAP is a required native plugin format,
not a compatibility add-on.

The framework-independent musical model and the real-time plugin boundary sit
underneath all of it. The model covers notes, semantic chords, inversions,
strum, microtiming, probability, ratchets, loop conditions, and parameter locks.
It also includes a native CLAP catalog that loads CLAP modules, initializes their
entry points, queries factories, and records plugin descriptors and features.
The CLAP instance adapter implements creation, initialization, activation,
sample-accurate note/parameter events, stereo processing, and state streams.
The JUCE-backed VST3 adapter scans bundles and default system locations and
instantiates plugins through the same real-time boundary, splitting each block
at parameter offsets so VST3 automation is sample-accurate too.
Parameter locks are compiled by the scheduler into sample-accurate parameter
events and applied by both plugin adapters; automation sets a parameter's value
while modulation offsets it, and neither erases the other.
The internal FluidSynth adapter loads SF2/SF3 files, accepts sample-offset note
events, renders stereo audio, selects presets, and persists its project state.
Projects save and reload the whole session: every named pattern with its chords,
locks, ratchets, microtiming, and loop conditions, the tracks with their channel
strips and clips, and the opaque state of every CLAP, VST3, and SoundFont
instrument.
The allocation-free real-time transport compiles pattern ticks to sample events,
loops patterns, drives either instrument adapter, and feeds a low-latency
non-interleaved stereo RtAudio callback.

A session is a song rather than a single pattern. Named patterns are placed as
clips on the timeline of mixer tracks, each track carrying its own instrument
and channel strip: gain in decibels, constant-power pan, mute, solo, and a peak
meter, summed through a master fader. The song engine compiles every track's
whole timeline once and then renders it without allocating, locking, or doing
I/O; each repetition of a clip counts as the next loop of its pattern, so
probability and loop conditions keep working across an arrangement. Mixer moves
reach the running engine, so a fader can be pulled while the song plays. The
same engine bounces the arrangement to a 16-bit, 24-bit, or 32-bit float WAVE
file, sample for sample identical to what was auditioned.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/blokkily
```

The main executable is a Qt Quick workstation with synchronized step-grid,
tracker, and piano-roll projections over one canonical pattern, a shared
playhead, and a step inspector that exposes the model's full expression:
transposition, velocity, micro-timing, probability, ratchets, loop conditions,
and parameter locks with an automation/modulation switch. Clicking a step
updates every view; the view switcher focuses one editor or shows them all.
Above the editors is the arrangement: one lane per track, one cell per bar, and
a click places or removes a clip of whichever pattern is open. Down the right
is the mixer, one strip per track plus the master. The playhead is the audio
engine's own position, read in bar.beat.sixteenth across the whole song.
Space plays, the arrow keys move and transpose the step cursor, Ctrl+E bounces
the arrangement to disk.
Its plugin browser scans the platform's standard CLAP and VST3 locations and
lists what it finds through one shared plugin boundary, tagging each entry with
the format that produced it. `./build/blokkily_cli` runs the headless scheduler
demo.

Behavior specifications are in `features/`, and repository-wide completion
rules are in `AGENTS.md`. See `docs/verification.md` for the repeatable BDD,
integration, end-to-end, and screenshot verification workflow.

## Architecture

- `blokkily_core`: canonical project/event model and deterministic scheduler
- `Song`: named patterns, mixer tracks, and the clips that arrange them
- `SongEngine`: allocation-free multi-track playback through one mixer bus
- Offline bounce and a WAVE writer at 16-bit, 24-bit, and 32-bit float
- `PluginInstance`: format-neutral real-time boundary preserving modulation
- Qt Quick prototype: synchronized custom editors and CLAP/VST3 plugin browser
- FluidSynth internal instrument: real SF2/SF3 loading and offline rendering
- Real-time transport and RtAudio hardware-output adapter
- Next engine layer: Tracktion playback/recording adapter
- CLAP host adapter: discovery, lifecycle, processing, and state
- Versioned project file: the whole song plus per-instrument plugin state
- Next CLAP layer: native plugin GUI embedding and broader port configurations
- Additional adapter: VST3 through JUCE

The canonical model deliberately does not use Tracktion MIDI clips. Adapters
compile it into engine and plugin events, preserving richer sequencer semantics.
