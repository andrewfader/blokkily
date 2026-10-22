Feature: The SETTINGS view: the audio device, its rate and its block
  SETTINGS, the gear beside HELP above the editors, holds what belongs to the
  machine rather than to the song: the audio system (ALSA, PulseAudio, JACK
  and PipeWire's JACK on Linux; WASAPI, DirectSound and ASIO on Windows; Core
  Audio on a Mac), the output and input devices on it, the sample rate and
  buffer size the device is asked for, the MIDI clock, and how the window lays
  itself out. Each audio change reopens the device at once. It is remembered
  between launches and never saved with a song; a gate keeps it in memory.

  # audio_systems_devices
  Scenario: The devices a system offers
    Then each device listed is named, plays or records, and offers only rates a device runs at
    And a system the build lacks offers none

  # bdd_settings
  Scenario: The SETTINGS view
    Given the SETTINGS gear is pressed above the editors
    Then the system, output, input, sample rate and buffer pickers are laid out at a usable size, each listing its choices
    And the editors give the view their room, while the rail and the mixer stay
    And the MIDI clock and sync and the window's auto-hide and docking switches are there
    And a second press goes back to the view it was opened from

  # bdd_settings
  Scenario: AUTOSAVE says how often unsaved work is copied aside
    Given the window copies unsaved work aside every 30 seconds, as it ships
    When 10 S is picked under PROJECT · AUTOSAVE
    Then it is copied every 10 seconds from then on
    When OFF is picked
    Then it is not copied at all

  # bdd_settings
  Scenario: Another rate and block reach the device
    When 44.1 kHz and a 256-frame buffer are picked from their menus
    Then the device is opened again at them, the song is prepared at 44.1 kHz, and RUNNING says so
    And a note played through the production callback is not silent
    And a rate or a block that is not offered is refused with a reason, leaving the device as it was
    And a device that is not there is refused, and DEFAULT is always offered
    And the user's settings file is not written
