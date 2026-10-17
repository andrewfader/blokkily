Feature: Blokkily narrates itself through a floating guide
  A GUIDE chip beside HELP opens the floating guide: Blokkily the mascot (a
  pixel-art T tetromino that is also a broccoli), watching what the producer
  does - and what an MCP agent does - and narrating it with why explanations
  in a speech bubble. It answers theory questions, teaches interactive lessons
  from JSON, and can change things itself through the same commands the
  window's buttons call, saying what it did. It is off until the producer
  turns it on, and draws nothing that takes a click until it talks: the
  producer can still play and mix while it does. Asking a model is off until
  the producer allows it, because the question goes to the prompt bar's
  model, which may be a cloud service.

  Proved by the bdd_guide gate (src/app/verify/scenario_guide.cpp) and by
  blokkily_guide_tests (tests/guide_tests.cpp) for the bus, the narrator, the
  dispatcher, the lesson engine and the LLM client. A verify run keeps the
  guide's settings in memory: no gate reads or changes the user's.

  # bdd_guide "guide: GUIDE toggles the mascot and its bubble"
  Scenario: GUIDE opens the floating guide, and its bubble turns it off
    Given the window at its canonical size, with the guide off
    Then the GUIDE chip is on screen at a usable size, unlit, and nothing of the guide is drawn
    When the producer clicks GUIDE
    Then GUIDE is lit, the bubble opens, and the mascot is beside it at a usable size
    When the producer clicks GUIDE again
    Then the bubble closes and nothing of the guide takes a click
    When the producer opens it and clicks "Turn the guide off"
    Then the overlay is gone and GUIDE is unlit

  # bdd_guide "guide: writing a note is narrated with a why"
  Scenario: The guide narrates what the producer does
    Given the guide is on and its bubble is dismissed
    When the producer writes C4 on step 0
    Then a speech bubble says the producer added C4 at step 1, numbered as the grid numbers it, and why to listen to it loop
    And the narration feed names the producer as who did it

  # bdd_guide "guide: the guide answers a theory question without an LLM"
  Scenario: The guide answers questions from its own knowledge
    Given the guide is on, with asking a model off
    When the producer asks "what is a triad"
    Then the bubble answers with the rule-based explanation of a triad, as the guide's
    When the producer asks a question its rules do not know
    Then nothing is asked of any model, and the bubble says to switch on ASK MODEL

  # bdd_guide "guide: TEACH ME starts a lesson with a spotlight and checks"
  Scenario: The guide teaches an interactive lesson
    Given the guide is on and the producer's C4 is on step 1
    When the producer clicks TEACH ME and picks "Your first beat"
    Then the lesson's first step is shown and a ring is drawn round the step grid
    When the producer goes on to KICK
    Then the guide leaves the producer's C4 where it is and says so
    And going back and forward again does not do the step's action twice
    When the producer writes one of the three kicks FOUR ON THE FLOOR asks for
    Then the step counts "1 of 3" and its instruction stays on screen
    When the producer writes the other two, then the four hats
    Then the lesson moves on by itself to PLAY, and the ring follows to the play button

  # bdd_guide "guide: the guide's own edits are undoable and announced"
  Scenario: The guide changes things and says what it did
    Given the guide is on and its bubble is dismissed
    When the guide adds a chord on step 2
    Then the chord is in the pattern, and the bubble says what the guide did
    And the narration feed marks the edit as the guide's
    And one UNDO takes it back, and the producer's next edit is theirs again

  # bdd_guide "guide: a destructive guide action asks first"
  Scenario: The guide confirms before a destructive action
    Given the guide is on and the pattern has notes
    When the guide is asked to clear the pattern
    Then a confirmation appears instead of the edit happening
    When the producer cancels
    Then the pattern is unchanged
    When the producer asks again and confirms
    Then the pattern is empty, and one UNDO puts every note back

  # bdd_guide "guide: the window stays playable under the guide"
  Scenario: The guide does not block the window
    Given the guide is on and talking
    When the producer clicks the play button, and then the master strip's FX
    Then the song plays and stops, the guide keeps talking, and the master rack opens

  # bdd_guide "guide: screenshot"
  Scenario: The guide, talking, on screen
    Given the guide is on
    When the producer writes G4 on step 6
    Then the window is captured with the mascot, the bubble and the ring
