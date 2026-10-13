# Using Blokkily

A session contains the whole song: named patterns, tracks with their instruments
and mixer settings, and arrangement clips. A pattern such as VERSE or CHORUS
contains a separate part for each track. Selecting a track shows its part of
the open pattern; notes entered there belong to that track.

The tracker, piano roll and step grid edit the same underlying parts. A change
in one appears in the other two, the step inspector and playback. The code
calls a whole pattern a section, and each track's part a pattern.

Press F1 or click HELP (at the end of the keyboard hints above the editors) for
a guided tour: Blokkily, the mascot, points out each part of the window in turn.
The tour leaves on DONE, CANCEL, Escape or a second press of HELP, and the
window stays playable while it runs. CLEAR in the transport bar empties the
session to one blank pattern and track, keeping the metronome settings. It is
one undo step, so UNDO brings everything back, including each plugin's sound.

## Editing patterns

Use the view switcher to focus one editor or show them all. Notes entered in any
editor are auditioned through the running engine, whether the transport is
stopped or playing. Editing during playback keeps the playhead in place. If you
erase a sounding note, the engine releases it.

The step inspector edits transposition, velocity, micro-timing, probability,
ratchets, loop conditions and parameter locks. Locks have an
automation/modulation switch: automation sets a parameter's value, while
modulation offsets it. Both can apply to the same parameter.

On a chord step the inspector also sets its voicing. INV ▲ lifts the lowest
voice an octave and ▼ INV drops the highest one. The chord's voices change in
the pattern itself, so the tracker and piano roll show the inversion you hear.
STRUM delays each voice after the one below it by 0 to 60 ticks (up to half a
step). Each click, and each drag of the dial, is one undo step. See
[the voicing scenarios](../features/chord_voicing.feature).

### Step grid

Click a step to toggle it. The edit uses the tracker's entry octave.
Shift-click selects a step for the inspector without toggling it, and
Ctrl-click moves the playhead to that step.

### Tracker

The tracker shows the section's instrument tracks side by side. Tab and
Shift+Tab select the next or previous channel, and long patterns scroll
vertically.

Click or drag in the note column to enter notes. Type ZSXDCVGBHNJM for one octave
and use the two keyboard rows above for the next. The entry octave is shown on
screen; Ctrl+Left and Ctrl+Right change it. Backspace or Delete clears a row.

The velocity column sets the row's velocity. Copying and pasting a step keeps
its locks and length.

### Piano roll

Click a pitch lane to write a note in the column under the pointer. Drag across
lanes to draw a phrase. Drag a note's body to move it, or its right edge to
change its duration. The arrow buttons shift the pitch window by octaves,
covering MIDI keys 0–127.

### Pattern and track management

DUP copies the open pattern into a new one. Right-click a pattern to rename,
duplicate, clear or delete it. Right-click a track to rename, mute, solo or
delete it. Double-click either name to edit it.

+TRK adds a track on the piano preset of the first SoundFont already assigned
in the session, or of an installed bank if none is assigned. Without an
available bank, the new track has no instrument. Renaming the track does not
change its preset.

The default instrument loader runs only when the session has no instruments.
It prefers an installed General MIDI SoundFont and falls back to the first
bank it finds. It picks each track's preset from its name:

| Name contains | Preset |
| --- | --- |
| DRUM, PERC or KIT | Percussion bank 128 |
| BASS | Program 33, fingered electric bass |
| PAD or STRING | Program 48, string ensemble |
| LEAD or SYNTH | Program 81, sawtooth lead |
| Anything else | Program 0, acoustic grand piano |

If any track already has an instrument, the loader leaves all tracks
alone, including empty ones. With no installed SoundFont, load an instrument
from the browser to hear notes.

## Arranging a song

The arrangement sits above the editors, with one lane per track and one cell
per bar. Patterns can have different lengths.

