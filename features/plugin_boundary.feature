Feature: Plugins talk back to the host through one boundary
  CLAP and VST3 plugins describe their parameters, report the edits made in
  their own windows, and tell the host their latency and tail, through the
  production adapters. Each scenario is proved by the CTest test named after
  it (blokkily_plugin_boundary_tests <case>, or blokkily_realtime_checks).

  # plugin_clap_parameters, plugin_vst3_parameters
  Scenario: A plugin lists its parameters
    Given the CLAP fixture loaded through its official clap_entry
    When the host asks it for its parameters
    Then it lists Level (id 0, default 0.25), Tone (id 1) and VelocityMode (id 2)
    And each spans 0 to 1 and is automatable
    And the VST3 fixture lists Level, Tone and its wrapper's Bypass, without hidden MIDI CC parameters

  # plugin_clap_gui_turn
  Scenario: A knob turned in a CLAP plugin's window reaches the host as one gesture
    Given the CLAP fixture playing a note at level 0.25
    When its window turns Level to 0.6 and asks the host to flush
    Then the next block renders 0.6 from its first sample
    And the host takes exactly a begin, a value of 0.6 and an end for parameter 0
    And an inactive plugin is flushed by idle() on the main thread instead

  # plugin_vst3_turn_edits, plugin_vst3_turn_base (regression)
  Scenario: A knob turned in a VST3 plugin's window reaches the host as one gesture
    Given the VST3 fixture playing a note at level 0.25
    When its window turns Level to 0.7 through the component handler
    Then within two seconds the host takes a begin, a value of 0.7 and an end
    And the note renders at 0.7
    And a +0.1 modulation afterwards renders 0.8, not the stale 0.35
    And the host's own automation is never reported back as an edit

  # plugin_vst3_load_state_base (regression)
  Scenario: Modulation after a state load adds to the loaded value
    Given a running VST3 fixture at its default level 0.25
    When a state holding level 0.7 is loaded into it
    And a +0.1 modulation arrives
    Then it renders 0.8

  # plugin_clap_thread_check
  Scenario: A CLAP plugin can ask which thread it is on
    Given the CLAP fixture asks the host's thread-check
    Then during init it is on the main thread and not the audio thread
    And during process() on an audio thread it is on the audio thread and not the main thread

  # plugin_clap_request_callback
  Scenario: A CLAP plugin's main-thread callback runs on the main thread
    Given the CLAP fixture requests a callback from another thread
    Then on_main_thread has not run
    When the host's main thread calls idle()
    Then on_main_thread runs once, on the main thread

  # plugin_clap_input_wiring
  Scenario: An effect hears its input; an instrument is given none
    Given a CLAP effect with a stereo input and the CLAP instrument fixture
    When each processes a block that holds a signal on entry
    Then the effect's output is exactly half its input, read from a separate copy
    And the instrument reports no audio input, is never handed one, and renders its own level

  # plugin_clap_state
  Scenario: A project saved before plugin parameters existed still opens
    Given the old 4-byte CLAP fixture state holding level 0.6
    When it is loaded
    Then the fixture plays at 0.6
    And the current state carries level, tone and velocity mode and restores all three

  # plugin_clap_latency_tail, plugin_vst3_parameters
  Scenario: A plugin reports its latency and tail
    Given the CLAP fixture announces a latency of 64 samples
    When the host polls latency_changed()
    Then it is told once, and latency_samples() reports 64
    And an announced tail of 4800 samples is reported after idle(), and INT32_MAX means endless

  # plugin_clap_transport
  Scenario: A plugin is told where the song is
    Given the host sets the transport to 137.5 BPM, beat 10.25, bar 3
    When the CLAP fixture processes a block
    Then it sees that tempo, beat position and bar

  # realtime_plugin_edits
  Scenario: Reporting a plugin's own edits does not allocate on the audio thread
    Given the CLAP and VST3 fixtures each processing 1000 blocks with the transport set
    And their windows turning a knob every hundred blocks
    When each block is processed and its edits taken
    Then no call allocates or frees memory
    And every turn is heard and taken as one gesture
