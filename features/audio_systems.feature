Feature: Choosing the audio system: ALSA, PulseAudio, JACK and PipeWire
  The AUDIO picker in the device rail opens the audio device on the system
  chosen - ALSA, PulseAudio, or JACK, which is also how PipeWire's JACK
  interface is reached - or leaves the choice to RtAudio (AUTO). The choice
  is remembered between launches; BLOKKILY_AUDIO_API overrides it. Under
  JACK and PipeWire the client is called Blokkily. A system this build does
  not have is refused, saying which.

  # audio_systems_listed
  Scenario: The systems this build can open
    Then each is named for the picker and can be asked for by name, RtAudio's silent dummy is not offered, and a Linux build offers ALSA

  # audio_systems_missing
  Scenario: A system the build lacks
    Then a device on it neither opens nor starts, and says which system is not available

  # audio_device_jack
  Scenario: Playing through JACK (or PipeWire's JACK)
    Given a JACK or PipeWire server is running (skipped where none is)
    Then the song plays through it: the callback is pulled and what reaches the server is not silent

  # bdd_audio_system
  Scenario: The AUDIO picker
    Then it lists AUTO and every system the build can open
    And an unknown system is refused with a reason, a listed one is chosen and shown
    And a gate's deterministic device is never replaced by a real one
