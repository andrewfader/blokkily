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

Editing while the song plays does not interrupt it. An edit recompiles the
arrangement and hands it to the render callback at the next block boundary,
without allocating on the audio thread, rebuilding the graph, reloading the
instrument, or moving the playhead; a note whose step is erased while it is
sounding is released rather than left ringing. A note entered in any editor is
auditioned through the running engine, on a stopped transport as well as a
playing one, and a session that opens with no instrument is given the machine's
General MIDI SoundFont — the percussion bank on a drum track — so the
workstation makes a sound the moment it opens.

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

A session also says which tuning and which scale it is written in. Tunings are
held in cents — equal divisions of any period, rational and historical tunings,
and Scala-style lists — so twelve equal semitones are one tuning among many. A
note carries the twelve-tone key its instrument is told plus the retune away
from it, and so does each voice of a chord, which is how a nineteen-tone or
quarter-tone pattern reaches CLAP, VST3, and the SoundFont synth as the pitch it
was written at rather than as the nearest approximation. Four playable surfaces
project the same tuning and scale: a piano keyboard, isomorphic grids
(Wicki-Hayden, Jankó, harmonic table, fourths), fretboards for nine string
tunings, and chord pads that name each harmony by its numeral and stack a row
per inversion. Playing one auditions it through the selected track's instrument
even on a stopped transport, and writes it onto the selected step of the
canonical pattern, where every editor names it in the song's own tuning.

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
and parameter locks with an automation/modulation switch. Every editor is an
input rather than a read-out: clicking a step toggles it, pressing a lane of the
piano roll writes that pitch onto the column under the pointer and dragging
draws a phrase across lanes, the tracker's note column takes a click and a drag,
and its rows are typed the way a tracker has always been typed — ZSXDCVGBHNJM
for one octave and the two rows above for the next, with the entry octave on
screen and moved by Ctrl+Left and Ctrl+Right, and Backspace clearing a row.
Whichever one is used, the edit lands on the one canonical pattern, so it shows
at once in every other editor, in the inspector, and in what the song plays; the
view switcher focuses one editor or shows them all.
Above the editors is the arrangement: one lane per track, one cell per bar, and
a click places or removes a clip of whichever pattern is open. Down the right
is the mixer, one strip per track plus the master. The playhead is the audio
engine's own position, read in bar.beat.sixteenth across the whole song.
Below the editors is the keyboard: the session's tuning, scale, and root, a
switch between the piano, the isomorphic grid, the fretboard and the chord pads,
and an auto-scale toggle that snaps a played key onto the notes of the scale.
Space plays, the arrow keys move and transpose the step cursor, Ctrl+E bounces
the arrangement to disk.
Its plugin browser scans the platform's standard CLAP and VST3 locations and
lists what it finds through one shared plugin boundary, tagging each entry with
the format that produced it. An installation of that size is browsed by typing
rather than by scrolling: Ctrl+F reaches the browser's field, a few letters of
a name, a maker or a format narrow the list to the instruments those letters
reach in order — "fbs" finds "Fat Bass" — the arrow keys and Return move
through them and load one, and Escape lists everything again. `./build/blokkily_cli` runs the headless scheduler
demo.

Behavior specifications are in `features/`, and repository-wide completion
rules are in `AGENTS.md`. See `docs/verification.md` for the repeatable BDD,
integration, end-to-end, and screenshot verification workflow.

## Architecture

- `blokkily_core`: canonical project/event model and deterministic scheduler
- `Tuning`/`Scale`/`KeyboardSpec`: cents-based tunings, scales, and the playable
  surfaces projected from them
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
