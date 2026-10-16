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
    And tools/list names get_session, get_pattern, write_notes, transport, list_plugins, set_instrument, list_parameters, set_parameter, set_soundfont_preset, screenshot, describe, call, get_property and analyze_render

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
    Then the lane is named INST · LEVEL, after the parameter, not its number
    And holds that lane at 0 with songModel.moveAutomationPoint
    Then the song renders silent from the production callback
    When it moves the point to 0.25
    Then the song is heard again

  Scenario: The agent lists a sound's parameters and sets one by name
    When the agent calls list_parameters for the CLAP track
    Then it reads Level with its range
    When it sets "level" to 0 with set_parameter
    Then the song renders silent, and set to 0.25 it is heard again
    And a value out of range, or a parameter that does not exist, is refused

  Scenario: The agent locks a parameter on one step, by its address
    Given the CLAP track plays notes on steps 1, 5 and 9, with no lane on its Level
    When the agent calls appController.setStepLock to lock Level to 0 on step 5 and to 0.25 on step 9
    Then get_pattern reads each lock back on its note, named Level
    And the production render callback is heard on step 1, silent from step 5, and heard again from step 9
    When it calls appController.clearStepLocks on step 5
    Then step 5 is heard again, and songModel.undo puts its lock back and silences it
    And a step with no note, a parameter that does not exist, a value out of range, or a part that does not exist is refused, changing nothing

  Scenario: The agent chooses a SoundFont's preset, and its note is heard
    Given a second track playing the test SoundFont
    When the agent calls set_soundfont_preset for bank 0, program 0
    Then the track's state names that bank and program, and a note on it is heard soloed
    And a preset the file does not hold, or a track that is not a SoundFont, is refused

  Scenario: The agent measures what it made
    Given the CLAP track plays sines on C4 and G4 beside the SoundFont track
    When the agent calls analyze_render with no arguments
    Then it reads the master's integrated loudness, a true peak no lower than the sample peak, and band shares that sum to 1
    And the render runs the song's length plus the export's half second of tail
    When it calls analyze_render for track 0
    Then the low-mid band holds most of that track's energy and the centroid sits near its notes
    When it exports the song and calls analyze_render with that file
    Then the file measures as the render did, because both came from the one engine
    When it lowers track 0's fader by 6 dB and measures the track again
    Then the track reads 6 LU quieter and its true peak 6 dB lower, and UNDO puts the fader back
    And a track or return that does not exist, a track and a file together, or a missing file is refused
    And measuring leaves the transport stopped where it was

  Scenario: Without a window the bridge still answers, and says why calls fail
    Given nothing is listening on the bridge's socket and it may not start Blokkily
    When an agent initializes and lists the tools
    Then both are answered
    And a tool call is answered as an error saying Blokkily is not running

  Scenario: Closing the agent's end ends the bridge
    When the agent closes the bridge's standard input
    Then the bridge exits cleanly

  Scenario: A second Blokkily never takes the socket from one that is busy
    Given a Blokkily serving MCP whose window is too busy to accept connections
    When a second Blokkily starts and tries to serve the same socket
    Then the second is refused, because the first holds the socket's lock
    And a new agent still reaches the first window by the same path

  Scenario: The bridge waits for a window that is still starting
    Given a Blokkily that has claimed the socket but is still loading
    When an agent calls a tool through the bridge
    Then the bridge neither starts another window nor reports Blokkily missing
    And the call is answered once the window starts listening