| Action | Result |
| --- | --- |
| Click an empty bar | Place the open pattern's parts on all tracks with room for them |
| Ctrl-click an empty bar | Place only that track's part of the open pattern |
| Click a filled clip | Open its pattern, select its track and seek to the clicked bar |
| Right-click a clip | Remove only that track's clip |
| Shift-click a filled bar | Set that clip's repeat count so it ends at the clicked bar |
| Shift-click an empty bar | Extend the preceding clip on that lane to cover the clicked bar |
| Alt-click an empty bar | Move the clip under the playhead on that lane to the clicked bar |
| Click a bar number on the ruler | Locate the audio engine on that bar |

Shift-click uses whole pattern repeats, so a clip may end beyond the clicked
bar, and a clip repeats at most 64 times. Shift-clicking within a long clip
shortens it; a plain click only opens it. Resizing and moving clips
cannot overlap another clip on the same lane.

The arrangement scrolls horizontally and has +/− zoom controls, so long songs
can keep usable bar widths. Each repetition of a clip advances its pattern's
loop count; probability and loop conditions work across the arrangement.

The playhead reads the audio engine's position in bar.beat.sixteenth across the
whole song. The tempo lane supports steps and ramps. Meter edits on the ruler
keep clips on their bar numbers. Drag or scroll the tempo to change it, hold
Shift for tenths, or double-click to type a value.

## Playing with tunings and scales

Below the editors, choose the session's tuning, scale and root, then select a
piano, isomorphic grid, fretboard or chord-pad surface. Auto-scale snaps a played
key to a note in the scale.

Tunings are stored in cents. They include equal divisions of any period,
rational and historical tunings, and Scala-style lists. Notes and individual
chord voices carry a twelve-tone key plus a retune offset, so CLAP, VST3 and
SoundFont instruments receive the intended microtonal pitch. Every editor names
notes in the song's tuning.

Playing a surface auditions the selected track's instrument and writes onto
the selected pattern step. A key sounds on press and is released when you let
go, including with the transport stopped.

The isomorphic grid has seven layouts:

- Wicki-Hayden, Bosanquet, harmonic table and B-system accordion on hexagonal tiles.
- Jankó, fourths and major thirds on square tiles.

Each layout uses cents per step, so its fingering follows the song's tuning.
Surfaces can run across the window or down it. Rotating one by a quarter turn
moves its keys without changing notes or pitches. If keys would become too
small to play, the surface scrolls instead of shrinking further.

Fretboards offer nine string tunings. Chord pads label harmonies by numeral and
show a row for each inversion.

## Instruments and the plugin browser

