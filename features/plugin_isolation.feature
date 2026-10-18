Feature: Each plugin in a process of its own
  A CLAP, VST3 or LV2 plugin runs in a helper process of its own
  (blokkily_plugin_host), behind the same boundary every processor uses. A
  plugin that crashes or hangs costs only its helper: its track goes silent,
  a banner names it, and RELOAD loads it again from the last state it
  reported, while the window, the song and every other plugin play on.
  Audio crosses in shared memory one device period behind, so the audio
  thread never waits on another process; that period is reported as the
  plugin's latency and compensated like any other. An offline render waits
  for the helper, so a bounce is exactly what a punctual helper plays. On by
  default where the platform supports it (Linux); SETTINGS ·
  PLUGINS · ISOLATE turns it off.

  # plugin_isolation_clap, plugin_isolation_vst3
  Scenario: An isolated plugin plays what it plays in the window, one block later
    Then the real CLAP and VST3 fixtures, isolated, play a note at its sample offset, sample for sample as they do in the window, one block later

  # plugin_isolation_period
  Scenario: Behind a device that asks for long periods, a whole period behind
    Then with a 2048-frame period the plugin reports 2048 more latency and plays a period later, sample for sample

  # plugin_isolation_effect, plugin_isolation_macro
  Scenario: Parameters and modulation reach an isolated plugin in time
    Then an isolated effect hears values and modulation where they land, and a macro turned while the song plays reaches an isolated instrument on the same sample, one block later

  # plugin_isolation_state
  Scenario: Its parameters and state cross with it
    Then it lists its parameters, a moved parameter reads back, and its state saves through the helper and loads anywhere

  # plugin_isolation_compensated
  Scenario: The engine lines an isolated instrument up with the others
    Then an isolated instrument and the same one in the window, playing the same note, land on the same sample of the bounce

  # plugin_isolation_crash
  Scenario: A plugin that crashes costs only its helper
    When the plugin's own bug crashes its process
    Then this process carries on, the plugin is noticed failed and plays silence, and its last state is kept

  # plugin_isolation_hang
  Scenario: A plugin that hangs is stopped
    When the plugin never returns from processing
    Then the audio thread is never held up, the plugin is heard as silence, and after a few seconds it is stopped

  # plugin_isolation_refused
  Scenario: A plugin that will not load is refused
    Then a missing plugin, or a missing helper, is refused with a reason

  # plugin_isolation_editor
  Scenario: An isolated plugin's editor opens, and is closed with it
    Then its editor opens in the helper, and its window is told when the plugin goes or crashes

  # audio_device_isolated, audio_device_isolated_jack
  Scenario: Isolated plugins keep up with a real device
    Given an audio server is running (skipped where none is)
    Then two isolated synths hold a note through it in real time without a silent callback

  # bdd_plugin_isolation
  Scenario: A crash in the window
    Given the test synth on one track and a synth that crashes on cue on another, each in its own process
    When the song plays the note that crashes it
    Then the window keeps running, the other track still plays, its track is silent, and a banner names it with RELOAD
    When RELOAD is pressed
    Then the plugin is loaded again and plays
    And SETTINGS · PLUGINS · ISOLATE moves plugins in and out of their processes
