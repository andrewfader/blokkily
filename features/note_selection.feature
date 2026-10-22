Feature: Several notes are chosen in the piano roll and edited together
  Shift-click adds a note to the notes chosen, or takes it away; Shift-drag
  from an empty place draws a band that chooses the notes inside it; Ctrl+A
  chooses every note of the part. Chosen notes are outlined. Dragging one of
  them moves them all, by steps and by keys; Delete removes them; Shift+Up and
  Shift+Down (Ctrl for octaves) transpose them; Ctrl+C copies them and Ctrl+V
  pastes them with the earliest on the cursor's step, the pasted notes chosen.
  Each is one step of history. Moving the cursor to a step none of them is
  on lets them go, so a key meant for that step never edits them.

  # bdd_note_selection
  Scenario: Shift-clicks choose notes
    Given four notes in the roll
    When two of them are Shift-clicked
    Then those two are chosen and no note has changed

  # bdd_note_selection
  Scenario: Dragging a chosen note moves them all
    When one of the two is dragged a step right and two keys up
    Then both have moved a step right and two keys up, and are still chosen
    And one undo puts them back

  # bdd_note_selection
  Scenario: A band chooses, Delete removes
    When a band is Shift-dragged across the first three notes
    Then those three are chosen and the fourth is not
    When Delete is pressed
    Then only the fourth note is left, and undo brings the others back

  # bdd_note_selection
  Scenario: Every note is copied, pasted and transposed
    When Ctrl+A, Ctrl+C, the cursor on step 3 and Ctrl+V
    Then a copy of the four notes starts on step 3, chosen
    When Shift+Up is pressed
    Then the pasted notes are a semitone higher and the first four are not

  # bdd_note_selection
  Scenario: Moving the cursor elsewhere lets the notes go
    Given every note chosen
    When the cursor is put on a step none of them is on
    Then no note is chosen
