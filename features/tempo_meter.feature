Feature: Tempo and meter are edited where the song is arranged
  The ruler offers each bar a meter and draws every bar as wide as it lasts.
  A tempo lane under the ruler holds the song's tempo points, each one a
  handle, with a switch after it between a step and a ramp. The transport
  reads out the meter and the tempo where the playhead is. A pattern need not
  be sixteen steps: one made in a 7/8 bar has fourteen, its LEN can be changed,
  and the grid, the tracker and the roll all draw as many columns as it has.
  (Plan item 2.1, the rest of feature 6. The maps themselves are in
  features/timebase.feature.)

  # bdd_tempo_meter "tempo-meter: the ruler's meter menu makes bar 2 7/8"
  Scenario: The ruler's meter menu changes a bar's meter
    Given a 4/4 song with the playhead in bar 1
    When bar 2 of the ruler is right-clicked and 7/8 is chosen from its menu
    Then bar 2 onwards is in 7/8, the ruler labels bar 2 "7/8"
    And the transport still reads "4 / 4" in bar 1

  # bdd_tempo_meter "tempo-meter: a 7/8 bar is drawn 7/8 as wide, and the tempo lane is usable"
  Scenario: Bars are drawn as wide as they last
    Given a song whose bar 2 is in 7/8
    Then ruler bar 2 is 7/8 as wide as ruler bar 1, and every bar is at least 12 px wide
    And the clip cells under the ruler line up with it, bar for bar
    And the tempo lane is at least 400 by 30 px and starts where the bars start

  # bdd_tempo_meter "tempo-meter: a click on bar 3 seeks to tick 3600, sample 180000"
  Scenario: Seeking to a bar after a 7/8 bar
    Given a 120 BPM song whose bar 2 onwards is in 7/8
    When ruler bar 3 is clicked
    Then the engine is at sample 180000, the sample tick 3600 falls on
    And the transport reads "3.1.1" and "7 / 8"

  # bdd_tempo_meter "tempo-meter: a pattern made in a 7/8 bar has 14 steps in the grid, tracker and roll"
  # tempo_meter_pattern_length_saved
  Scenario: A pattern made in a 7/8 bar has fourteen steps in every editor
    Given the playhead in a 7/8 bar
    When +PAT is clicked
    Then the new pattern is 1680 ticks, fourteen steps, and LEN reads "14 ST"
    And the step grid shows 14 cells, the tracker 14 rows and the roll 14 columns
    And a click in the roll's last column writes step 14, drawn in all three editors
    And the roll's last column lies inside its panel, where it can be clicked
    And the pattern saves and loads at its length

  # bdd_tempo_meter "tempo-meter: the downbeat of bar 3 sounds at sample_at(3600), each step of a 7/8 bar in place"
  # tempo_meter_fourteen_steps_in_seven_eight
  Scenario: A fourteen-step pattern plays with the 7/8 bars
    Given the fourteen-step pattern, with notes on its first and last steps, placed on bars 2 and 3
    When the song plays from bar 2 on the CLAP fixture through the production callback
    Then notes start at samples 96000, 174000, 180000 and 258000
    And the downbeat of bar 3 sounds at sample 180000, where the tempo map puts tick 3600

  # bdd_tempo_meter "tempo-meter: a Ctrl-click on the grid seeks to that step of the 7/8 pass"
  Scenario: A Ctrl-click on the step grid seeks in any meter
    Given the playhead at the start of a 7/8 bar that plays the open fourteen-step pattern
    When step 6 of the grid is Ctrl-clicked
    Then the playhead and the engine are at tick 4200, step 6 of that pass
    And not at tick 4440, where counting sixteen steps a bar would put it

  # bdd_tempo_meter "tempo-meter: the LEN spinner sets the pattern's length and undo brings its steps back"
  # tempo_meter_pattern_with_length
  Scenario: The LEN spinner changes a pattern's length
    Given a fourteen-step pattern with a note on its last step
    When LEN is made two steps shorter with its minus button
    Then the pattern has twelve steps, the grid twelve cells and the tracker twelve rows
    And the note on the last step is gone
    When the edits are undone
    Then the pattern has fourteen steps again, with its last note

  # bdd_tempo_meter "tempo-meter: a double-click on the tempo lane adds a point on the beat"
  Scenario: A double-click on the tempo lane adds a tempo point
    Given a song with one tempo, 120 BPM
    When the tempo lane is double-clicked just inside bar 3
    Then a tempo point is added on the downbeat of bar 3, at the tempo sounding there

  # bdd_tempo_meter "tempo-meter: dragging a tempo point's handle sets its tempo in one step of history"
  Scenario: Dragging a tempo point's handle sets its tempo
    Given a tempo point at 120 BPM on bar 3
    When its handle is dragged 60 px up
    Then the point is at 150 BPM and the lane labels it "150"
    And one undo takes the whole drag back, and redo brings it again

  # bdd_tempo_meter "tempo-meter: the ramp and the new tempo are heard where the tempo map puts them"
  Scenario: The ramp switch glides the tempo into the next point
    Given 120 BPM from the start and a point at 150 BPM on bar 3
    When the ramp switch after the first point is clicked
    Then the tempo ramps from 120 to 150 BPM up to bar 3 and the transport reads 150.00 BPM there
    And played from bar 2 on the CLAP fixture, every note starts within a sample of where integrating the ramp puts it

  # bdd_tempo_meter "tempo-meter: a take played in the 7/8 bar lands on the step it was played at"
  Scenario: A take in a 7/8 bar lands on its step
    Given recording armed and the playhead Ctrl-clicked onto step 7 of the 7/8 pass
    When a MIDI key is played there
    Then it is written on step 7 of the fourteen-step pattern and drawn in the grid and the roll
    And nothing is written into the 4/4 pattern

  # bdd_tempo_meter "tempo-meter: save, load and undo keep the tempo and meter maps"
  Scenario: Tempo points and meters survive save, load and undo
    Given a song with a tempo ramp, a 7/8 meter and a fourteen-step pattern
    When it is saved, a meter is changed, and it is loaded again
    Then the tempo map, the meter map and the pattern's length are as they were saved
    And a meter chosen afterwards from the ruler is one step of history
