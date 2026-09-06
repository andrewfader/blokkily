Feature: Discover installed plugins without risking the interface
  Describing a plugin means running someone else's code. A plugin that hangs,
  crashes, or refuses to load must cost that plugin's place in the browser and
  nothing else.

  Scenario: The window opens while the installed plugins are still being scanned
    Given an installation directory holding many plugins
    When the producer launches Blokkily
    Then the window appears without waiting for the scan
    And the browser fills with instruments as they are described
    And edits to the pattern are answered while the scan is still running

  Scenario: A plugin that never returns cannot freeze the interface
    Given an installation directory holding a working plugin and one whose
      entry point never returns
    When Blokkily scans that directory
    Then the working plugin is listed in the browser
    And the plugin that never returns is reported as a failure
    And the interface keeps answering throughout

  Scenario: A plugin that failed to scan is skipped on the next launch
    Given a scan in which one plugin had to be given up on
    When Blokkily scans the same directory again
    Then the failed plugin is not loaded a second time
    And the second scan finishes without waiting out another deadline

  Scenario: Asking for a rescan tries everything again
    Given a plugin that failed the last scan
    When the producer presses RESCAN PLUGINS
    Then that plugin is scanned again rather than skipped

  Scenario: An instrument is found by typing a few letters of it
    Given a browser listing every installed plugin and SoundFont
    When the producer types a few letters of an instrument's name
    Then only the instruments those letters reach are listed
    And the closest match is offered first
    And the arrow keys move through them and Return loads one
    And clearing the field lists the whole installation again

  Scenario: A list longer than the panel is still browsable
    Given more instruments than the browser can show at once
    When the producer scrolls the browser
    Then a scrollbar says where in the list they are
