Feature: Audio remains correct at the production callback boundary
  The tracker, piano roll, live keys and arrangement reach real instruments.

  Scenario: Live input keeps its sample order
    Given a playing song with a scheduled note at sample 100 and release at sample 200
    When a live note arrives at the start of the same callback
    Then the CLAP instrument sounds from sample zero
    And the scheduled release still silences it at sample 200

  Scenario: Dense patterns keep every note and release
    Given a pattern containing 300 short notes in one callback
    When the production callback renders the song or pattern preview through CLAP
    Then all 300 notes sound at their scheduled samples
    And every release leaves the expected silence

  Scenario: Preview accepts a larger device callback
    Given a VST3 pattern preview prepared for 128 frames
    When the production callback asks for 1024 frames
    Then all 1024 stereo frames contain the held instrument note

  Scenario: Invalid timing is refused before activation
    Given a song or pattern preview
    When its tempo or sample rate is non-finite or non-positive
    Then preparation fails before activating an instrument

  Scenario: Replacing an instrument preserves the correct state
    Given two CLAP tracks with different edited instrument levels
    When the first track is replaced with the real VST3 fixture
    Then its rendered audio uses the new instrument's default level
    And the second track retains its own saved instrument state

  Scenario: Unsupported simultaneous density is explicit
    Given a playing song with 256 events at the same sample on one track
    When an edit adds another simultaneous event beyond the supported limit
    Then recompilation returns an explanatory error
    And the previous arrangement still renders its notes and releases correctly
    And an edit through the real application preserves the running engine and playhead

  Scenario: Playback does not allocate in the render callback
    Given a song playing through the CLAP fixture with a MIDI input connected and recording
    When the production process call renders a thousand blocks across a seek and loop wraps
    Then no block calls operator new or operator delete
    And the rendered audio still follows the arrangement, the seek, and the keys pressed

  # Engine seams (plan F-D, item 1.1): tests/engine_seams_tests.cpp,
  # tests/realtime/engine_seams.cpp and the bdd_engine_seams gate
  # (src/app/verify/scenario_engine_seams.cpp).

  Scenario: A track without an instrument still plays through its strip
    Given a playing song with a CLAP track and a track with no instrument
    And a steady source on the bare track's clip stage
    When the production callback renders the song
    Then the bare track's source reaches the bus scaled by its strip gain and pan
    And muting it, soloing another track, or moving its fader changes the bus accordingly
    And a stopped song plays no clip region

  Scenario: The render callback keeps its seams allocation-free
    Given a song with a CLAP track, a bare track with a source, and a plugin that reports edits
    When the production process call renders a thousand playing and a hundred stopped blocks
    Then no block calls operator new or operator delete
    And every chunk's parameter edit reaches the edit ring
    And the bare track sounds through its strip exactly while the song plays

  Scenario: A processor's own parameter edits are stamped where they happened
    Given a plugin that reports a parameter edit seven samples into every block
    When the song is stopped with the playhead at sample 1000, and then plays
    Then each edit reaches the edit ring with its processor's address
    And it is stamped with the song sample it happened at and whether the song was rolling
    And a ring nobody drains drops edits instead of blocking the callback

  Scenario: Rebuilding the graph keeps the instruments that did not change
    Given two tracks playing the CLAP fixture, the first dialled to level 0.6
    When the second track is given the VST3 fixture and the graph is rebuilt
    Then the first track's instrument is the same instance, never destroyed or created again
    And it still plays at level 0.6
    And only the replaced instrument is destroyed

  Scenario: A deleted track does not hand its instrument to its neighbour
    Given tracks playing the same CLAP plugin at different levels
    When the first track is deleted
    Then each remaining track keeps its own instance and plays at its own level
    And its saved state is its own level

  Scenario: A running plugin is not sent a state it cannot take while running
    Given the CLAP, VST3 and SoundFont instruments loaded in a prepared engine
    When a state is pushed into each while it runs
    Then each refuses it and nothing is loaded
    And a processor that accepts running state is given it

  Scenario: The instrument a slot names is created by one factory
    Given the processor factory
    When a SoundFont slot is created
    Then the SoundFont player comes from the internal registry and plays a note as audio
    And CLAP and VST3 slots come from their adapters
    And an unknown format is refused with a reason
    And a registered internal processor is created with the project context and listed in the catalog

  Scenario: A burst of edits is one recompile of the latest song
    Given a song playing through the CLAP fixture
    When fifty edits are made in one turn of the event loop
    Then no recompile runs during the turn and exactly one runs after it
    And the engine plays the last edit and nothing from the earlier ones

  Scenario: A busy engine is asked again rather than losing the edit
    Given an engine that refuses a recompile because no arrangement slot is free
    When the recompile is requested
    Then it is tried again on each later turn until the engine accepts it
    And a refusal for any other reason is reported and not retried
    And retries stop after a bounded number of turns

  # Wave 2 review fixes: tests/wave2_fixes_tests.cpp and
  # tests/realtime/wave2_fixes.cpp.

  # wave2_handoff_probe (regression)
  Scenario: A recompile made while the callback takes an arrangement never overwrites it
    Given a recompiled arrangement queued for the render callback
    When another recompile lands at the very moment the callback takes the queued one
    Then the block that took it plays the queued arrangement
    And the next block plays the later one

  # wave2_handoff_stress
  Scenario: Recompiling from another thread while the callback runs plays every arrangement whole
    Given a clip whose level names the arrangement it belongs to
    When the control thread recompiles three thousand times while the callback renders
    Then every block plays one arrangement, never an older one after a newer one
    And no recompile is refused for want of a free slot, and the last one is what plays

  # realtime_wave2_fixes
  Scenario: The review fixes keep the real-time rules
    Given a CLAP synth through the CLAP effect and a delay, a reverb return, and an armed track
    When a thousand blocks render while another thread recompiles and keys are struck,
    And the processors are reset halfway, as a bounce resets them
    Then no block and no reset calls operator new or operator delete
    And every key-down is stamped 64 samples before the block it arrived in
