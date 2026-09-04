# Agent verification contract

These rules apply to every change in this repository.

## Definition of done

An agent may say a change is complete only after all applicable checks below
pass in the same working tree that it hands back. Never claim that something
was verified merely because it compiled or looked correct in source.

1. Configure and compile from the repository root:
   `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
2. Run the complete suite:
   `ctest --test-dir build --output-on-failure`
3. For any GUI, workflow, model-projection, or plugin-browser change, run the
   `bdd`, `e2e`, `integration`, and `screenshot` labeled test. Inspect the PNG
   with an image-viewing tool; checking only that the file exists is forbidden.
4. Repeat the complete suite after the final edit. A test run made before the
   final edit does not count.
5. Report exactly which checks ran and disclose anything that remains untested.

## Test design rules

- User-visible behavior must have a scenario in `features/` written in
  Given/When/Then form and an executable test mapped to that scenario.
- Test public behavior or real integration boundaries. Avoid duplicating the
  implementation inside the test.
- CLAP behavior must use a dynamically loaded `.clap` fixture exporting the
  official ABI. It must prove discovery, instantiation, activation, processing,
  sample-offset events, and state streams. A mocked catalog result is insufficient.
- SoundFont behavior must load a real SF2 or SF3 through FluidSynth and prove
  that a note produces non-silent audio. A UI label or mocked renderer does not
  count as SoundFont support.
- VST3 behavior must use a real `.vst3` bundle built by the suite and loaded
  through the production JUCE host adapter. Mock processor construction does
  not count as VST3 support.
- The GUI end-to-end gate must start the real `blokkily` executable, load its
  real QML module, exercise the canonical model, scan the CLAP fixture, render
  a frame, and save a screenshot.
- Mixer behaviour must be proved from rendered audio. A strip's colour, flag, or
  computed gain is not evidence that mute, solo, pan, or a fader changed the bus.
- Export behaviour must read the written file back and compare it with what the
  engine renders. A file that merely exists, or a byte count, is insufficient.
- A panel must be checked for usable size, not only for visibility: an item
  squeezed to nothing by a neighbour still reports itself visible.
- Screenshots are evidence, not golden pixel tests. The gate checks dimensions
  and that rendered pixels vary; an agent must additionally inspect the image.
- A regression fix requires a failing test that demonstrates the regression.
- Tests must be deterministic and must not depend on installed user plugins,
  audio hardware, a network connection, or wall-clock timing beyond bounded UI
  event-loop waits.

## Architectural invariants

- Tracker, piano roll, and step grid are projections of one canonical pattern.
- CLAP support is mandatory and first-class. Builds and tests may not silently
  disable it.
- Real-time processing code must not allocate, lock, perform filesystem I/O,
  log, or call GUI APIs.
- Audio-device tests must exercise the production render callback through a
  deterministic buffer pump. They must not require CI audio hardware.
- Parameter modulation and parameter automation remain distinct event types.
- Format-specific details stay behind plugin adapters.
- A session is a song: named patterns, mixer tracks that own their instrument,
  and the clips that arrange them. Nothing may keep a second copy of a pattern,
  a track list, or a mixer setting beside the song.
- Mixer moves reach the running engine. Changing gain, pan, mute, or solo must
  not rebuild the audio graph or interrupt playback.
- An export is the mix that was auditioned. Bouncing renders through the same
  engine as playback, never through a second rendering path.

## Evidence

CTest writes verification screenshots to `build/artifacts/`. Keep generated
build output untracked. When handing work back, link to the inspected artifact
and summarize what is visibly demonstrated.
