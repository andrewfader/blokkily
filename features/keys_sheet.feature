Feature: Every key the window answers is listed in it
  The keys are many, and a hint line cut short at the editors' top cannot
  hold them. Ctrl+/ or the ? beside HELP opens a sheet over the window that
  lists them by what they act on - transport, steps and tracker, file, window
  - each with what it does. Escape, a click outside it, or the same key again
  closes it, and the editors have the keyboard back.

  # bdd_window_layout
  Scenario: Ctrl+/ lists the keys
    When Ctrl+/ is pressed
    Then a sheet of the window's keys is open at a readable size, naming Ctrl+M and what it does
    When Escape is pressed
    Then it is closed
    When the ? beside HELP is clicked
    Then it opens again, and the ? is lit
