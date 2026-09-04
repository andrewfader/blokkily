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
- loads a real SF2 through FluidSynth and verifies that middle C produces
  non-silent stereo audio
- proves a parameter lock on a step actually changes what that step sounds
  like, with automation and modulation arriving as distinct events
- saves the session to `build/artifacts/verification.blok`, disturbs the live
  pattern, the arrangement, and the mixer, reloads from that file, and confirms
  every disturbance is undone: the pattern, the clip count, the track count,
  the muted strip, and its -7.5 dB fader all come back with their instruments
- proves the arrangement is a real editor over the song: the timeline lanes
  report the clips the song holds, clicking a bar places and removes a clip of
  the pattern currently open, and switching patterns moves every editor
- proves the mixer moves audio and not only colours: with both tracks open each
  contributes its own level, muting one removes it from the bus, and soloing
  the other leaves only itself, measured at a sample position where the track
  under test actually sounds
- bounces the arrangement to `build/artifacts/arrangement-bounce.wav` through
  the same engine the speakers hear, then reads the file back and checks it is
  stereo, at the session rate, as long as the song plus its tail, and audible
- checks the playhead is the engine's own position: 96000 samples at 120 BPM
  reads as bar 2, and the interface does not run a second clock beside it
- checks panels are usable and not merely present: the tracker and the piano
  roll must have real width and height, so a neighbour cannot squeeze one to
  nothing while it still reports itself visible
- renders the shared pattern through the production real-time callback path
- renders the real QML scene and writes:

`build/artifacts/edit-once-see-everywhere.png`

`blokkily --verify --view STEP|TRACKER|PIANO --screenshot <path>` captures a
focused editor instead of the combined layout, so each view can be inspected on
its own after an interface change. `--export <path>` runs the bounce gate and
writes the audio it checked.

The unit and integration suite additionally covers the layer underneath: the
decibel and constant-power pan maths, mute and solo semantics, clip expansion
over the timeline including per-repetition loop conditions, the multi-track
engine, and the wave writer at every supported depth. Its audible evidence is
`build/artifacts/mixdown.wav` and `build/artifacts/mixdown-16.wav`.

Open and inspect that image after UI changes. A valid exit status alone does
not prove that layout, contrast, text, and editor projections are usable.
