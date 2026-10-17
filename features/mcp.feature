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

  Scenario: The agent writes a note between keys, and it is heard there
    Given the CLAP track plays one long A4 sine, written with write_notes
    When the agent writes the same note with cents 50
    Then get_pattern reads the note back with its 50 cents
    And the track's rendered pitch is a quarter tone higher than the plain A4's
    And a note retuned by more than 100 cents is refused, and the part keeps its note

  Scenario: The agent sets an effect on a return and on the master by name
    Given a Saturation on the master, and one on a return that the CLAP track sends to
    When the agent calls list_parameters for the master's slot 0
    Then it reads Output with its range, and the return's effect is listed too
    When it sets the master's "output" from 0 to -12 with set_parameter
    Then the master renders 12 LU quieter, from one lane kept on the selected track
    When it sets the return's "output" from 0 to -12
    Then the return renders 12 LU quieter
    And with another track selected, the master's existing lane is the one that moves
    And a return that does not exist, a track named beside the master, a value out of range, or the master's instrument slot is refused

  Scenario: The session shows each bus's effects and sends
    Given Saturation on the master and on a return that the CLAP track sends to
    When the agent calls get_session
    Then it reads the master's and the return's effects by name and slot
    And the track's send to that return with its level, and each pattern's length

  Scenario: The agent shapes and places a pattern while the song plays
    Given the song plays with the window following the playhead, which keeps the first pattern open
    When the agent adds a pattern and calls set_section for it with 32 steps, swing 25 and the name BRIDGE
    Then every part of that pattern is 32 steps long with swing 25, and the open first pattern keeps its 16
    When it calls place_section for BRIDGE at bar 8
    Then every track has a two-bar BRIDGE clip there, and get_session reads it back
    And place_section with a track places that track's part alone
    And a bar already taken, a pattern or track that does not exist, a length under one step, a swing over 100, or no change at all is refused, changing nothing

  Scenario: An agent that hangs up during a long call leaves the window running
    Given an agent has asked for analyze_render
    When it kills its bridge while the window is still rendering
    Then the window drops the answer it owed instead of writing it to the closed connection
    And the next agent that connects is answered

  Scenario: Without a window the bridge still answers, and says why calls fail
    Given nothing is listening on the bridge's socket and it may not start Blokkily
    When an agent initializes and lists the tools
    Then both are answered
    And a tool call is answered as an error saying Blokkily is not running

  Scenario: Closing the agent's end ends the bridge
    When the agent closes the bridge's standard input
    Then the bridge exits cleanly

  Scenario: The tools list names the notation and guide tools
    When the agent lists the tools
    Then it names get_score, set_clef, set_key_signature, set_time_signature, export_musicxml, import_musicxml, guide_events, guide_lesson and guide_ask

  Scenario: The agent reads a part as staff notation
    Given the CLAP track plays a quarter-note C4, then an E4-G4 dyad on beat three
    When the agent calls get_score
    Then it reads C4 on step 0 and the dyad on step 8 as quarters, the G4 marked as a chord tone
    And quarter rests on steps 4 and 12, and a MusicXML document with the chord and the rests

  Scenario: The agent dresses the score in another clef, key and meter
    When the agent sets the clef to Bass, the key to 2 flats and the meter to 3/4
    Then get_score reads the clef Bass, the key Bb major and the meter 3/4
    And the E4 keeps its pitch and prints a natural sign
    When it switches chant on and off, then asks for the song's meter
    Then chant keeps 3/4 for when it comes off, and the song's 4/4 comes back
    And one UNDO puts 3/4 back, while the clef and key stay in the song
    And a bad clef name, a key of 8 fifths or of "2", a meter of 3/3, or no meter at all is refused and the score is unchanged

  Scenario: The agent exports MusicXML and reads it back
    When the agent exports the part to a MusicXML file
    Then the file is read back from disk and plays the part's three notes as the part does
    And exporting over it again is refused unless overwrite is given, as are a relative path and a .wav name
    When the agent clears the part, sets it back to treble in C, and imports the file with replace
    Then get_pattern reads the C4 and the dyad at their steps and lengths again
    And the part takes the file's bass clef, two flats and 3/4
    And one UNDO takes back the notes and the clef, key and meter together

  Scenario: A MusicXML import that cannot land is refused without changing the song
    When the agent imports a score three bars long into a one-bar pattern
    Then the call is refused, naming set_section to lengthen the pattern
    And text that is not MusicXML, a missing file, no document, or both xml and path are refused
    And the pattern is unchanged

  Scenario: The agent writes lyrics, in any script
    When the agent writes three notes, the first two sung "Hal-" and "le-"
    Then get_pattern reads each note's lyric, the third with none
    When it lays a line of pointed Hebrew on the part with set_lyrics
    Then all three syllables find a note, get_score reads them under their notes and its MusicXML carries them
    And one UNDO puts the earlier lyrics back
    And set_lyrics with no text, or a lyric longer than 256 bytes, is refused

  Scenario: The guide narrates what the agent did
    When the agent writes a note with write_notes
    Then guide_events names the agent as the source and explains the note in the guide's voice
    When the producer plays the transport from the window
    Then guide_events names the user as the source of the transport event

  Scenario: The agent walks a guide lesson and asks it a question
    When the agent lists the guide's lessons
    Then it reads "Your first beat" among them
    When it starts that lesson
    Then the lesson is active on its first step
    When it steps forward and back, then stops
    Then the lesson is no longer active
    When it asks the guide what a triad is
    Then the answer names three notes a third apart
    And asking with no question is refused

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
