Feature: Native plugin windows
  A producer opens an instrument's own window from its mixer strip or the
  instrument panel, turns its knobs, and hears the song change. Each turn is
  one step of undo. The window belongs to the instrument: it survives the
  graph being rebuilt around it and closes when the instrument goes.

  # Executable checks:
  #   core   = blokkily_plugin_windows_tests <case> (CTest plugin_windows_<case>)
  #   gate   = bdd_plugin_windows, reached() labels "plugin windows: ..."
  #   nodisp = blokkily_editor_nodisplay_check
  #   x11    = plugin_window_display_check (skips with 77 without an X server)
  #   rt     = realtime_plugin_windows

  Scenario: The run loop serves plugin timers and descriptors
    # core: run_loop
    Given a run loop the host installed
    When a plugin registers a 10 ms and a 25 ms timer and time moves on 25 ms
    Then the timers fire in time order, once per period passed
    And a descriptor is called back only for the events it is watched for

  Scenario: A CLAP editor is embedded in the window the host gives it
    # core: clap_embed
    Given the CLAP fixture
    When its editor is opened with an X11 parent window
    Then the host creates it, scales it, asks its size, gives it the parent and shows it, in that order
    And the editor is 320 x 200 physical pixels
    And closing it hides and destroys it

  Scenario: A CLAP editor is embedded in a Wayland surface
    # core: clap_wayland_embed
    Given the CLAP fixture and a native Wayland host surface
    When its editor is opened
    Then it is created, parented to that surface through the Wayland API, and shown

  Scenario: A CLAP editor can float on its own
    # core: clap_floating
    Given the CLAP fixture
    When its editor is opened with no parent
    Then it is created floating, given a title and shown, and never given a parent

  Scenario: What an editor asks of its window reaches the window
    # core: clap_requests
    Given an open CLAP editor
    When it asks for a new size on the main thread
    Then the window is resized at once
    When it asks from another thread
    Then the window is resized when the host next serves the main thread
    When the editor closes itself
    Then the host destroys it on the main thread and the window is told

  Scenario: An editor's timer is served while it is open, and only then
    # core: clap_timer
    Given the CLAP fixture, whose editor keeps a 20 ms timer
    When its editor is open and 100 ms of the run loop pass
    Then the timer fires five times
    And once the editor is closed the timer is gone and never fires

  Scenario: An editor's display connection is watched while it is open
    # core: clap_posix_fd
    Given an open CLAP editor watching its display connection
    When something arrives on that connection
    Then the editor is called back and reads it
    And closing the editor stops the watch

  Scenario: An instrument destroyed with its editor open closes the editor first
    # core: clap_destroy_open
    Given an open CLAP editor
    When its instrument is destroyed
    Then the editor is destroyed before the plugin, the window is told, and nothing is left in the run loop

  Scenario: A knob turned in the editor is one step of history, heard in the song
    # core: gesture_step; gate: "LEVEL 0.60 turned in the editor is heard",
    #       "undo takes the turn back to 0.25 and redo returns it"
    Given the CLAP fixture on track 0 at level 0.25 with its editor open
    When the Level knob is turned to 0.60 in the editor
    Then the turn arrives as one gesture through the engine's edit ring
    And the readout shows "LEVEL 0.60" and 0.60 is heard on the bus
    When the producer undoes once
    Then 0.25 is heard again without the audio graph being rebuilt
    When the producer redoes
    Then 0.60 is heard again

  Scenario: The E button on a strip opens the instrument's own window
    # gate: "E opens a 320x200 editor parented to its own window"
    Given the CLAP fixture on track 0
    When the producer clicks E on track 0's strip
    Then a 320 x 200 window opens, and the editor is embedded in it
    And E is lit while the window is open

  Scenario: An open editor survives a rebuild of the audio graph
    # core: survives_adoption; gate: "adding a track keeps the same window, nothing destroyed"
    Given an open editor on track 0
    When a track is added
    Then the same window stays open on the same instrument, and nothing is destroyed or created

  Scenario: Swapping an instrument closes its editor
    # core: survives_adoption; gate: "an instrument swap closes its editor, the other stays"
    Given open editors on two tracks
    When one track's instrument is swapped for a SoundFont
    Then that track's editor closes and the other stays open

  Scenario: Opening a saved project opens no windows
    # gate: "save and load opens no windows and keeps LEVEL 0.60"
    Given a session saved with an editor open and the level turned to 0.60
    When it is opened again
    Then no plugin window opens and 0.60 is heard

  Scenario: EDITOR in the instrument panel opens the selected track's editor
    # gate: "EDITOR in the instrument panel opens the selected track's editor"
    Given track 0 selected
    When the producer clicks EDITOR
    Then track 0's editor opens

  Scenario: A VST3 editor is refused without an X11 display
    # core: vst3_refused; gate: "a VST3 editor is refused offscreen with a clear message"
    Given the VST3 fixture and no X11 display
    When the producer asks for its editor
    Then it is refused with "Plugin window needs an X11 display"
    And the instrument keeps playing

  Scenario: With no display at all, asking for a VST3 editor is refused at once
    # nodisp
    Given the VST3 fixture, and DISPLAY unset and then empty
    When its editor is asked for, embedded and floating
    Then each is refused with "Plugin window needs an X11 display" before JUCE touches X
    And the instrument keeps playing

  Scenario: On a real X11 display the VST3 editor is drawn inside the host window
    # x11
    Given a real X server and the xcb platform
    When the VST3 fixture's editor is opened
    Then it is embedded in a host window of 320 x 200 physical pixels
    And the centre pixel grabbed from the server is the editor's #C8FF3C
    And closing the window closes the editor

  Scenario: An open editor costs the audio thread nothing
    # rt
    Given the CLAP fixture with its editor open and its timer running
    When a thousand blocks are processed while its knob is turned ten times
    Then nothing is allocated or freed on the audio thread, and every turn is heard

  Scenario: Edits made in plugin windows flow through the engine without allocating
    # rt (realtime_plugin_windows)
    Given the CLAP synth with its editor open on track 0 and the VST3 fixture on track 1
    When their knobs are turned ten times while SongEngine renders a thousand blocks
    Then nothing is allocated or freed in the production process()
    And every turn reaches the engine's edit ring with its processor's address,
    And stamped inside the block it came out of, and the last turns are what both tracks play
