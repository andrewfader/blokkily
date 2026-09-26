Feature: A complete song survives recording, saving and export
  Scenario: The complete workstation (bdd_session_everything)
    Given a song with a tempo ramp, a 7/8 bar, keyed and sliced samplers, CLAP, VST3 and SoundFont instruments
    And a warped audio clip, built-in and plugin inserts, a return and a send
    And gain automation and effect-parameter automation
    When I record MIDI on two armed tracks and audio input after a metronome count-in while resampling the master
    Then one undo removes the complete take and redo restores it
    When I save, start a new session, reopen and export the song
    Then the read-back file equals the reopened engine's render sample for sample
    And muting each track changes the rendered mix
    And the real QML panels remain usable in the inspected screenshot
