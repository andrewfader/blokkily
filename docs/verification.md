# Verification

The acceptance specifications live in `features/`. CTest is the executable
source of truth and labels checks by scope.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -L bdd --output-on-failure
ctest --test-dir build -L "mixer|song|export" --output-on-failure
```

When the end-to-end gate fails it names the scenario group it failed in
(`BDD FAIL at: mixer`), so a regression points at the behaviour that broke.
`BLOKKILY_TRACE=1` prints every scenario group as it is reached, so a gate that
hangs says where.

The verification driver itself is worth running under AddressSanitizer after
changing it: its scenarios run from the event loop and reach back into the
block that set them up, and a helper that has gone out of scope reads dead
stack rather than failing cleanly.

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan -L bdd --output-on-failure
```

The end-to-end scenario runs the actual application with Qt's offscreen
platform and, in one process, exercises the whole stack:

- modifies the shared pattern model and reads the *rendered* step grid back, so
  a projection that stops following the canonical pattern fails the gate
- drives the real interface: the view switcher must actually show and hide the
  tracker and the piano roll, the inspector must name the selected step and
  follow a transposition, and the shared playhead must land on the right step
  and wrap at the end of the bar and again at the end of the song
- scans a dynamically loaded CLAP fixture, then instantiates and activates the
  CLAP synth, proves its output begins at the requested sample offset, and
  round-trips its state through CLAP streams
- scans the VST3 bundle the suite builds, instantiates it through the JUCE host
  adapter, proves its output begins at sample 32, and round-trips its state
- checks that the plugin browser lists both formats in one list, each entry
  carrying a format tag, a name, and a vendor
- searches that browser through its own rendered field: typing a few letters
  narrows the list to the instruments those letters reach, a row of the
  filtered list still points at the instrument it names, the arrow keys walk
  the list without leaving the field, and clearing it lists everything again
- loads a real SF2 through FluidSynth and verifies that middle C produces
  non-silent stereo audio
- proves a parameter lock on a step actually changes what that step sounds
  like, with automation and modulation arriving as distinct events
- saves the session to `build/artifacts/verification.blok`, disturbs the live
  pattern, the arrangement, and the mixer, reloads from that file, and confirms
  every disturbance is undone: the pattern, the clip count, the track count,
  the muted strip, and its -7.5 dB fader all come back with their instruments
- proves the arrangement is a real editor over the song: the timeline lanes
  report the clips the song holds, clicking an empty bar places a clip of the
  pattern currently open, right-clicking a clip removes it, clicking a filled
  clip opens that pattern in every editor and seeks the engine there without
  removing the clip, lengthening a clip to two bars keeps one clip with
  `repeats=2` rather than placing a second clip, moving a clip frees the bars
  it left, clicking the ruler locates the audio engine on that bar, and
  switching patterns moves every editor
- proves the mixer moves audio and not only colours: with both tracks open each
  contributes its own level, muting one removes it from the bus, and soloing
  the other leaves only itself, measured at a sample position where the track
  under test actually sounds
- bounces the arrangement to `build/artifacts/arrangement-bounce.wav` through
  the same engine the speakers hear, then reads the file back and checks it is
  stereo, at the session rate, as long as the song plus its tail, and audible
- checks the playhead is the engine's own position: 96000 samples at 120 BPM
  reads as bar 2, and the interface does not run a second clock beside it
- proves the keyboard is a projection of the session's tuning and scale: the
  piano surface offers one key per degree of the tuning and grows when the
  tuning divides the octave more finely, each key reports the retune its
  instrument will be told, the scale marks its own degrees and its root on
  every octave without shortening the keyboard, auto-scale plays the nearest
  degree of the scale and playing with it off does not, a played key is written
  onto the selected step and named the same way by the step grid, the tracker
  and the inspector, a press is *heard* — measured as non-silent audio from the
  engine with the transport stopped — a quarter-tone degree survives being
  saved and reloaded with its retune intact, switching surface changes the
  layout and not the pattern, the tuning and scale pickers are populated from
  the model and choosing an entry reaches the session, a note is released on the
  track that was told to sound it rather than on whichever one is selected next,
  and a chord pad writes one chord event that the scheduler expands into the
  voices the pad named
