Feature: A compressor keyed from another track
  An insert's sidechain key names a track (EffectSlot::sidechain, saved with
  the song). The key is that track's signal after its inserts and before its
  fader, so a muted or faded-down kick still ducks the bass. The engine renders
  every key's source before the track it keys, whatever their places in the
  song, and hands each insert the part of the key that lines up with the
  samples it is processing. Keys may not form a loop, and a track's insert
  cannot key from its own track. The built-in compressor listens to its key.

  Multi-output plugin routing (auxiliary output ports broken out to mixer
  tracks) is not implemented; see docs/plans/phase2-program.md.

  Executable: tests/sidechain_tests.cpp (CTest sidechain_<case>),
  tests/realtime/sidechain.cpp (realtime_sidechain), the bdd_modulation gate
  (src/app/verify/scenario_modulation.cpp), and dynamic_modulation_song_model
  for the schema and project records.

  # sidechain_key_pre_fader
  Scenario: A muted, faded-down key still ducks, and renders first
    Given a compressor on track 0 keyed from track 1, which is muted and at -60 dB
    When track 1 plays full scale
    Then track 0 is 15 dB quieter on the master bus
    And with the key silent track 0 passes untouched, and comes back after the key stops

  # sidechain_key_aligned
  Scenario: The key lines up with the samples it keys
    Given a compressor whose key is silent for the first half of a block and loud for the second
    When a parameter event at the middle splits the block
    Then the first half is untouched and the second half is ducked

  # sidechain_bounce_includes_sidechain
  Scenario: An export is ducked as playback was
    Given a keyed compressor and a key track of bursts
    When the song is exported and read back
    Then every sample equals the live render, and the export pumps with the key

  # dynamic_modulation_song_model
  Scenario: A key the engine could not play is refused
    Given a song with a keyed compressor
    When a key names its own track, a missing track, or closes a loop
    Then the song is refused, and a file holding it does not load

  # realtime_sidechain
  Scenario: A sidechain keeps the real-time rules
    Given a keyed compressor whose key comes from a later, muted track
    When a thousand blocks play across a seek and a recompile that moves the key
    Then the render callback never allocates or frees, and the key ducked

  # bdd_modulation
  Scenario: A producer keys a compressor from the rack
    Given the application with a compressor inserted on the bass, and a muted, faded kick
    When the rack's SIDECHAIN chip is clicked and "Key from KICK" chosen
    Then the song is recompiled, not rebuilt, and the chip names KICK
    And the bass is ducked while a key is held on the kick
