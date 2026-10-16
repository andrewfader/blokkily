Feature: Blokkily narrates itself through a floating guide
  A GUIDE chip in the transport bar toggles the floating guide: Blokkily the
  mascot (a pixel-art T tetromino that is also a broccoli), watching what the
  producer does — and what an MCP agent does — and narrating it with why
  explanations in a speech bubble. It answers theory questions, teaches
  interactive lessons from JSON, and can change things itself through the same
  commands the window's buttons call, saying what it did. It never blocks the
  window: the producer can still play while it talks. LLM help is optional and
  off unless the producer configures it.

  # bdd_guide "guide: GUIDE toggles the mascot and its bubble"
  Scenario: GUIDE toggles the floating guide
    Given the window at its canonical size
    Then a GUIDE chip sits wholly inside the transport bar
    When the producer clicks GUIDE
    Then the mascot and the overlay are gone and GUIDE is unlit
    When the producer clicks GUIDE again
    Then the mascot is on screen at a usable size and GUIDE is lit

  # bdd_guide "guide: writing a note is narrated with a why"
  Scenario: The guide narrates what the producer does
    Given the guide is on and its bubble is dismissed
    When the producer writes C4 on step 0
    Then a speech bubble appears naming the note C4
    And the bubble explains why it will sound — the step, the pattern, the instrument

  # bdd_guide "guide: the guide answers a theory question without an LLM"
  Scenario: The guide answers questions from its own knowledge
    Given the guide is on with no guide-specific LLM configured
    When the producer asks "what is a triad"
    Then the bubble answers with the rule-based explanation of a triad
    And the answer is labelled as the guide's, not the LLM's

  # bdd_guide "guide: TEACH ME starts a lesson with a spotlight and checks"
  Scenario: The guide teaches an interactive lesson
    Given the guide is on
    When the producer starts the "first beat" lesson
    Then the lesson's first step is shown and a ring is drawn round the step grid
    When the producer advances the lesson
    Then the next step's instruction appears and the ring follows its target
    When the producer writes the note the step asks for
    Then the lesson marks the step done

  # bdd_guide "guide: the guide's own edits are undoable and announced"
  Scenario: The guide changes things and says what it did
    Given the guide is on and its bubble is dismissed
    When the guide adds a chord on step 4
    Then the chord's notes are in the pattern as one undo step
    And the bubble says what the guide did, in its own voice
    And the narration feed marks the edit as the guide's, not the producer's

  # bdd_guide "guide: a destructive guide action asks first"
  Scenario: The guide confirms before a destructive action
    Given the guide is on
    When the guide is asked to clear the pattern
    Then a confirmation appears instead of the edit happening
    When the producer cancels
    Then the pattern is unchanged and the confirmation is gone

  # bdd_guide "guide: the window stays playable under the guide"
  Scenario: The guide does not block the window
    Given the guide is on and narrating
    When the producer clicks the play button
    Then the song plays and the guide keeps narrating

  # bdd_guide "guide: screenshot"
  Scenario: The guide, narrating, on screen
    Given the guide is on
    When the producer writes C4 on step 0
    Then the window is captured with the mascot, the bubble and the ring
