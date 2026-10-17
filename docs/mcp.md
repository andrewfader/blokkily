# Controlling Blokkily through MCP

Blokkily provides an MCP server for controlling the running application. An
agent can read the session, write notes, start and stop playback, call methods
used by the interface and capture a screenshot. Edits use the same undoable
paths as the window's controls.

## Setup

Register the bridge with your agent. For Claude Code:

```sh
claude mcp add blokkily -- blokkily --mcp
```

For a client that accepts JSON configuration:

```json
{ "mcpServers": { "blokkily": { "command": "blokkily", "args": ["--mcp"] } } }
```

`blokkily --mcp` speaks MCP on stdin and stdout and forwards tool calls to the
window over a local socket (`$XDG_RUNTIME_DIR/blokkily-mcp.sock`, or a named
pipe on Windows). Connecting and listing tools does not open a window. The
first tool call starts Blokkily if it is not running. Only the user running
Blokkily can connect.

## Tools

| Tool | Use |
| --- | --- |
| `get_session` | Read the session, including each bus's effects and sends, the song's length in bars and its meter changes |
| `get_pattern` | Read a pattern |
| `write_notes` | Write notes, each retuned from its key by up to ±100 `cents` and pushed off its step by up to ±59 ticks of `offset` |
| `set_section` | Set a pattern's length, swing or name |
| `set_lyrics` | Lay a line of lyrics on a part, one syllable per note, in any script |
| `place_section` | Place a pattern's clips at a bar, on every track or one |
| `set_meter` | Set the song's time signature from a bar on: how long its bars are and where clips land |
| `transport` | Control playback |
| `list_plugins` | List plugins |
| `set_instrument` | Assign an instrument |
| `list_parameters` | List the parameters of a track's instrument or effect, or of an effect on a `return` or the `master`, with each one's current value |
| `set_parameter` | Set a synth preset or knob by name, on a track, a `return` or the `master` |
| `set_soundfont_preset` | Set a General MIDI bank and program |
| `screenshot` | Capture the window |
| `analyze_render` | Measure the mix, a track, a return, every bus at once or a WAVE file, over the song or some bars: loudness, peaks, stereo image, band energy |
| `get_score` | Read a part as staff notation: spelled notes plus the MusicXML document |
| `set_clef` | The clef a track's part of a pattern is read in |
| `set_key_signature` | The key signature a pattern's parts are spelled in |
| `set_time_signature` | The meter a pattern's parts are read in (or unmetered chant) |
| `export_musicxml` | Write a pattern as a MusicXML 4.0 partwise file |
| `import_musicxml` | Read a MusicXML score into a pattern, as one undo step |
| `guide_events` | What the floating guide has been narrating, and who did what |
| `guide_lesson` | List, start and step through the guide's interactive lessons |
| `guide_ask` | Ask the guide a music-theory or composition question |
| `describe`, `call`, `get_property` | Access methods and properties used by the interface |

The generic tools cover mixer controls, effects, automation, clips, tempo,
tuning, saving, exporting and undo.

Every argument is checked against the tool's schema before the tool runs: a
track, section or bar must be a whole number, a step a number, and an action
one of its choices. A list or a string where a number belongs is refused by
name, never read as the selected track.

`set_section` and `place_section` name the pattern they act on, so they are
safe while the song plays: the window keeps open whichever pattern the playhead
is in, and a pattern opened by one call may be gone by the next. Placing a
pattern where it already is changes nothing and answers `alreadyPlaced`; a
placement that is blocked names the clips in the way.

`set_meter` changes the song's own time signature from a bar on (7/8 is 14
steps a bar), so odd-meter sections line up with the arrangement's bars. Clips
and tempo points keep their bar numbers, and it is one step of undo.
`set_time_signature` only changes how a pattern is written. `get_session`
lists the meter changes and `lengthBars`, where the last clip ends; its `bars`
is what the arrangement shows (a spare bar, at least eight).

`write_notes` takes an `offset` in ticks (120 to a step), -59 to 59, that plays
a note early or late without moving it off its step, for a laid-back snare or
a loose double; `get_pattern` reads it back.

`set_parameter` holds a parameter at one value for the whole song with a lane.
A track's lane is on that track. An effect on a return or the master has its
lane on the selected track, or on whichever track already has one for it.

