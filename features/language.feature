Feature: The window in another language
  LANGUAGE in SETTINGS shows the window in ENGLISH, in DEUTSCH, or in the
  desktop's own language (SYSTEM), as it ships, in English where there is no
  translation. Every label the window draws is a qsTr; the translations are
  compiled into the program from i18n/blokkily_<locale>.ts. A label must read
  whole in every language: a translation that would be cut short where the
  English reads in full, or push a control off the window, is a defect.
  Song content (pattern, track and plugin names) and the engine's status
  messages are not translated.

  Proved by the bdd_language gate (`blokkily --verify --scenario language`,
  src/app/verify/scenario_language.cpp), which saves
  build/artifacts/language-de.png, language-settings-de.png and language.png.

  # bdd_language
  Scenario: DEUTSCH redraws the window in German
    Given the real window in English, with ENGLISH lit under LANGUAGE in SETTINGS
    When DEUTSCH is pressed
    Then SETTINGS and the editors show their labels in German, RESET reads ZURÜCKSETZEN and HELP reads HILFE
    And no label that read whole in English is cut short or pushed past the window's edge

  # bdd_language
  Scenario: The language is a choice, not layout
    Given the window in German
    When RESET is pressed under LAYOUT
    Then the window is still in German, and the choice is remembered

  # bdd_language
  Scenario: ENGLISH puts every label back
    When ENGLISH is pressed
    Then every label in the window reads as it did before
