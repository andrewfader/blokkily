# Current limitations

Blokkily is experimental. These restrictions apply to the behaviour described
in the [usage guide](usage.md).

## MIDI and MPE

- VST3 instruments receive per-note pitch only within a two-semitone channel
  bend. SoundFonts have no CC 74 timbre response unless the SF2 maps it. The
  sampler has no timbre: per-note pitch and pressure reach it, and pressure
  (per-note or poly) swells the note by up to double.
- Following an external clock holds the song on the clock's beat by trimming
  its tempo (at most 2%), so a sudden tempo change is caught up over a beat or
  two rather than at once. A track played through a MIDI output sends no
  microtonal retuning.

MIDI file exchange also omits plugin state, parameter automation, per-note
expression, audio clips and microtonal retuning. Tempo ramps export as
sixteenth-note steps. See [MIDI file exchange](usage.md#midi-file-exchange).

## Audio and sampling

- MIDI takes are whole passes of the loop's range: a pass is chosen for a
  section of it by splitting the stack (SPLIT), not by editing notes across
  takes. Removing a take's clip keeps its (hidden) take section in the song.
- Disk streaming handles unwarped audio clips only. Warped clips decode into
  memory before stretching, and the sampler always decodes its samples.
- A sampler marker moves by whole pixels of the waveform as it is zoomed.

## Parameter locks and routing

- A step lock sets its parameter from that step on.
- Only the first auxiliary input of a CLAP or VST3 effect receives a sidechain
  key, as a stereo or mono signal. Other sidechain inputs receive silence.
- VST3 extra buses are used only with a stereo main bus.

## Modulation

- An LFO is evaluated every 16 samples, whatever the device's block size, and
  each value reaches its processor on its own sample; an LFO's steps (a
  square's turn, a saw's reset, a new sample-and-hold value) land on their
  own sample. A macro and a follower move once per 128-sample chunk.
- A follower targeting its own track, or a track that the render order cannot
  place after its source, receives the previous chunk's level.

## Arrangement loop

- Export always renders the whole arrangement. A loop is for playback; the
  bounce turns it off for that pass so the file is the song from its start.

## Scene launcher

- A scene's tempo takes effect when the scene is launched, not at the bar
  where its cells start; it is for playing live and is not printed into the
  arrangement with REC ARRANGEMENT.
- Export always renders the arrangement. Live launched tracks must be recorded
  into the arrangement to appear in an export.
- When a printed take leaves the tail of an earlier clip, that tail does not
  chase a controller value set before its trim.

## Track freeze

- A frozen track's render runs to the end of its tail, so a frozen song
  lasts until that tail ends. Notes and lanes edited while a track is frozen
  are heard only once it is unfrozen.

## Output recording

- Live output recording captures one bus per pass. A full set of stems is
  exported offline in one step instead: STEMS writes every track and return
  to a file of its own, from one render.
- A partial live pass ends when recording stops and does not automatically add
  effect tails. Offline stems include tails.
- Track stems exclude send returns. Track and return stems sum to the signal
  before master processing; a nonlinear master effect cannot be distributed
  among independent stems. Muting a source also stops its sends. See
  [export, resampling and stems](usage.md#export-resampling-and-stems).

## Staff notation

- The SCORE view draws one part at a time. Notes entered on its staff land
  on the step grid (sixteenths), up to a whole note long; tuplets, ties you
  draw yourself and finer rhythms are written in the piano roll. There is no
  full score of every part together yet.
- Lyrics are one verse: a note carries one syllable. Hebrew and other scripts
  are drawn correctly, but cantillation marks (te'amim) are not.
- Beams join eighths and shorter by the beat and are always flat; a beam is
  never broken across a beat or sloped to follow the melody. Spacing follows
  the rhythm but is not engraving-grade.
- Notation leaves out what it cannot write: timing off the 128th-note grid,
  probability, ratchets, loop conditions, parameter locks and chord strum.
  A MusicXML export reports when a part does not play back the same.
- Grace and cue notes in a MusicXML file are skipped.

## DAWproject

- Export only: a `.dawproject` from another host cannot be opened yet.
- Left out of an export, and named in its status: the scene launcher, the
  arpeggiator (notes go as written), modulation step locks, LFOs, macros and
  followers, clip gain, an audio clip's pitch shift, sidechain keys and
  instrument outputs routed to other tracks, and MIDI takes that are comped
  out (the take that plays is exported). SoundFont, sampler, LV2 and
  built-in processors go as placeholder devices other hosts cannot load.
- Controllers are written normalized to 0..1 (the pitch wheel centred at
  0.5). The format leaves the scale to the host, and this has not been
  checked against Bitwig, Studio One or Cubase.

## LV2

- LV2 plugins are hosted through JUCE. Their own windows open where JUCE can
  host the plugin's UI (an X11 UI on Linux); the suite's LV2 fixture has no
  window, so LV2 editors are not covered by the gates. Per-note expression
  reaches an LV2 plugin as MIDI, as it reaches a VST3.

## Plugin windows

Plugin editors embed in a window of the host's: an NSView on macOS, an HWND
on Windows, and an X11 window on Linux. On a Wayland desktop a CLAP plugin with
Wayland support embeds natively, and other editors embed through XWayland when
an X server is available; without one they are refused. Offscreen verification
checks the embedding lifecycle. Display checks read real editor pixels back on
an X server, under XWayland, and on macOS and Windows; they run with the
suite's own test plugins, and on macOS and Windows also with Dexed, at
standard pixel density only. See
[verification](verification.md) for display-check requirements and skips.

## Plugin isolation

- Windows and macOS builds load plugins inside Blokkily: isolation runs on
  Linux only. (On macOS a helper would also need Cocoa's event loop for
  plugin editors.)
- An isolated plugin is one audio period late (at least 512 samples); the
  mixer compensates it, but a key played live through it is heard that much
  later. An audio clip on a track fed by an isolated instrument's auxiliary
  output is not delayed to match that output.
- On Wayland an isolated plugin's editor opens in a window of its own rather
  than inside Blokkily's: a window of another process can be embedded only
  through X11 (XWayland).
- A helper whose plugin hangs is stopped after three seconds without a block;
  until then its track is silent.

