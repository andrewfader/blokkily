Feature: Audio clips follow the tempo, stretch and change pitch
  An audio clip can be warped (decision 7). With FOLLOW TEMPO on it has a
  source tempo, detected from its audio or typed, and every beat of the
  recording lands on a beat of the song across tempo steps and ramps, without
  its pitch changing. A clip also takes a plain stretch ratio, and a pitch
  shift in semitones and cents that keeps its length. A warped clip plays a
  rendition rendered by Rubber Band (GPL) in offline mode on a worker thread
  and kept in the asset cache, keyed by the file, the stretch map, the pitch
  and the engine rate, so the render callback only ever reads decoded memory.
  While its rendition renders the clip is silent and shown RENDERING: playing
  the unstretched file would put its beats and notes in the wrong places. An
  export waits for every rendition. (Item 3.6.)

  Executable: tests/clip_warp_tests.cpp (CTest clip_warp_<case>) through the
  production warp worker, render callback and bounce on real files;
  tests/realtime/clip_warp.cpp (realtime_clip_warp); and the gate
  bdd_clip_warp (src/app/verify/scenario_clip_warp.cpp), whose reached()
  labels are quoted below.

  # clip_warp_follow_constant
  Scenario: A click track at 100 BPM lands on a 120 BPM song's beats
    Given a click track recorded at 100 BPM, one click a beat, placed at tick 0 of a 120 BPM song
    And the clip follows the tempo from a source tempo of 100 BPM
    When the song plays through the production callback
    Then every click starts within 1 ms of a beat of the song
    And without warp the fourth click would be 0.3 s late

  # clip_warp_follow_tempo
  Scenario: A click track follows the song's tempo map, steps and ramps included
    Given a song at 120 BPM that steps to 90 BPM at beat 8 and ramps from 90 to 140 BPM over beats 12 to 16
    And a 100 BPM click track following the tempo from the song's second beat
    When the song is bounced and the file read back
    Then each of the 20 clicks starts within 1 ms of its beat under the tempo map
    And the clip ends on beat 21, where its twentieth recorded beat ends

  # clip_warp_follow_tempo
  Scenario: A warped clip exports exactly as it plays
    Given the same warped song
    When it is bounced and also played through the production callback
    Then the file read back is what the callback played, sample for sample

  # clip_warp_pitch_octave
  Scenario: A pitch shift of +12 semitones plays an octave up at the same length
    Given a two-second 1 kHz tone shifted by +12 semitones
    When the song plays and is bounced with half a second of tail
    Then the tone measures 2 kHz, with nothing left at 1 kHz
    And it sounds from its first frame to within 5 ms of two seconds, at full level to the end, then stops
    And the bounce read back is what was played

  # clip_warp_ratio_double
  Scenario: A stretch ratio of 2 doubles a clip's length and keeps its pitch
    Given a one-second 1 kHz tone stretched by a ratio of 2
    When the song plays and is bounced
    Then the song is two seconds long and the tone sounds for two seconds
    And it still measures 1 kHz

  # clip_warp_pending_silent
  Scenario: A clip is silent while its rendition renders, and never plays stale
    Given a warped clip whose rendition is not rendered yet, beside an unwarped clip of the same file
    When the song plays
    Then the warped clip is silent and the unwarped one plays
    When the rendition arrives and the arrangement is recompiled
    Then the warped clip plays it, without the engine being prepared again
    When the clip's pitch is edited
    Then the old rendition is not played

  # clip_warp_project_round_trip
  Scenario: A clip's warp survives a save and a load
    Given clips that follow the tempo, stretch and shift pitch, and one left alone
    When the project is saved and loaded
    Then every clip comes back with its warp and saving again writes the same bytes
    And a clip left alone writes no clipwarp record
    And a clipwarp record for a missing clip, twice for one clip, out of range, short, or changing nothing is refused

  # clip_warp_detect_bpm
  Scenario: A clip's source tempo is detected from its audio
    Given click tracks at 100, 128 and 87 BPM
    When their tempo is detected
    Then each is found exactly
    And silence has no tempo

  # clip_warp_cache_derived
  Scenario: A rendition is kept in the asset cache and let go when unused
    Given a clip stretched by 1.25
    When its rendition is rendered
    Then it is held in the asset cache under a key naming its file, stretch map, pitch and engine rate
    And it is let go once nothing plays it

  # realtime_clip_warp
  Scenario: Renditions render off the audio thread and swap in without the render callback allocating
    Given two warped clips prepared before their renditions exist
    When the warp worker renders them while the callback runs, and a second thread recompiles with and without them
    Then not one process() call allocates or frees
    And the clips are silent before the renditions and the octave-up rendition is heard after

  # bdd_clip_warp: "clip warp: the W button opens the clip's warp panel"
  Scenario: A clip's W button opens its warp panel
    Given a click track imported as a clip in the running application
    When its W button is clicked
    Then the warp panel opens at a usable size with FOLLOW TEMPO, source tempo, stretch and pitch

  # bdd_clip_warp: "clip warp: following the tempo detects 100 BPM and renders, silent meanwhile"
  Scenario: Following the tempo detects the clip's tempo and shows it rendering
    When FOLLOW TEMPO is clicked
    Then the source tempo reads 100.00, detected from the audio, as one step of history
    And the clip is veiled RENDERING and the panel says RENDERING
    And the production callback plays silence for it meanwhile

  # bdd_clip_warp: "clip warp: the click track plays on the song's beats"
  Scenario: The rendered clip plays on the song's beats
    When the rendition is ready
    Then the veil goes, the panel says READY, the clip ends on beat 12
    And the production callback plays every click within 1 ms of a beat of the 120 BPM song

  # bdd_clip_warp: "clip warp: typed pitch and stretch are heard an octave up and twice as long"
  Scenario: Typed pitch and stretch are heard
    Given a two-second 1 kHz tone clip with its warp panel open
    When 12 is typed into the semitones field
    Then the tone is heard at 2 kHz for two seconds
    When 2 is typed into the stretch field
    Then the clip reaches the start of bar 3 on the lane and is heard at 2 kHz for four seconds

  # bdd_clip_warp: "clip warp: undo and redo take the warp back and forth"
  Scenario: Undo and redo take the warp back and forth
    When the stretch and then the pitch are undone
    Then the tone is heard for two seconds, then at 1 kHz again
    And redo brings back both

  # bdd_clip_warp: "clip warp: the warp is saved and loaded"
  Scenario: The warp is saved and loaded in the application
    When the project is saved and loaded
    Then the file holds a clipwarp record for each warped clip
    And after loading the clicks are on the song's beats again

  # bdd_clip_warp: "clip warp: the export is what was played"
  Scenario: An export of warped clips is what was played
    When the song is exported
    Then the file read back is what the production callback plays, sample for sample