The browser scans standard CLAP and VST3 locations and labels each result by
format. It also lists Sampler and Drum Sampler, which support keyed zones,
loops, envelopes, kit pads and slicing. SoundFont instruments load SF2/SF3
banks and let you choose presets. CLAP and VST3 plugins provide stereo ports and
can open native editor windows; see the [display restrictions](limitations.md#plugin-windows).

Ctrl+F focuses the browser's search field. Search matches letters in order
across a name, maker or format: "fbs" finds "Fat Bass". Use the arrow keys to
select a result and Return to load it. Escape clears the filter.

## MIDI input and recording

The MIDI IN panel on the left chooses the controller. Its light flashes on
each incoming key. A controller plays the selected track's instrument while
stopped or playing. Its keys map to degrees of the song's tuning and scale, as
on the on-screen piano.

Each track's R button arms notes and audio. Its input controls choose MIDI
channels or audio-device inputs and monitoring. With no track armed, the
selected track records. The on-screen keyboards and tracker can also record
while the song plays.

During recording, notes go into the pattern under the playhead. They land on
the nearest step, keep their timing offset as micro-timing, and last as long as
the key was held. Notes on the same step become a chord; each voice keeps its
own velocity and length. Recording uses the position at which the engine heard
the input. One take is one undo step, shared by its notes, automation and audio.
A bounce does not hear or record live MIDI input.

Pitch wheel, control changes and pressure reach the track's instrument on the
controller's channel. SoundFont, VST3, CLAP and the sampler follow the wheel;
SoundFonts also accept poly pressure. The sustain pedal holds released keys at
the input for every instrument.

Recorded wheel, controller, channel-pressure and poly-pressure movements become
pattern controller events. Poly pressure follows the key sent by the song's
tuning. Events are saved with the project, played at their sample offsets,
chased when the playhead jumps and exported as heard.

The controller lane below the piano roll shows one controller at a time. Its
picker offers BEND, MOD, EXPR, any numbered CC or PRESSURE. Drag to draw a
stroke, drag a point to move it, or right-click to erase it. Each gesture is one
undo step and reaches playback on the next block.

Played notes and controllers keep their timing to the sample. Each message is
stamped as it arrives and placed at the matching sample of the next audio
block, so what you play is heard, and recorded, one block late but without
jitter. See [the timing scenarios](../features/midi_timing.feature).

For MPE, enable MPE on the MIDI IN panel or let the keyboard send its
configuration message. MPE reads the lower zone (notes on channels 2-16);
click UPPER beside it for a keyboard set to the upper zone (channels 1-15). Each note's bend, CC 74 and pressure affect that note
alone and are recorded with it. Playback uses CLAP note expressions or a
per-note channel for SoundFont and VST3. See
[the continuous MIDI scenarios](../features/continuous_midi.feature) and
[format-specific limits](limitations.md#midi-and-mpe).

CLICK follows the tempo and meter. CI counts in up to four bars without moving
the song. Exports exclude the click unless + CLICK is enabled.

## MIDI file exchange

The project panel's MIDI IN / MIDI OUT buttons import and export Standard MIDI
Files. Export uses the playback scheduler to write arranged notes and
controllers, track names, tempo and meter. Tempo ramps become sixteenth-note
tempo steps.

Import accepts formats 0 and 1, splits channels into instrument parts and
appends four-bar sections as one undoable edit. It keeps the current song's
tempo and instruments; assign instruments to any additional tracks. Import
uses the recording grid: notes on one sixteenth form a chord with shared
timing, and durations are capped at the section length.

MIDI exchange omits plugin state, parameter automation, per-note expression,
audio clips and microtonal retuning. Use project files to back up a session.

## Audio clips and files

+AUD imports WAV, FLAC or AIFF files as waveform clips with trim, fades and gain.
Overlapping clips sum. A clip's W control enables tempo following, a manual
stretch ratio or pitch shift. Rubber Band processes these changes on a worker;
the clip is silent until the rendition is ready.

Unwarped clips whose decoded size exceeds 128 MiB stream from disk by default.
`BLOKKILY_STREAM_THRESHOLD_BYTES` overrides the threshold. Streams are prepared
ahead of clip starts and song wraps and relocated when you seek. An underrun
fades out and back in to avoid a click. Export waits for streamed data on its
own thread, so it does not reproduce playback underruns. Warped clips and
sampler samples still decode into memory.

Imported files stay in their original locations. Recordings go into
`<project>.audio/`, or a temporary session folder before the first save.
COLLECT copies referenced clips and sampler samples into the saved project's
audio folder, deduplicates shared files and updates references as one undoable
edit. Save afterwards to persist the references. Original files are kept so
undo can still play them.

## Mixing and effects

The mixer on the right has one strip per track plus a master fader. Each track
owns its instrument and channel strip, with gain in decibels, constant-power
pan, mute, solo and a peak meter. Mixer changes reach the running engine
without rebuilding the graph or interrupting playback.

The FX browser adds CLAP, VST3 or built-in EQ, delay, reverb and compressor
inserts to a track, return or master rack. Sends are post-fader and pre-pan.
Returns remain audible when a source track is soloed. Playback and export both
compensate plugin latency.

Each insert row shows the browser's effect name on its own line. E opens its
native window. A chooses a parameter to edit in the selected track's automation
lane. A plugin-window gesture is one undo step, including on return and master
inserts.

### Automation and modulation

Automation has OFF / READ / TOUCH / LATCH / WRITE modes. Step locks target the
track instrument; insert parameters use automation lanes.

The MODULATION panel under the inspector adds:

- LFOs with sine, triangle, saws, square or random shapes. Set a rate in Hz, or
  enable SYNC for a note value from 1/16 to 4 bars that follows the tempo map.
  Each note value also comes as a triplet (1/8T) and dotted (1/8.).
- Macros, each with one 0..1 knob.
- Envelope followers, measured from a selected track after its inserts and
  before its fader.

+ TARGET routes a modulator to a parameter of the selected track's instrument
or inserts. Depth ranges from -100% to +100% of the parameter's range.
Modulation offsets the parameter's value and automation; it does not write
into a lane. Multiple modulators on one parameter add up. The engine sends
CLAP parameter modulation or its VST3 and built-in-effect equivalent.

Rates, shapes, depths and macros change live without recompilation. Modulators
and sidechain keys are saved and undone with the song and exported as heard.
During playback, an LFO's phase comes from song position. See
[modulation timing limits](limitations.md#modulation).

### Sidechains

The built-in compressor and CLAP/VST3 effects that declare a sidechain input
have a SIDECHAIN chip. For CLAP this is a non-main input port; for VST3 it is an
auxiliary input bus. Choose another track as the key source. The compressor
then listens to that key instead of the signal it compresses.

The key signal is taken after the source's inserts and before its fader, so a
muted or faded-down kick can still duck a bass. The engine renders the source
first and rejects self-routing or cycles of keys.

### Auxiliary instrument outputs

An instrument with extra CLAP output ports or VST3 auxiliary output buses gets a
+ OUT chip. Break out an output into its own mixer channel, such as "PAD AUX 1".
The channel has no instrument: it receives that output and renders after its
source. Its fader, pan, mute, solo, inserts, sends and automation work like
those of any other track. The output is heard only through this channel, not
also through the source track.

Soloing the source keeps its output channels audible; soloing a channel alone
plays only that channel. Output channels are saved and undone with the song
and exported as heard. Removing the source leaves its channels as plain tracks.

## Export, resampling and stems

The arrangement exports to 16-bit, 24-bit or 32-bit float WAVE through the same
engine as playback. Float files preserve the rendered samples; 16-bit and
24-bit files round them to the chosen precision and clip levels above full
scale. Export is the mix you hear when you play the arrangement from its start:
before the bounce, the engine resets instruments, effects, LFOs and followers
to rest. A live pass can differ where state carried over from earlier
playback, such as an LFO's phase or a plugin that does not fully reset.

RESAMPLE in a rack arms its track, return or master output for the next
recording pass. Arm the transport's record button and press Play. When the pass
ends, the output is written off the audio thread into a new audio track. The
destination cannot feed back into its recording. The master tap excludes the
guide click.

STEM bounces a rack through the same engine into a new audio clip and can mute
its source. Both STEM and RESAMPLE compensate latency up to the selected tap.
When a master bounce replaces muted sources, it also bypasses the old master
inserts and resets the master fader to avoid processing the mix twice. Undo
restores these changes.

Stems are bus taps:

| Tap | Contents |
| --- | --- |
| Track | Inserts and stereo fader/pan, excluding send returns |
| Return | Sends and return processing |
| Master | The complete mix |

Bounce returns as well as tracks to reconstruct the signal before master
processing. A nonlinear master effect cannot be distributed among independent
stems. Muting a source also stops its sends; bounce returns separately if you
need to keep them. See [output-recording limits](limitations.md#output-recording).

## Scene launcher

The LAUNCH view has scenes down the rows and tracks across the columns. Each
cell refers to one of the song's patterns, shared with the pattern editors.

Click an empty cell to put the open pattern in it. Click a filled cell to
launch it on that track. Cell quantization can be none, a sixteenth, a beat,
one, two or four bars. A scene's chip launches the whole row at the grid's
quantization.

A cell can play a set number of loops and then stop, move to the next,
previous, first or last cell, choose randomly, or repeat. Launches, stops,
loop turns and follow actions occur at the exact sample of their boundary.
A launched track plays its cell instead of its arrangement until the transport
stops.

With REC ARRANGEMENT enabled, each stretch played from a cell becomes clips
when it ends, as one undo step. Those clips replay the performed pattern
sequence through the arrangement engine and can be exported. Printing over a
longer clip splits it and keeps the portion after the take.

Adding or removing a track or changing an instrument during a jam rebuilds the
engine without stopping the jam. Launched cells stay in phase and the playhead
stays in place. If a launch queue is full, the launch is refused with a status
message. The status line also reports takes lost to a full take queue. The
grid is saved with the song. Export renders the arrangement, so print a live
performance into it before exporting.

## Saving and undo

Projects save all named patterns, including chords, locks, ratchets,
microtiming and loop conditions; tracks, channel strips and clips; and the
opaque state of every CLAP, VST3 and SoundFont instrument and effect.

Ctrl+S saves to the current project file. Ctrl+Shift+S asks for a new file. A dot
in the window title marks unsaved changes. Opening a file, starting a new
session or closing the window asks before discarding unsaved work.

Ctrl+Z undoes and Ctrl+Shift+Z or Ctrl+Y redoes edits to steps, strokes, clips,
tracks, patterns, faders and tuning. A phrase-drawing or fader drag is one undo
step, rather than one per pointer movement.

## Keyboard shortcuts

While a text field has focus, typing a search or name does not trigger editing
or transport shortcuts.

| Shortcut | Action |
| --- | --- |
| F1 | Start or leave the guided tour |
| Space | Play or stop |
| Return or Home | Go to the start of the song |
| Arrow keys | Move the step cursor |
| Shift+Up / Shift+Down | Transpose the step by a semitone |
| Ctrl+Up / Ctrl+Down | Transpose the step by an octave |
| Ctrl+Left / Ctrl+Right | Change the tracker entry octave |
| Tab / Shift+Tab | Select the next / previous tracker channel |
| Backspace or Delete | Clear a tracker row |
| Insert | Push later rows down |
| Shift+Backspace | Pull later rows up |
| Alt+arrows | Nudge micro-timing and note length |
| Ctrl+C / Ctrl+V | Copy / paste the selected step, including locks and length |
| Ctrl+D | Duplicate the selected step onto the next row |
| Ctrl+1..0 | Toggle a step |
| Ctrl+Shift+1..6 | Toggle a step in the second half of the bar |
| Ctrl+M | Mute the selected track |
| Ctrl+L | Solo the selected track |
| Ctrl+R | Arm recording |
| Ctrl+E | Bounce the arrangement to disk |
| Ctrl+F | Focus the plugin browser search |
| Ctrl+K | Focus the assistant prompt bar |
| Escape in the assistant | Return keyboard focus to the editors |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z or Ctrl+Y | Redo |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save to a new file |
| Ctrl+N | Start a new session |
| Ctrl+O | Open a project |

## Composition assistant

The prompt bar above the editors asks an LLM to write into the open pattern.
For example, try "a minor pentatonic ascending" or "offbeat hats". The default
backend is a local Ollama model. Gemini, MiniMax, ChatGPT and OpenRouter are
also available with their API keys.

Replies are parsed and clamped like other input and shown as proposals. Nothing
changes in the song until you click Apply. Each generation is one undo step.
The assistant runs on the GUI thread only; it never calls an LLM from the audio
thread.

See the [assistant reference](llm-assistant.md) for the wire dialect, backends
and the network-free `bdd_llm_assistant` verification gate.

## Screenshot session

The [README screenshot](../README.md) is the rendered `bdd_session_everything`
session. Its song includes a tempo ramp, a 7/8 bar, keyed and sliced samplers,
CLAP, VST3 and SoundFont instruments, a warped audio clip, built-in and
CLAP/VST3 effects, a return/send bus, gain and effect automation, a count-in,
and two armed tracks receiving deterministic MIDI input. See
[verification](verification.md) for the scenario and its artifacts.
