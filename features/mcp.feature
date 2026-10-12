Feature: An agent drives Blokkily over MCP
  A producer can hand the session to an AI agent: the agent is configured to
  run `blokkily --mcp`, which speaks the Model Context Protocol on its stdin
  and stdout and carries every tool call to the running window over a local
  socket. The agent edits the same canonical song the window does, through
  the same methods its controls call, so what it writes is heard, drawn in
  every editor, and taken back by UNDO.

  Each scenario is exercised by the bdd_mcp gate (`--scenario mcp`, in
  src/app/verify/scenario_mcp.cpp), which starts the real `blokkily --mcp`
  bridge as a child process and talks to it the way an agent does.

  Scenario: An agent connects and is told what it can do
    Given Blokkily is running with one CLAP track
    When an agent starts `blokkily --mcp` and initializes
    Then it is answered with the protocol version it asked for and a tools capability
    And tools/list names get_session, get_pattern, write_notes, transport, list_plugins, set_instrument, screenshot, describe, call and get_property

  Scenario: The agent reads the session
    When the agent calls get_session
    Then it reads the track's name and that it has an instrument, the patterns, and the transport

  Scenario: Notes the agent writes while the song plays are heard
    Given the song is playing an empty pattern, and is silent
    When the agent writes three notes with write_notes
    Then the step grid draws them and get_pattern reads them back
    And the next blocks of the production render callback are not silent

  Scenario: The agent calls the window's own methods, and UNDO takes them back
    When the agent calls songModel.setTrackGain(0, -6)
    Then the track's fader is at -6 dB
    When it calls songModel.undo twice
    Then the fader is back at 0 dB and the notes are gone

  Scenario: A bad request is refused without changing the song
    When the agent writes a note with key 200, or calls a method that does not exist
    Then the call is answered as an error and the pattern is unchanged

  Scenario: The agent sees the window
    When the agent calls screenshot
    Then it receives a PNG the size of the window whose pixels vary

  Scenario: The agent sets an instrument's parameter for the whole song
    Given a pattern of notes on a CLAP track
    When the agent calls appController.automateInsert("track", 0, -1, the Level parameter)
    And holds that lane at 0 with songModel.moveAutomationPoint
    Then the song renders silent from the production callback
    When it moves the point to 0.25
    Then the song is heard again

  Scenario: Without a window the bridge still answers, and says why calls fail
    Given nothing is listening on the bridge's socket and it may not start Blokkily
    When an agent initializes and lists the tools
    Then both are answered
    And a tool call is answered as an error saying Blokkily is not running

  Scenario: Closing the agent's end ends the bridge
    When the agent closes the bridge's standard input
    Then the bridge exits cleanly
