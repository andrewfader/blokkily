Feature: Effects and audio files in one session
  Scenario: Track, return and master plugin gestures are undoable (bdd_cross_feature)
    Given real CLAP effects on a track, return and master
    When I open each insert's editor and turn its gain
    Then one undo restores its previous state and redo restores the gesture
    And the running instance remains the same

  Scenario: An insert parameter becomes an editable lane (bdd_cross_feature)
    Given an insert with a Gain parameter
    When I select its parameter for automation and draw a zero value
    Then the rendered song is silent
    And raising the lane makes the song audible

  Scenario: Collect audio is portable and undoable (bdd_cross_feature)
    Given a saved song whose clip and sampler reference the same external file
    When I collect audio
    Then one copy lives in the project's audio folder and both references point to it
    And undo restores the original references without deleting the copy or original
    And saving and reopening preserves the collected references

  Scenario: An editor follows its effect after an earlier insert is removed (bdd_cross_feature)
    Given the second insert has an open native editor
    When I remove the first insert
    Then the second processor is adopted into slot one
    And the same window remains open at its new address
