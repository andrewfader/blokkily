Feature: A track arpeggiator
  The ARP rows of the device panel switch the selected track's arpeggiator.
  While it is on, the notes held on the track (from the arrangement, the
  launcher, a keyboard or a MIDI port) play one at a time on a grid of
  1/4 to 1/32 (with triplets), UP, DOWN, UP/DN, in PLAYED order or at
  RANDOM, across one to four octaves, each held for its GATE of a step.
  LATCH keeps it going after the keys are let go, until a new chord. It sits
  before the instrument, so every format hears the same notes; its settings
  reach the running engine like a fader, and it is saved with the song.

  # arpeggiator_stage_up
  Scenario: A held chord plays up, one note a sixteenth
    Given C, E and G held on a track with its arpeggiator on at its defaults
    Then C, E, G and C sound at 0, 6000, 12000 and 18000 samples (120 BPM)
    And each ends half a step later
    And the same comes out whatever size the chunks are

  # arpeggiator_stage_modes
  Scenario: Modes, octaves, rates and gate
    Then DOWN walks from the top, UP/DN over two octaves turns at the ends
    And PLAYED keeps the order the keys went down, RANDOM plays only held keys, the same each time
    And at 1/8 it steps every 12000 samples, and a full gate ends one note where the next begins

  # arpeggiator_stage_release_and_latch
  Scenario: Letting go, latching and stopping
    When the keys are let go
    Then the arpeggio stops and its note ends with them
    But latched, it keeps going, and a new key after a full release replaces the chord at once
    And a stop lets go of what the latch kept
    And with the transport stopped it starts on the key and keeps its own time
    And a note-off for a key it never took reaches the instrument

  # arpeggiator_engine_render
  Scenario: The engine plays the arpeggio
    Given a CLAP track holding C, E and G for a bar, its notes as pitches
    When it is bounced with the arpeggiator on
    Then each sixteenth's first half is one key, in turn, alone, and its second half is silent
    And with it off the held note sounds through every step

  # arpeggiator_records
  Scenario: The arpeggiator is saved with the song
    Then an "arp" record holds its settings and loads as it was saved
    And a song that never used one saves no record, and a bad division is refused

  # realtime_arpeggiator
  Scenario: The arpeggiator keeps the real-time rules
    Then a thousand engine blocks through it, across settings changes, a seek, a stop and a start, allocate nothing

  # bdd_arpeggiator
  Scenario: ARP in the window
    Given a CLAP track holding a chord, in the real window
    When the producer presses ARP ON
    Then the export sounds the first half of each sixteenth and is silent in the second
    And the pickers show the mode, rate and octaves the song holds, LATCH toggles, and each change undoes alone
    And it survives a save and a load
