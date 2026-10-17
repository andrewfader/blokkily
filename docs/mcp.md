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
| `get_session` | Read the session, including each bus's effects and sends |
| `get_pattern` | Read a pattern |
| `write_notes` | Write notes, each retuned from its key by up to ±100 `cents` |
| `set_section` | Set a pattern's length, swing or name |
| `place_section` | Place a pattern's clips at a bar, on every track or one |
| `transport` | Control playback |
| `list_plugins` | List plugins |
| `set_instrument` | Assign an instrument |
| `list_parameters` | List the parameters of a track's instrument or effect, or of an effect on a `return` or the `master` |
| `set_parameter` | Set a synth preset or knob by name, on a track, a `return` or the `master` |
| `set_soundfont_preset` | Set a General MIDI bank and program |
| `screenshot` | Capture the window |
| `analyze_render` | Measure the mix, a track, a return or a WAVE file: loudness, peaks, stereo image, band energy |
| `describe`, `call`, `get_property` | Access methods and properties used by the interface |

The generic tools cover mixer controls, effects, automation, clips, tempo,
tuning, saving, exporting and undo.

`set_section` and `place_section` name the pattern they act on, so they are
safe while the song plays: the window keeps open whichever pattern the playhead
is in, and a pattern opened by one call may be gone by the next.

`set_parameter` holds a parameter at one value for the whole song with a lane.
A track's lane is on that track. An effect on a return or the master has its
lane on the selected track, or on whichever track already has one for it.

A call is answered when its work is done; an export or `analyze_render` takes
as long as the render. An agent that hangs up before then is owed nothing, and
the window carries on.

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
alone, after its inserts, fader and pan. `file` measures a WAVE file that is
already written, such as an earlier export or a reference mix. Levels of
silence are `null`. Measuring does not change the song.

## Environment variables

| Variable | Effect |
| --- | --- |
| `BLOKKILY_MCP=0` | Disable the server |
| `BLOKKILY_MCP_SOCKET` | Change the socket; set the same value for the window and bridge |
| `BLOKKILY_MCP_NO_LAUNCH=1` | Prevent the bridge from starting a window |

The executable scenarios are in [features/mcp.feature](../features/mcp.feature).