- proves the tracker and the piano roll are inputs and not read-outs: a press
  on a lane of the *rendered* roll writes that lane's pitch onto the column it
  landed in, the right button erases it, dragging a drawn note's right edge
  lengthens it (the scheduler sounds that duration, the roll draws a wider
  block, the tracker keeps the note on the row it started on), dragging the
  body of a note moves it in time and pitch, a click on the tracker's note
  column writes the entry octave's root, typing X on a row writes D and moves
  the cursor on, dragging the VEL column changes how loud the row is, Ctrl+C
  and Ctrl+V copy a step with its length, velocity and lock, Ctrl+D duplicates
  it onto the next row, Insert pushes later rows down and Shift+Backspace pulls
  them up, dragging the FX column writes a parameter lock and a right-click
  clears it, Alt+arrows nudge micro-timing and note length, Ctrl+digit toggles
  that numbered step, Ctrl-click on
  the step grid seeks the engine into that column, and a press on
  the roll's keyboard gutter is heard until it is let go — each checked in the
  step grid, the tracker's own columns and the roll's drawn note, so an editor
  that stops turning input into an edit, or a projection that stops following
  it, fails the gate
- proves the session opens with something to hear: every track without an
  instrument is given a General MIDI bank, a track named DRUMS is given the
  percussion bank, a session that already carries instruments is left alone,
  and the resulting engine is *rendered* and must not be silent
- proves the session can be worked on (`features/session_workflow.feature`):
  a click on the rendered grid writes in the tracker's octave, Ctrl+Z and
  Ctrl+Shift+Z sent to the window take it back and put it back, a stroke
  dragged across the rendered roll is one undo, the roll's playhead is drawn in
  bar two, a chord is drawn as each of its voices, a pattern is duplicated,
  renamed, cleared and deleted with its clips, +TRK adds a track that renders
  audible sound, a track is renamed by typing into the rendered field and
  pressing Return, a strip's meter lights and goes dark with the engine's
  levels, nine tracks scroll in the mixer with the master still inside the
  window, the arrow keys stay with a focused text field, Return and Home rewind
  the engine's own playhead, Ctrl+M and Ctrl+L mute and solo the selected
  track, a held key still sounds after 700 ms and stops on
  release, saving makes the session clean and an edit makes the title show it,
  a device that negotiates 44.1 kHz gets an engine prepared at 44.1 kHz, and a
  new session is one empty pattern with no history
- checks panels are usable and not merely present: the tracker and the piano
  roll must have real width and height, so a neighbour cannot squeeze one to
  nothing while it still reports itself visible
- renders the shared pattern through the production real-time callback path
- renders the real QML scene and writes:

`build/artifacts/edit-once-see-everywhere.png`

The keyboard gates render the same application with one surface in view and
write:

`build/artifacts/playable-keyboards.png` — a Wicki-Hayden grid in 19-EDO, every
cell naming its degree and its retune

`build/artifacts/chord-pads.png` — Maqam Rast in 24-EDO, one column per chord of
the scale and one row per inversion

`build/artifacts/midi-recording.png` — from the `bdd_midi_recording` gate
(`blokkily --verify --scenario midi`), which runs only the MIDI scenarios of
`features/midi_input_and_recording.feature`: it chooses the port by clicking
the rendered MIDI IN panel and its menu, plays keys through a deterministic
MIDI input that runs the same decode, tuning and routing path a port's thread
does, and hears them through the production render callback on a stopped song
and on the selected track only. It arms recording from the rendered button,
holds a key across part of an empty step while the song runs, and checks —
while the song is still playing — that the step holds that key with the
micro-timing and length the key was played at, in the step grid, the tracker
and the piano roll; that the arrangement then sounds on that step with no key
down where before it was silent; that one undo takes the take back; that
stopping with a key held writes it released there; that an unarmed song records
nothing; and that in 19-EDO a recorded key keeps the degree and retune it was
heard at. It then slows the song to 60 BPM from bar 2 (`features/timebase.feature`)
and checks that a key played into bar 2 is written on the tick the tempo map
puts it at, that the readouts show 60.00 BPM and bar 2, that a 7/8 bar 2 moves
the next clip to tick 3600 while it keeps its bar number and seeks the engine
to that tick's sample, and that undo restores both. The screenshot shows the
take in every editor, recording armed, the panel naming the port and the last
key, and the transport at 2.1.1 reading 60.00 BPM.

