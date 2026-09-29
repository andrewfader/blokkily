Feature: Modulators move plugin parameters while the song plays
  LFOs, macros and envelope followers belong to the song: they are saved with
  the project, undone with it, and checked by its schema. Each modulator is
  aimed at parameters of a track's instrument or of an insert on any bus. What
  it sends is a modulation, never automation: an offset on top of the
  parameter's own value, equal to the modulator's signal (an LFO -1..+1, a
  macro 0..1, a follower's level 0..1) times the target's depth (-1..+1) times
  the parameter's range. Modulators on one parameter add up. The engine
  compiles them from the song with the arrangement; a rate, a shape, a macro
  or a depth reaches the running engine live, like a fader.

  Executable: tests/dynamic_modulation_tests.cpp (CTest
  dynamic_modulation_<case>), tests/realtime/dynamic_modulation.cpp
  (realtime_dynamic_modulation), and the bdd_modulation gate
  (src/app/verify/scenario_modulation.cpp).

  # dynamic_modulation_lfo_shapes
  Scenario: An LFO's shapes
    Given an LFO with each shape
    When it is read through its cycle
    Then the sine and triangle peak at a quarter cycle, the saws start at their extremes
    And the square is high for the first half
    And the random shape holds one repeatable value per cycle

  # dynamic_modulation_envelope_follower
  Scenario: An envelope follower tracks a level
    Given an envelope follower with an attack and a release time
    When a loud block follows silence, and silence follows it
    Then its level rises to the block's peak with the attack
    And falls back with the release

  # dynamic_modulation_lfo_reaches_clap
  Scenario: An LFO moves a CLAP instrument's parameter
    Given the CLAP fixture on a track holding a key, and a 1 Hz square LFO at 0.2 depth on its Level
    When the song plays
    Then the fixture receives a CLAP_EVENT_PARAM_MOD every block, its last amount -0.2
    And the rendered level swings between 0.45 and 0.05
    And in the block where the LFO turns, the samples before a note later in that block already hear the new modulation

  # dynamic_modulation_lfo_tempo_sync
  Scenario: A synced LFO's cycle follows the tempo map
    Given a square LFO synced to one beat, aimed at the CLAP synth's Level, whose free rate is 7 Hz
    And a song at 120 BPM for two bars and 60 BPM after
    When the song plays
    Then the level turns every 12000 samples at 120 BPM and every 24000 at 60 BPM
    And the fixture receives modulations of +/- the depth
    And after the tempo is edited to 240 BPM and recompiled it turns every 6000 samples

  # dynamic_modulation_macro_live
  Scenario: A macro is turned while the song plays
    Given two macros on the CLAP instrument's Level, one also on a CLAP insert's Gain
    When the song plays and a macro value or a depth is changed
    Then the offsets on one parameter add up to one event
    And each is scaled by its parameter's range
    And the change is heard in the next block without a recompile or a released note

  # dynamic_modulation_follower_hears_source
  Scenario: A follower follows another track, whatever its fader
    Given a follower of a muted, faded-down track aimed at an earlier track's Level
    When the source track plays for half the bar
    Then the earlier track is ducked while the source plays, as the source renders first
    And the level comes back after the source stops
    And nothing of the muted source reaches the master

  # dynamic_modulation_bounce_matches_playback
  Scenario: An export has the modulation playback had
    Given an LFO, a follower and a macro, and a song that has played for a while
    When it is exported and the file is read back
    Then every sample equals what the engine renders playing from the top

  # dynamic_modulation_removed_modulation_releases
  Scenario: Removing a modulator lets go of its parameter
    Given a macro adding 0.3 to Level while the song plays
    When the modulator is removed and the song recompiled
    Then the plugin is sent a modulation of 0 and Level is heard at its own value

  # dynamic_modulation_song_model
  Scenario: Modulators are part of the song
    Given a song with an LFO, a macro, a follower and a keyed compressor
    When it is saved and loaded, or loaded from a file saved before modulation existed
    Then it reads back exactly, and the older file loads with no modulators
    And a target on a missing processor, an out-of-range value, a follower of nothing or a doubled target is refused
    And removing an insert or a track takes its targets and moves the rest to where their processors went

  # realtime_dynamic_modulation
  Scenario: Modulation keeps the real-time rules
    Given an LFO, a macro and a follower on CLAP instruments
    When a thousand blocks play across a seek, a stop, a live macro move and a recompile
    Then the render callback never allocates or frees

  # bdd_modulation
  Scenario: A producer adds and turns modulators in the application
    Given the application with the CLAP synth on a track
    When an LFO is added from the modulation panel, aimed at Level from its target menu, set to a square and given a rate and a depth with its dials
    Then the rendered audio swings by that depth
    And removing it brings the level back
    And a macro added and turned there is heard at once, without a recompile, and is undone and redone
    And the modulators survive a save and a load, and the export has the macro's level
    When an LFO's SYNC chip is clicked and 1/8 picked from its division picker
    Then the rate dial gives way to the division, live, without a recompile
    And the level turns up every 23.4 blocks of 512 at 120 BPM and every 46.9 after the tempo is set to 60
    When a follower is added, aimed at the bass's Level, and KICK picked from its source picker
    Then the song is recompiled, not rebuilt
    And holding a key on the muted, faded kick raises the bass by half the kick's level, and letting go brings it back
    And the modulation panel and its dials, the SYNC chip, the division and source pickers have a usable size
