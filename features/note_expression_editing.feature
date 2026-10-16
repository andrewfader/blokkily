Feature: Editing per-note expression and poly pressure
  The piano roll's controller lane draws the selected note's own
  expression - NOTE PITCH (-12..+12 semitones), NOTE TIMBRE and NOTE PRESS -
  across its length, and POLY draws one key's poly pressure across the
  pattern (the selected note's key, or any key with the arrows). Points are
  drawn by dragging, moved by dragging a point, and erased with a
  right-drag, as on the other lanes. A note's expression goes where the note
  goes; each stroke is a step of history. Instruments hear it as per-note
  expression (CLAP note expressions, MPE channels for SoundFont and VST3).

  # bdd_note_expression
  Scenario: Pressure, timbre and poly pressure drawn in the lane, heard in the export
    Given a CLAP note held for a bar and selected, in the real window
    When NOTE PRESS is drawn at full across its second beat
    Then the export there is twice as loud as the plain note and unchanged before it
    And one undo removes the stroke, and erasing it does too
    When NOTE TIMBRE is drawn at half
    Then the export there is half again as loud
    When the lane shows POLY
    Then it is on the note's key, C4
    And pressure drawn on it there raises the export by its value
