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
    Then no block allocates or frees heap memory
    And the rendered audio still follows the arrangement, the seek, and the keys pressed
