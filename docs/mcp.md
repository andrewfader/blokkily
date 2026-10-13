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
| `get_session` | Read the session |
| `get_pattern` | Read a pattern |
| `write_notes` | Write notes |
| `transport` | Control playback |
| `list_plugins` | List plugins |
| `set_instrument` | Assign an instrument |
| `list_parameters` | List parameters |
| `set_parameter` | Set a synth preset or knob by name |
| `set_soundfont_preset` | Set a General MIDI bank and program |
| `screenshot` | Capture the window |
| `describe`, `call`, `get_property` | Access methods and properties used by the interface |

The generic tools cover mixer controls, effects, automation, clips, tempo,
tuning, saving, exporting and undo.

## Environment variables

| Variable | Effect |
| --- | --- |
| `BLOKKILY_MCP=0` | Disable the server |
| `BLOKKILY_MCP_SOCKET` | Change the socket; set the same value for the window and bridge |
| `BLOKKILY_MCP_NO_LAUNCH=1` | Prevent the bridge from starting a window |

The executable scenarios are in [features/mcp.feature](../features/mcp.feature).