The timebase itself (`blokkily_timebase_tests`, tests `timebase_*`, and the
`realtime_tempo_edit_keeps_bar` regression) is proved from audio rendered through
the production callback: CLAP DC onsets across a tempo step, a ramp and a 7/8
bar at hand-computed samples, a bounce across a tempo change read back and
compared sample by sample with the live render, a recording across a tempo
change, and non-silent SoundFont and VST3 notes placed by the tempo map.

`build/artifacts/chord-velocity.png` — from the `bdd_chord_velocity` gate
(`blokkily --verify --scenario chords`), which runs the interface scenarios of
`features/chord_velocity.feature`: two keys are recorded onto one empty step at
velocities 40 and 120, the softer let go first. The step must become a chord
that keeps both velocities and both held lengths, heard through the production
callback on the CLAP fixture's velocity mode at 0.25 × (40 + 120) / 127.
Dragging the first voice's bar in the rendered inspector to the top must raise
that voice alone, heard at 0.25 × (1 + 120 / 127); undo and redo are one step
each, and the chord survives a save and a load. The screenshot shows the chord
selected: the tracker's VEL column at the loudest voice, the piano roll drawing
each voice at its own length, and the inspector's VOICE VELOCITY bars.

`build/artifacts/record-everything.png` — from the `bdd_record_everything` gate
(`blokkily --verify --scenario record`), part 1 of
`features/record_everything.feature`: two tracks play the CLAP fixture, one
panned hard left and one hard right, and are armed by clicking the R on their
mixer strips (each at least 18 px square; arming adds no step of history). With
the song recording, a key of the rendered piano is clicked and held and a
tracker note key is typed and held: each must be heard at exactly 0.25 on both
the left and the right half of the production callback's output, and each must
land in both tracks' patterns on the step nearest where it was heard, with the
micro-timing it was heard at, while the tracker cursor stays on its empty step.
Stopped, the same key writes the cursor's step and advances. Undo and redo take
the step and the take back and forth without touching the arm, and arm and
channel survive a save and a load. The screenshot shows both strips armed (the
second on CH 2) and the take in the step grid, the tracker and the piano roll.
The routing underneath (`blokkily_record_tests`, tests `record_*`, and
`realtime_record_fanout` with 64 armed tracks) is proved from the left and right
energy of the rendered bus.

`midi_device_input` opens a virtual port through the system's MIDI server — the
way a controller's driver presents one — connects the production input to it by
name, and fails unless a note sent there is heard through the render callback
and, with the song armed and playing, captured with its position. It skips
itself (exit 77) where there is no MIDI server.

`blokkily --verify --view STEP|TRACKER|PIANO|KEYS --screenshot <path>` captures
a focused editor instead of the combined layout, so each view can be inspected
on its own after an interface change. `--tuning`, `--scale` and `--surface`
leave the session in a named state before the capture, which is how the two
keyboard gates photograph a nineteen-tone isomorphic grid and quarter-tone chord
pads. `--export <path>` runs the bounce gate and writes the audio it checked.

The unit and integration suite additionally covers the layer underneath: the
decibel and constant-power pan maths, mute and solo semantics, clip expansion
over the timeline including per-repetition loop conditions, the multi-track
engine, block-size independence in the render path, recompiling an arrangement
underneath a running playhead — an edit made mid-playback is heard on the next
block without moving the playhead, without rebuilding the instruments, and
without leaving an erased note ringing — the wave writer at every
supported depth, and the tuning layer: equal divisions of any period, Scala
cents and ratio lists, scale membership and snapping in twelve and nineteen
tones, chord spelling and inversion, the cells each playable surface produces,
and a chord whose voices are retuned one by one through the scheduler and the
project file. Its audible evidence is
`build/artifacts/mixdown.wav` and `build/artifacts/mixdown-16.wav`.

Open and inspect those images after UI changes. A valid exit status alone does
not prove that layout, contrast, text, and editor projections are usable.
