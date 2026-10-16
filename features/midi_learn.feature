Feature: MIDI learn
  A hardware knob, fader or button - one controller (CC) on one channel -
  can be bound to a track's gain, pan, mute or solo (right-click a mixer
  strip: MIDI learn gain, pan, mute, solo) or to a macro (its card's LEARN
  chip). After LEARN the next controller moved is bound; Escape cancels.
  Moving a bound controller is an edit like moving the control on screen.
  Since a macro can aim at any plugin parameter, a knob reaches any
  parameter through one. Bindings are saved with the song and are one step
  of history each. The status line under the project panel says what
  learning waits for and what it bound.

  # midi_learn_feed
  Scenario: The MIDI input offers its controllers
    When CC 7 on channel 1 and CC 74 on channel 4 arrive with a pedal, an RPN, a mode message and a note
    Then the two controllers are offered to the control thread, and nothing else
    And a full feed drops what is newest rather than blocking the port

  # midi_learn_records
  Scenario: Bindings are saved with the song
    Then each binding is a midi_learn record, its channel counted from 1, and loads as saved
    And two bindings for one controller, or one to a track the song lacks, are refused

  # midi_learn_track_removal
  Scenario: Bindings follow their tracks
    When a track is deleted
    Then its bindings go, the later tracks' bindings move with them, and master's stays

  # bdd_midi_learn
  Scenario: Learn a fader, a mute and a macro
    Given the CLAP synth on track 0 and a MIDI input
    When the producer learns track 0's gain and turns CC 7 on channel 1 to 64
    Then the fader is at -26.7 dB and the note heard from the callback is 26.7 dB quieter
    And CC 7 on channel 2 changes nothing
    When a button is learned to the mute
    Then pressing it silences the track and releasing it brings it back
    When a macro's LEARN chip is clicked and CC 21 turned
    Then the chip shows CC 21 · CH 1 and the macro follows the knob
    And the bindings survive a save and a load, a learn undoes in one step, forgetting unbinds, and Escape cancels