A call is answered when its work is done; an export or `analyze_render` takes
as long as the render. An agent that hangs up before then is owed nothing: the
render it asked for is abandoned (an export's half-written file is removed),
and the window carries on.
## Staff notation

`get_score` reads one track's part of one pattern as the SCORE view draws it:
every note and rest with its bar, step, written value, dots, tuplet and voice,
and for a note its spelling (`C4`, `F#3`), whether it is a chord tone or tied,
its staff position and whether its accidental prints by the bar's rule - and
the part as a MusicXML 4.0 document in the same answer. The clef, key and
meter are the song's unless the call overrides them: `set_clef` picks one of
the eleven clefs for a track's part, `set_key_signature` takes fifths from -7
to 7 for a pattern, and `set_time_signature` takes beats and unit, `unmetered`
for chant (switching it off keeps the meter), or `song: true` to follow the
song's meter again. They are saved with the song and undone with it.

`write_notes` takes a `lyric` on each note and `get_pattern` and `get_score`
read them back; `set_lyrics` lays a whole line on a part, as the SCORE view's
LYRICS field does. The window's own SCORE IN and SCORE OUT are
`appController.importMusicXmlFile(path)` - a score into the song as a new
section with a track per staff - and `appController.exportMusicXmlFile(path)`
- the whole arrangement as a score, read back and compared - through `call`.

`export_musicxml` writes a pattern - one part per track that has a part of it,
or one track's - to an absolute `.musicxml` or `.xml` path, refusing to replace
a file unless `overwrite` is given. It reads the file back and compares what
it plays with each part; `matches` is false where a part holds what notation
cannot say, such as timing off the 128th-note grid. `import_musicxml` reads a
MusicXML partwise score (`xml` or `path`) into a pattern as one step of undo:
part *i* lands on track *i* from `track`, every staff of it at its sounding
pitch, and the part takes the score's clef, key and meter. With `replace` each
part is emptied first. Nothing changes when no part has a track or the score
runs past the pattern; notes outside MIDI 0-127 are left out and counted.
Micro-timing off the grid, probability, ratchets, loop conditions and
parameter locks do not survive the trip - notation cannot say them.

## The guide

`guide_events` reads the narration feed: the last user edits, agent tool calls
and guide actions, each with who did it (`user`, `mcp`, `guide`) and the
guide's why-explanation. Everything an MCP call does is stamped `mcp`, so the
producer can tell what they did from what their agent did. `guide_lesson`
lists the guide's interactive lessons and walks through one (starting one turns
the guide on); each step names the control the guide spotlights in the window,
and a step that waits for the producer counts only the producer's own edits.
`guide_ask` asks the guide a theory question: its built-in rules answer at
once. What they do not cover goes to the guide's bubble only while the guide is
on, and reaches a model only if the producer has switched on ASK MODEL (or
configured the guide's own model) - the question would go to the prompt bar's
model, which may be a cloud service.

## Measuring a render

`analyze_render` renders the song through the same engine as an export, reads
the file back and reports what an engineer would read off meters:

- integrated loudness in LUFS (ITU-R BS.1770-4, gated), the loudest 400 ms and
  3 s and where that 3 s starts, and the loudness range (EBU Tech 3342)
- sample peak, true peak (4x oversampled), RMS, crest factor and clipped samples
- left/right correlation and the spectral centroid
- each band's level and share of the energy: `sub` 20-60 Hz, `bass` 60-250,
  `lowMid` 250-500, `mid` 500-2k, `highMid` 2-4k, `presence` 4-6k, `air` 6-20k

With no arguments it measures the master. `track` or `return` measures that bus
alone, after its inserts, fader and pan. `all` measures the master, every track
and every return from a single render, as a list under `buses`, each with its
`source` and `name`: one render instead of one per bus. `fromBar` and `toBar`
measure bars `fromBar` up to (not including) `toBar`, starting from silence as
playback from `fromBar` does. `file` measures a WAVE file that is already
written, such as an earlier export or a reference mix. Levels of silence are
`null`. Measuring does not change the song.

Through `call`, `appController.exportAudioRange(path, fromBar, toBar)` and
`exportTrackFilesRange(directory, fromBar, toBar)` export some bars of the
song; a `toBar` of -1 is the song's end. A stem export renders the song once,
however many tracks and returns it writes.

## Environment variables

| Variable | Effect |
| --- | --- |
| `BLOKKILY_MCP=0` | Disable the server |
| `BLOKKILY_MCP_SOCKET` | Change the socket; set the same value for the window and bridge |
| `BLOKKILY_MCP_NO_LAUNCH=1` | Prevent the bridge from starting a window |

The executable scenarios are in [features/mcp.feature](../features/mcp.feature).
