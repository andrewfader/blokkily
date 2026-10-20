Feature: Blokkily shows a newcomer around
  A HELP button at the end of the keyboard hints, above the editors, starts
  a guided tour. Blokkily, the
  mascot (a pixel-art T tetromino that is also a broccoli), hops from stop to
  stop with a speech bubble; a ring flies to the control being talked about
  and a keycap tooltip names the key that drives it. F1 starts it too. The
  tour leaves on CANCEL, on the bubble's close button, on Escape or X, or
  when HELP is pressed again. Finished, or skipped with ASK or a second F1,
  it hands over to the floating guide with the keyboard in its question box.
  It never blocks the window: the producer can still play while it talks.

  # bdd_tutorial "tutorial: HELP sits beside the keyboard hints, whole and on screen"
  # bdd_tutorial "tutorial: HELP brings Blokkily in with a bubble"
  Scenario: HELP starts the tour
    Given the window at its canonical size
    Then a HELP button sits wholly inside the window, after the keyboard hints
    When the producer clicks HELP
    Then Blokkily and a speech bubble are on screen, each at a usable size
    And the bubble says "HI, I'M BLOKKILY" and HELP is lit

  # bdd_tutorial "tutorial: NEXT flies the bubble and the ring to the transport"
  # bdd_tutorial "tutorial: every stop rings its control and keeps the bubble on screen"
  Scenario: The tour visits each part of the window
    Given the tour at its first stop
    When the producer clicks NEXT
    Then the bubble moves, and a ring is drawn round the play button
    And the bubble does not cover the play button
    When they keep clicking NEXT
    Then the ring goes round the tempo, the views, the song, the loop, the step grid,
      the instrument, the mixer, the assistant and HELP, in that order
    And at every stop the bubble and Blokkily lie wholly inside the window
    And the last stop's button reads DONE

  # bdd_tutorial "tutorial: the window stays playable under the tour"
  Scenario: The tour does not block the window
    Given the tour pointing at the play button
    When the producer clicks the play button
    Then the song plays and the tour is still running

  # bdd_tutorial "tutorial: the arrow keys turn its pages, not the editor's"
  Scenario: The keyboard turns the tour's pages
    Given the tour has the keyboard
    When the producer presses Right and then Left
    Then the tour goes forward a stop and back again
    And the step cursor in the editors has not moved

  # bdd_tutorial "tutorial: DONE goes on to the guide's question box"
  # bdd_tutorial "tutorial: ASK skips the rest of the tour to the question box"
  # bdd_tutorial "tutorial: F1 starts the tour, and again skips to the question box"
  Scenario Outline: The tour ends in asking
    Given the guide is off and the tour is running
    When the producer <moves on>
    Then Blokkily, the bubble and the ring are gone and HELP is not lit
    And the guide is on with its bubble open
    And the keyboard is in its question box, which is wholly on screen at a usable size
    And a key typed there is a question, and no note was written

    Examples:
      | moves on                         |
      | clicks DONE at the last stop     |
      | clicks ASK on a later stop       |
      | presses F1 to start, then again  |

  # bdd_tutorial "tutorial: Escape ends it"
  # bdd_tutorial "tutorial: X ends it, and is not typed as a note"
  # bdd_tutorial "tutorial: the bubble's close button ends it"
  # bdd_tutorial "tutorial: CANCEL ends it"
  # bdd_tutorial "tutorial: HELP again ends it"
  Scenario Outline: Every way out of the tour
    Given the tour is running
    When the producer <leaves>
    Then Blokkily, the bubble and the ring are gone and HELP is not lit
    And the guide stays off
    And the keyboard is back with note entry, and no note was written

    Examples:
      | leaves                        |
      | presses Escape                |
      | presses X                     |
      | clicks the bubble's close box |
      | clicks CANCEL                 |
      | clicks HELP again             |
