Feature: LV2 plugins
  LV2 instruments and effects are hosted through JUCE's LV2 format by the
  same adapter that hosts VST3, scanned out of process like CLAP and VST3
  from $LV2_PATH, ~/.lv2, /usr/local/lib/lv2 and /usr/lib/lv2, listed in the
  browser with a violet LV2 badge, and saved with the song by bundle and URI.
  The suite builds a real .lv2 bundle with JUCE as its fixture: it sounds
  Level x velocity while a note is held, so what the host sent can be read
  off the audio.

  # lv2_scan
  Scenario: The scan finds an LV2 bundle
    Then the fixture's bundle holds one instrument, named, with its URI
    And its folder's .lv2 bundle is a scan candidate, and the helper's record names it LV2

  # lv2_process
  Scenario: Notes are played on their samples
    Given the fixture made through JUCE's LV2 host
    When a note is struck at sample 100 and let go at 300
    Then it is silent before 100, sounds 0.25 from 100 to 299, and is silent from 300

  # lv2_parameters_and_state
  Scenario: Parameters and state
    Then its Level parameter is listed, a value of 0.75 sent to it is heard
    And its saved state, loaded into another instance, is heard the same

  # lv2_factory_and_engine
  Scenario: A song of an LV2 instrument
    Given a note on beat two of a track playing the fixture, made by the production factory
    Then the bounce is silent to sample 23999 and sounds 0.25 from 24000

  # bdd_lv2
  Scenario: LV2 in the window
    When the fixture's folder is scanned
    Then the browser lists it with an LV2 badge and the status counts 1 LV2
    When it is chosen for track 0
    Then the track plays it, heard through the production callback
    And it survives a save and a load, played by an LV2 processor
