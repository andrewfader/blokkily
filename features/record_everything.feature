Feature: Record everything, part 1 - armed tracks and on-screen surfaces
  A producer arms the tracks they want to play with the R on each mixer strip
  and plays: a MIDI keyboard, the on-screen piano, or the tracker's note keys.
  Every armed track hears what is played and, with the song recording, writes
  it into the pattern under its playhead. With no track armed, the selected
  track takes what is played, as it always has. A track can listen to one MIDI
  channel of the keyboard or to all sixteen. Arm and channel are saved with
  the song, but they are how the session is wired rather than the music, so
  undo and redo never change them (plan item 2.5; decisions 3, 4 and 5).
  Automation has its own per-track mode and is part 2, features/automation.feature.

  The engine and routing scenarios are proved by blokkily_record_tests
  (tests/record_tests.cpp, one CTest test per case: record_<case>) from the
  left and right halves of what the production render callback rendered, with
  the real CLAP fixture on a track panned hard left and one panned hard right.
  The interface scenarios are exercised by the bdd_record_everything gate
  (`blokkily --verify --scenario record`, src/app/verify/scenario_record.cpp),
  which names the scenario it failed in ("BDD FAIL at: record: ..."). The
  realtime_record_fanout case (tests/realtime/record_fanout.cpp) proves the
  render callback stays allocation-free with sixty-four armed tracks.

  # record_routes
  Scenario: Arming decides where played notes go
    Given a song of several tracks with none armed
    Then every MIDI channel and the on-screen surfaces play the selected track
    When two tracks are armed, one of them on channel 10 only
    Then every channel plays the omni track and channel 10 plays both
    And the on-screen surfaces play both armed tracks
    And a track armed for audio only, or past the sixty-fourth, takes no notes

  # record_armed_tracks; gate step "record: a piano click is heard on both armed tracks"
  Scenario: One key reaches every armed track
    Given two tracks with the CLAP instrument, panned hard left and hard right
    And both are armed
    When a key goes down on the keyboard
    Then the left and the right of the rendered bus both carry it
    When it is let go
    Then both fall silent
    When the left track is disarmed and a key is played
    Then only the right of the bus carries it

  # record_channel_masks
  Scenario: A track hears only its own MIDI channel
    Given the left track listens to channel 1 and the right track to channel 2
    When a key is played on channel 1
    Then only the left of the bus carries it
    When the same key is played on channel 2
    Then only the right carries it
    And a key on channel 3 is not heard at all
    And the same key held on two channels is two keys, released one at a time

  # record_release_follows_note_on
  Scenario: A key's release follows the tracks it went down on
    Given a key held on the left track
    When the left track is disarmed and the right track armed before it is let go
    Then letting it go silences the left track
    And the right track never sounds it
    And re-channelling a track under a held key does not strand the key either

  # record_pending_release
  Scenario: A release the queue has no room for is owed, not lost
    Given a key held on both tracks and the input queue full
    When the key is let go
    Then the release is owed and the key keeps sounding
    When the next message arrives from the keyboard
    Then the owed release is sent first and both tracks fall silent

  # record_nothing_armed
  Scenario: With nothing armed, the selected track plays what is played
    Given no track is armed and the right track is selected
    When a key is played on any channel
    Then only the right of the bus carries it
    When the left track is selected
    Then only the left carries the next key
    When the right track is armed
    Then the armed track plays, not the selected one
    And a track armed for audio only does not take the notes from the selected track

  # record_perform_queue
  Scenario: The on-screen surfaces are input like a keyboard
    Given both tracks are armed
    When a key is performed from the interface on a stopped song
    Then both tracks sound it and nothing is recorded
    When the song plays and records and a key is performed beside a MIDI key
    Then each key is captured once for each armed track
    And at the sample and tick of the block that sounded it
    And each release is captured on the track that key went down on

  # realtime_record_fanout
  Scenario: Sixty-four armed tracks record without the callback allocating
    Given sixty-four tracks with the CLAP instrument, all armed
    When a MIDI keyboard and the on-screen surfaces play keys while recording
    Then every track sounds every key and every key is captured on every track
    And not one render callback allocates or frees memory

  # gate steps "record: the strips offer R and a channel",
  #            "record: two tracks armed from their strips"
  Scenario: Tracks are armed from their mixer strips
    Given two tracks, and the mixer strips on screen
    Then each strip shows an R and a channel chip big enough to click
    When the producer clicks the R of both strips
    Then both are lit and the keyboard is routed to both tracks
    And no step of history was added
    When they click the channel chip, and right click it
    Then it steps from ALL to CH 1 and back

  # gate steps "record: a piano click is heard on both armed tracks",
  #            "record: a tracker key is heard on both armed tracks",
  #            "record: both patterns hold both keys with their micro-timing"
  Scenario: On-screen surfaces record against the running transport
    Given two armed tracks playing different patterns, and recording
    And the tracker cursor on an empty step
    When the producer clicks a piano key and later types a tracker note key
    Then each is heard on both tracks while it is held
    And each lands in both patterns, on the step nearest where it was played,
    And off the grid by exactly as much as it was played off it
    And the tracker cursor has not moved and its step is still empty

  # gate step "record: stopped, a tracker key writes the step and advances"
  Scenario: Stopped, the tracker's keys still write steps
    Given the song is stopped
    When the producer types a tracker note key
    Then the selected step holds the note and the cursor moves to the next step

  # gate steps "record: undo and redo leave the arm alone",
  #            "record: arm and channel are saved and loaded"; record_project
  Scenario: Arm and channel are saved but are not history
    Given two armed tracks and a recorded take
    When the producer undoes the take and redoes it
    Then both tracks are still armed and the keyboard still reaches both
    And a track disarmed between undo and redo stays disarmed
    When the right track is set to channel 2 and the song is saved and loaded
    Then both tracks are armed and the right track listens to channel 2

  # wave2_take_on_heard_beat (regression; tests/wave2_fixes_tests.cpp)
  Scenario: A note struck on an audible beat is written on that beat
    Given a reference note on beat 2 heard through the CLAP effect's 64 samples of latency
    And an armed track
    When a MIDI key and an on-screen key are struck as the beat is heard, 64 samples after its sample
    Then both are captured on the beat's own sample and tick, not 64 samples late
    And written into the pattern and played back, they sound together with the beat
    # Song::record_offset_samples is not applied to notes: it corrects the audio
    # device's round trip, which a MIDI or on-screen event never takes.
