Feature: Edit a musical pattern through synchronized projections
  A producer should be able to choose the representation that suits the task
  without converting or duplicating musical data.

  Scenario: A step edit is immediately shared by every editor
    Given Blokkily is running with its demonstration pattern
    And the pattern contains 4 events
    When the producer activates step 2
    Then the canonical pattern contains 5 events
    And step 2 is lit in the rendered step grid
    And step 3 is not lit
    And the tracker is backed by the same canonical pattern
    And the piano roll is backed by the same canonical pattern
    And a non-empty 1280 by 800 screenshot is captured

  Scenario: Every editor agrees about the selected step
    Given the producer selects step 9, which carries a note and a parameter lock
    Then the step grid marks that cell as selected and shows its lock badge
    And the tracker highlights row 08 and shows the lock in its FX column
    And the piano roll draws that note in the selection colour
    And the inspector names the note and offers its lock for editing

  Scenario: The view switcher focuses one editor
    Given all three projections are on screen
    When the producer chooses the TRACKER view
    Then the tracker is shown and the piano roll is hidden
    When the producer chooses the PIANO view
    Then the piano roll is shown and the tracker is hidden
    When the producer chooses ALL
    Then both editors are shown again

  Scenario: The inspector edits reach every projection
    Given step 9 is selected
    When the producer transposes it up an octave
    Then the inspector, the grid, the tracker, and the roll all read D3
    When the producer sets its velocity to half
    Then every projection reports 64 MIDI units for that step

  Scenario: An empty step invites a note instead of showing dead controls
    Given the producer selects a step with no note
    Then the inspector says the step is empty and how to fill it

  Scenario: One playhead is shared by every editor
    Given the transport is located at step 6
    Then the position reads 1.2.3 as bar, beat, and sixteenth
    And the step grid, tracker, and piano roll all mark step 6
    When the transport passes the end of the bar
    Then the editors wrap to the first step and the song moves to bar 2
    When the transport passes the end of the song
    Then it wraps to the first bar

  Scenario: The playhead is the engine's position
    Given an arrangement prepared at 120 BPM and 48000 Hz
    When the engine has rendered 96000 samples
    Then the transport reads 2.1.1, one bar in
    And the interface does not advance the playhead on a clock of its own

  Scenario: A native CLAP module is discovered
    Given Blokkily is running with a valid CLAP fixture module
    When the CLAP catalog initializes the module entry point
    Then exactly one plugin descriptor is listed
    And no scan failure is reported

  Scenario: A native CLAP instrument processes sample-accurate events
    Given Blokkily instantiated and activated the discovered CLAP instrument
    When a note-on is scheduled at sample 64
    Then samples 0 through 63 are silent
    And sample 64 produces audio
    And automation and modulation remain distinct CLAP events
    And the plugin state round-trips through CLAP streams

  Scenario: The pattern transport drives the plugin graph
    Given a 120 BPM pattern with 480 ticks per beat
    And middle C begins at tick 120 and ends at tick 240
    When the real-time engine renders at 48000 samples per second
    Then the CLAP instrument starts at sample 6000
    And the CLAP instrument stops at sample 12000
    And stopping transport clears the output

  Scenario: A VST3 instrument uses the shared plugin boundary
    Given the test suite built a real VST3 bundle
    When Blokkily scans and instantiates that VST3 through JUCE
    And a note-on is scheduled at sample 32
    Then samples 0 through 31 are silent
    And sample 32 produces audio
    And the VST3 state can be saved and restored

  Scenario: The plugin browser lists both native formats together
    Given Blokkily is running with a CLAP fixture and a VST3 fixture
    When the producer scans for plugins
    Then the browser lists the CLAP instrument tagged CLAP
    And the browser lists the VST3 instrument tagged VST3
    And both entries show the plugin name and its vendor

  Scenario: Installed instruments are discovered when the app starts
    Given CLAP, VST3, SF2, and SF3 instruments are installed in their standard directories
    When Blokkily starts
    Then it scans the standard instrument directories without requiring a button press
    And the browser lists every discovered CLAP, VST3, and SoundFont instrument

  Scenario: A parameter lock changes how its own step sounds
    Given a step at tick 120 carrying a level automation lock of 0.75
    When the real-time engine renders the pattern at 120 BPM and 48000 Hz
    Then samples before 6000 are silent
    And sample 6000 onward sounds at 0.75 rather than the previous level
    And a step suppressed by its loop condition emits no parameter change

  Scenario: Automation and modulation stay distinct through every format
    Given a step carrying both an automation lock and a modulation lock
    When the engine sends them to a CLAP and to a VST3 instrument
    Then each arrives as its own event type
    And modulation offsets the automated value without replacing it
    And a later automation event does not discard the modulation

  Scenario: VST3 automation is sample-accurate
    Given a VST3 instrument playing at its default level
    When a level automation event is scheduled at sample 32
    Then samples 0 through 31 keep the previous level
    And sample 32 onward uses the new level

  Scenario: A session survives being saved and reloaded
    Given Blokkily verified a CLAP, a VST3, and a SoundFont instrument
    And the canonical pattern contains 5 events
    When the producer saves the project and then edits the pattern further
    And the producer reloads the saved project
    Then the canonical pattern contains 5 events again
    And the later edit is gone
    And all 3 instrument slots are restored with their plugin state

  Scenario: Project persistence is available in the real interface
    Given Blokkily is running normally
    Then the project rail offers Open and Save controls
    And the standard Open and Save keyboard shortcuts invoke those workflows

  Scenario: A discovered instrument can drive audible playback
    Given the browser lists an installed CLAP, VST3, or SoundFont instrument
    When the producer selects it
    Then Blokkily prepares it with the canonical pattern
    And Play drives the production audio callback rather than only the visual playhead
    And an unavailable audio device is reported without crashing

  Scenario: A reloaded plugin keeps the state it was saved with
    Given a CLAP instrument whose level parameter was set to 0.375
    When its state is written to a project file and read back
    Then the bytes are unchanged
    And a freshly created instance loaded with that state renders at 0.375

  Scenario: A damaged project file is refused
    Given a project file with an unknown record or a bad version
    When Blokkily loads it
    Then no project is returned
    And the reason is reported

  Scenario: A bundled-format SoundFont produces audio
    Given Blokkily is running with a valid SF2 fixture
    When the internal SoundFont instrument receives middle C
    Then it renders a non-silent stereo audio block
    And its selected file and preset can be restored from project state
