# Current limitations

Blokkily is experimental. These restrictions apply to the behaviour described
in the [usage guide](usage.md).

## MIDI and MPE

- The MPE chip enables the lower zone. An upper zone can be configured only by
  a message from the keyboard.
- Per-note expression is recorded and played back but has no editor.
- VST3 instruments receive per-note pitch only within a two-semitone channel
  bend. SoundFonts have no CC 74 timbre response unless the SF2 maps it. The
  sampler supports per-note pitch only.
- MIDI clock input is not supported, and there is no MIDI output to hardware
  (notes or clock); MIDI OUT exports a Standard MIDI File.
- The built-in sampler ignores poly pressure. The controller lane cannot edit
  poly pressure.
- Incoming notes and controllers are timestamped to the callback block.

MIDI file exchange also omits plugin state, parameter automation, per-note
expression, audio clips and microtonal retuning. Tempo ramps export as
sixteenth-note steps. See [MIDI file exchange](usage.md#midi-file-exchange).

## Audio and sampling

- Comping, or choosing alternate passes of a recorded lane, is not implemented.
- Disk streaming handles unwarped audio clips only. Warped clips decode into
  memory before stretching, and the sampler always decodes its samples.
- A streamed clip that crosses the song's wrap point dips briefly at the wrap
  because there is one stream per clip.
- The sampler panel loads one file at a time. It has no multi-sample import
  wizard, velocity-layer editor or waveform editing surface.

## Parameter locks and routing

- Step parameter locks target the track instrument. Insert parameters use
  automation lanes; insert-targeted step locks are not supported in this release.
- Only the first auxiliary input of a CLAP or VST3 effect receives a sidechain
  key, as a stereo or mono signal. Other sidechain inputs receive silence.
- VST3 extra buses are used only with a stereo main bus.

## Modulation

- Modulation is evaluated once per block, up to the device's block size. A
  synced LFO therefore turns on in the block after its beat.
- A follower targeting its own track, or a track that the render order cannot
  place after its source, receives the previous block's level.
- Sync divisions are straight note values, without dotted or triplet options.
  A modulation "bar" is four beats regardless of the song's meter.

## Scene launcher

- Scenes have no individual tempo settings.
- Export always renders the arrangement. Live launched tracks must be recorded
  into the arrangement to appear in an export.
- When a printed take leaves the tail of an earlier clip, that tail does not
  chase a controller value set before its trim.

## Output recording

- Output recording selects one bus per pass. Bounce additional racks to build
  a set of stems.
- A partial live pass ends when recording stops and does not automatically add
  effect tails. Offline stems include tails.
- Track stems exclude send returns. Track and return stems sum to the signal
  before master processing; a nonlinear master effect cannot be distributed
  among independent stems. Muting a source also stops its sends. See
  [export, resampling and stems](usage.md#export-resampling-and-stems).

## Plugin windows

Plugin editor windows open only on Linux. On X11 they embed in the host window.
On Wayland a CLAP plugin with Wayland support embeds natively, and other
editors embed through XWayland when an X server is available. On macOS and
Windows, plugins load and play but their editors do not open yet: the CLAP
host never selects the Cocoa or Win32 window API, and the VST3 editor needs
X11. Offscreen verification
checks the embedding lifecycle; a separate display gate checks real VST3
pixels when an X server is available. See
[verification](verification.md) for display-gate requirements and skips.
