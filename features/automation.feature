Feature: Record everything, part 2 - automation
  A track's lanes move its strip (gain, pan, mute) and the parameters of its
  instrument and of any insert effect, on the track, on a return or on the
  master (decision 11). Each track has its own automation mode (decision 3):
  OFF ignores its lanes, READ plays them, TOUCH lets a control the producer
  holds override its lane and records the hold, LATCH keeps recording from
  the first touch until the transport stops, WRITE records the strip for the
  whole pass. A fader dragged, a mute clicked, or a knob turned in a
  plugin's own window while the song plays is recorded where it was heard,
  and a pass of the transport - its notes and its automation - is one step of
  history. Strip lanes are precomputed when the arrangement is compiled, so
  the render callback never takes a power or a sine; parameter lanes are
  events on the timeline, chased to the song's position on every seek.

  Each scenario names the check that proves it: `automation_<case>` runs
  `blokkily_automation_tests <case>` (tests/automation_tests.cpp) against the
  real CLAP instrument and effect fixtures, reading what the production
  SongEngine::process() rendered, or a bounce read back from disk and
  compared sample for sample with that render; `realtime_automation` runs
  blokkily_realtime_checks; the `bdd_automation` gate drives the real
  application (`blokkily --verify --scenario automation`,
  src/app/verify/scenario_automation.cpp) and its reached() labels are quoted.

  # automation_gain_ramp_bounce
  Scenario: A gain lane is heard, and bounced, as a ramp
    Given a CLAP track holding its level and a gain lane from -40 dB to 0 dB over two bars
    When the song is bounced and the file read back
    Then the file is what the engine renders live from the top, sample for sample
    And each of its eight quarter notes is louder than the one before
    And each sounds within 0.5 dB of the lane's level at its middle

  # automation_pan_and_mute_lanes
  Scenario: Pan and mute lanes move the track around and off the bus
    Given a track panned 0.3 with a pan lane hard left in bar 1 and hard right in bar 2
    And a mute lane that mutes the second half of bar 2
    When the song plays
    Then bar 1 is heard on the left only, bar 2 on the right only, and its second half not at all
    When the track's automation mode is OFF
    Then the static pan of 0.3 plays and nothing is muted

  # automation_touch_override
  Scenario: Holding a fader in touch mode overrides its lane without a recompile
    Given a track in TOUCH with a gain lane at -20 dB, playing
    When its fader is taken hold of at 0 dB
    Then the very next block is heard at 0 dB, with nothing recompiled
    And the hold and its release come back from the engine stamped with their block's sample and tick
    When it is let go
    Then the next block is back on the lane at -20 dB
    And in READ a held fader does not override the lane
    And in LATCH the value it was let go at holds until the transport stops

  # automation_strip_moves_replay
  Scenario: Recorded fader moves replay as they were heard
    Given a track in TOUCH with a flat gain lane at 0 dB, playing
    When its fader is held, stepped down to -6, -12 and -18 dB and let go
    Then the moves recorded become one pass of the gain lane
    And played again untouched, every block sounds as it did live
    # A move is a step when it comes more than 24 ticks after the one before
    # (a control held still, then moved), and a ramp when closer (a control
    # being dragged). The envelope reaches a step across the 256 samples
    # before it, so the replay is compared over each block's first half.

  # automation_plugin_moves_lane
  Scenario: A knob turned in the plugin's own window becomes a lane
    Given the CLAP fixture on a track in LATCH, playing, at its default level 0.25
    When its level knob is turned to 0 in its own window (blokkily_test_gui_turn)
    Then the fixture falls silent
    And when the transport stops, a lane on the instrument's level holds 0.25, drops to 0 at the turn, and returns at the stop
    And the bounce, read back, is silenced exactly from the turn to the stop and equals the live render

  # automation_chase_on_seek
  Scenario: A seek chases every parameter lane to where the song now is
    Given lanes on the instrument's level (0.1 then 0.4) and on its insert's gain (1 then 0.5), jumping at bar 2
    When the playhead is sent into bar 2
    Then both lanes are there at once: level 0.4 at gain 0.5
    When it is sent back into bar 1, where no event lies
    Then both are chased back
    And starting the transport again chases them too

  # automation_effect_lane_on_return
  Scenario: Effect parameters are automatable on returns and the master
    Given a track panned left sending post-fader to a return panned right
    And a lane on the return's insert gain (0.2 then 0.8) and one on a master insert (1 then 0.5)
    When the song plays
    Then the right of the bus follows the return's lane and both sides follow the master's

  # automation_round_trip
  Scenario: Lanes and modes are saved and loaded
    Given tracks in TOUCH and LATCH with gain, pan, mute and parameter lanes on the instrument, an insert, a return and the master
    When the project is saved and loaded
    Then every lane and mode comes back as it was, and saving again writes the same text
    And a mute point outside 0..1 is refused

  # automation_solo_with_lane
  Scenario: Solo silences an automated track that is not soloed
    Given two tracks, the right one automated to -6 dB
    When the left one is soloed from the mixer, without a recompile
    Then the automated track is silent
    And soloed itself, it plays at its lane alone

  # automation_post_fader_send
  Scenario: A post-fader send follows the automated fader
    Given a track panned left with a gain lane (-20 dB then 0 dB) sending post-fader to a return panned right
    When the song plays
    Then the return is heard at the lane's gain, as the direct path is
    And sent pre-fader, it ignores the lane
    And muted by a mute lane, the track sends nothing at all

  # automation_recorder_passes
  Scenario: Passes are recorded by mode, split at the loop, and thinned
    Given a track in TOUCH
    When a fader is held, moved to -6 dB and let go
    Then the lane steps to -6 dB between the hold and the release and resumes afterwards
    And a hold that never moved makes no lane, and READ records nothing
    And in LATCH a pass over the loop point is written either side of it
    And a straight sweep of a hundred moves is thinned to a handful of points
    And a return insert's knob is recorded into the track that holds its lane, and nowhere when none does

  # realtime_automation
  Scenario: Automation keeps the real-time rules
    Given strip, instrument, insert, return and master lanes, and a track in TOUCH
    When a thousand blocks play through seeks, wraps, a stop, a recompile, solo moves, fader holds and a plugin knob turn
    Then not one render callback allocates or frees memory
    And the song still sounds, and the moves and the knob turn came back from the engine

  # gate steps "automation: one CLAP track, a key on every sixteenth",
  #            "automation: the mode chip steps READ to TOUCH"
  Scenario: A strip's automation mode is chosen from the strip
    Given a CLAP track in the mixer
    Then its strip shows an AUTO chip big enough to click, reading READ, and a lane editor under the tracks
    When the chip is clicked
    Then it reads TOUCH; a right click steps back to READ

  # gate steps "automation: a fader dragged in touch mode becomes a lane",
  #            "automation: the lane plays back what was heard"
  Scenario: A fader dragged while playing in touch mode becomes a lane
    Given the track in TOUCH, playing and recording
    When a key is performed and the rendered GAIN fader is pressed, dragged down in three steps and let go
    Then each step is heard at its gain as it is made
    And on release a gain lane holds the three steps and 0 dB either side
    And its points are drawn in the lane editor, each big enough to grab
    And the strip is back on the lane at 0 dB
    And played again, each step sounds as it did

  # gate step "automation: one undo takes back the take"
  Scenario: A take is one step of history
    Given the take of that pass: a note in the pattern and the gain lane
    When the producer undoes once
    Then the note, the lane and the fader's move are all gone, and redo brings them all back

  # gate step "automation: points drawn, moved and removed in the lane editor"
  Scenario: The lane editor draws, moves and removes points
    When the producer double-clicks on the lane in bar 2
    Then a point is added there at the value clicked
    When its handle is dragged up
    Then its value rises, as one step of history
    When it is right-clicked
    Then it is removed

  # gate step "automation: lanes and mode are saved and loaded"
  Scenario: The lane and the mode survive a save and a load
    When the project is saved, the mode changed, and the project opened again
    Then the lane is as it was and the chip reads TOUCH
