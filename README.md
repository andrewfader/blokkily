# Blokkily

Blokkily is an experimental pattern-based DAW. You can edit the same pattern in
a tracker, piano roll or step grid, then arrange patterns across multiple
tracks. It supports CLAP and VST3 plugins, SoundFont instruments and built-in
samplers. CLAP support is required in every build.

**[Watch a short video](https://www.youtube.com/watch?v=_mbjwN5OBEg)** (with
sound) of an agent building a funky chiptune in Blokkily over
[MCP](docs/mcp.md), using the YANES and FUZZBOI CLAP synths.

![Blokkily workstation showing an arrangement, synchronized editors, plugin browser, mixer, and keyboard](docs/images/blokkily-session.png)

The screenshot comes from the `bdd_session_everything` verification session.
It shows the arrangement, pattern editors, plugin browser, mixer and playable
keyboard, with the composition assistant's prompt bar above the editors.

## Build and run

You need Qt 6.5+, plus FluidSynth, RtAudio, RtMidi, libsndfile, libsamplerate and
Rubber Band 4 (found through pkg-config). The verification tests also need a
SoundFont. JUCE and the CLAP headers are vendored under
`third_party/`.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/blokkily
```

`./build/blokkily_cli` runs the headless scheduler demo.

Clip warping links the Rubber Band Library, licensed under the GPL (version 2
or later) unless you buy a commercial licence from its authors. A Blokkily
binary built with it is therefore subject to the GPL.

IBM Plex Sans and JetBrains Mono are bundled in `resources/fonts/` under the
SIL Open Font License 1.1, with licence texts beside them. Bundling the fonts
keeps the layout consistent across desktops.

## What you can do

- Edit notes and chords in any of the three synchronized editors. Patterns
  support inversions, strum, microtiming, probability, ratchets, loop conditions
  and parameter locks. Edits are heard during playback without stopping it.
- Arrange named patterns and audio clips on a multi-track timeline, with tempo
  steps, ramps and meter changes. The scene launcher lets you perform patterns
  live and record the performance into the arrangement.
- Play and record with a MIDI controller or the on-screen piano, isomorphic
  grids, fretboards and chord pads. Tunings and scales include microtonal
  options. A session with no instruments uses an installed SoundFont by
  default, preferring a General MIDI bank when available.
- Load CLAP, VST3 and SoundFont instruments, or use the keyed and drum samplers.
  Mix with inserts, sends, returns, sidechains, auxiliary outputs, automation
  and modulation.
- Record audio and MIDI, resample a bus, or bounce stems. Export the arrangement
  as 16-bit, 24-bit or 32-bit float WAVE through the same engine used for
  playback. Import and export Standard MIDI Files for note exchange.
- Ask the LLM composition assistant for a phrase. Review its proposal before
  applying it; each generation is one undo step. An MCP bridge also lets agents
  control the running application.

See the [usage guide](docs/usage.md) for controls, keyboard shortcuts and
recording workflows.

## Current limitations

Blokkily is experimental. Comping and MIDI clock input are not implemented.
MPE support varies by instrument format, and per-note expression has no editor.
Large unwarped audio clips can stream from disk; warped clips and sampler
samples still load into memory. Native plugin-window pixels require an X11
display. Export always renders the arrangement, so record a launcher
performance into it before exporting.

The [limitations reference](docs/limitations.md) covers these restrictions and
others, including sidechains, modulation timing, sampler editing and stems.

## Documentation and development

- [Usage guide](docs/usage.md): editing, arranging, recording, mixing and shortcuts.
- [Architecture](docs/architecture.md): the shared model, scheduler, plugin
  adapters and coverage commands.
- [Agent control through MCP](docs/mcp.md): setup, tools and environment variables.
- [LLM composition assistant](docs/llm-assistant.md): backends and reply format.
- [Verification](docs/verification.md): repeatable BDD, integration, end-to-end
  and screenshot checks.

Behaviour specifications live in `features/`. Repository-wide completion rules
are in [AGENTS.md](AGENTS.md).
