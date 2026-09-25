Feature: One timebase for the whole song
  A song has a tempo map and a meter map. The engine, the bounce, the
  transport and recording all place ticks through them, so a tempo change,
  a ramp or a 7/8 bar sounds, exports, reads out and records the same way.
  (Plan F-A, item 1.2. The tempo lane and meter menu are item 2.1.)

  # tests/timebase_tests.cpp: timebase_tempo_map_constant
  Scenario: A song with one tempo keeps the arithmetic it always had
    Given a new song at 120 BPM with 480 ticks a beat at 48 kHz
    Then a beat lasts half a second and a tick 50 samples
    And every sample reads back as the same tick the single-tempo formula gave

  # timebase_tempo_map_step, timebase_engine_step
  Scenario: A tempo step changes the tempo from its tick on
    Given a song at 120 BPM that steps to 60 BPM at the start of bar 2
    When the CLAP fixture plays a note on every beat
    Then the beats of bar 1 sound 24000 samples apart
    And the beats of bar 2 sound 48000 samples apart, from sample 96000

  # timebase_tempo_map_ramp, timebase_engine_ramp
  Scenario: A ramp glides the tempo
    Given a song that ramps from 60 BPM to 180 BPM over two bars
    When the CLAP fixture plays a note every eighth
    Then each note sounds within a sample of where integrating the ramp puts it
    And the notes come closer together as the tempo rises

  # timebase_meter_map, timebase_engine_seven_eight
  Scenario: A 7/8 bar is shorter
    Given a song in 4/4 that changes to 7/8 at bar 2
    When a clip is placed on the first beat of each of four bars
    Then the downbeats sound at samples 0, 96000, 180000 and 264000
    And tick 3600 reads as bar 3, beat 1, sixteenth 1

  # timebase_tempo_map_edits
  Scenario: A tempo map that makes no sense is refused
    Given a tempo map with a tempo outside 20 to 300 BPM, two points on a tick, or no point at tick 0
    Then the song is inconsistent and the engine refuses to prepare it

  # timebase_rebar
  Scenario: Changing a meter keeps clips and tempo points on their bars
    Given clips and tempo points placed in bars 1 to 4 of a 4/4 song
    When bar 2 onwards becomes 7/8
    Then every clip and tempo point is in the same bar, at the same offset into it
    And an offset that no longer fits its bar lands on the bar's last tick

  # timebase_song_length_audio
  Scenario: An audio clip ends where its seconds run out
    Given a two-second audio clip that starts one second before a step to 60 BPM
    Then the song ends 480 ticks after the step

  # realtime_tempo_edit_keeps_bar (regression)
  Scenario: A tempo edit while the song plays keeps its bar
    Given a song playing at 120 BPM in beat 3 of bar 1
    When the tempo is halved while it plays
    Then the playhead stays on the tick it had reached
    And the next beat sounds where the new tempo puts it from there
    And the render callback does not allocate while the playhead moves

  # timebase_engine_playhead
  Scenario: A seek taken with a tempo change lands where it was sent
    Given a playing song whose tempo is changed and which is sent to a sample in the same block
    Then the playhead is where the seek put it
    And a stopped playhead keeps its tick across a tempo change
    And an edit that leaves the tempo alone leaves the playhead alone

  # timebase_bounce_across_tempo
  Scenario: The bounce of a song with a tempo step and a ramp is what plays
    Given a song with a tempo step and a ramp
    When it is bounced to a file and the file is read back
    Then every sample equals the live render through the production callback
    And every beat is where the tempo map puts it

  # timebase_recording_across_tempo; bdd_midi_recording "a take across a tempo change"
  Scenario: A take recorded across a tempo change lands on the ticks played
    Given a song that slows to 60 BPM at bar 2, armed to record
    When a key is played and released in bar 2
    Then the take is read through the tempo map onto the ticks where it was heard
    And it is written on the step it was played in and plays back there
    And the transport reads 60.00 BPM and bar 2 while it plays there

  # timebase_soundfont_across_tempo, timebase_vst3_across_tempo
  Scenario: Real instruments follow the tempo map
    Given a note after a tempo step
    When FluidSynth plays it from a real SoundFont, or the VST3 fixture through the JUCE host adapter
    Then it is heard, not silent, from the sample the tempo map puts it at

  # timebase_project_records
  Scenario: Tempo points and meters are saved and loaded
    Given a song with tempo steps, a ramp and meter changes
    When it is saved and loaded
    Then the maps come back as they were and the file saves byte-identically
    And a legacy "tempo" line is one tempo point at tick 0
    And a file that mixes a "tempo" line with tempo_point records is refused
