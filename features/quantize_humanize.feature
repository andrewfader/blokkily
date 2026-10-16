Feature: Quantize and humanize a part
  QUANT and HUMAN in the pattern header edit every step of the open part
  at once, as one step of history. QUANT moves each step onto the grid it
  shows (1/16, 1/8 or 1/4, chosen with a right click); Shift-click moves
  it halfway. HUMAN loosens each step's timing by up to 8 ticks and its
  velocity by up to 0.08, differently each time. A step keeps its place on
  the grid and carries its timing as micro-offset, as a recorded take does.

  # groove_quantize_full
  Scenario: Quantize moves played-off steps onto the grid
    Given a part with steps played 30 ticks late and 40 early
    When it is quantized to sixteenths
    Then every step sits on the grid
    And quantized to eighths, an odd sixteenth moves to the eighth after it

  # groove_quantize_strength
  Scenario: Quantize strength moves part of the way
    Given a step 40 ticks late
    When it is quantized at half strength
    Then it is 20 ticks late, and no strength moves nothing

  # groove_quantize_collisions
  Scenario: Quantize keeps what it cannot place and wraps the downbeat
    Given steps on 8 and 9, and one played just before the loop's end
    When they are quantized to beats
    Then step 9, whose beat is taken, stays and is counted as kept
    And the last one lands on the loop's downbeat

  # groove_humanize
  Scenario: Humanize loosens timing and velocity within its amounts
    Given sixteen even steps at velocity 0.5
    When the part is humanized by 8 ticks and 0.1
    Then each moves at most that far, on its own step, both louder and softer
    And the same seed does the same, another seed something else

  # groove_render
  Scenario: Quantized timing is what plays
    Given a CLAP note played 40 ticks late on beat 2 at 120 BPM
    Then it sounds at sample 26000
    When the part is quantized
    Then it sounds at sample 24000, on the beat

  # bdd_groove
  Scenario: QUANT and HUMAN in the window
    Given the part played a little late, in the real window
    When the producer Shift-clicks QUANT, then clicks it
    Then the steps go halfway, then onto the grid, and each click undoes alone
    And a right click changes QUANT 1/16 to QUANT 1/8
    And HUMAN loosens the part, and one undo puts it back
