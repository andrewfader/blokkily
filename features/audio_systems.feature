Feature: Choosing the audio system: ALSA, PulseAudio, JACK and PipeWire
  The AUDIO picker in SETTINGS opens the audio device on the system
  chosen - ALSA, PulseAudio, or JACK, which is also how PipeWire's JACK
  interface is reached - or AUTO, as it ships, which plays through PipeWire's
  JACK interface when a PipeWire or JACK server answers, else PulseAudio
  where it has devices, else where RtAudio chooses. The choice
  is remembered between launches; BLOKKILY_AUDIO_API overrides it. Under
  JACK and PipeWire the client is called Blokkily. A system this build does
  not have is refused, saying which.

  # audio_systems_listed
  Scenario: The systems this build can open
    Then each is named for the picker and can be asked for by name, RtAudio's silent dummy is not offered, and a Linux build offers ALSA

  # audio_systems_auto
  Scenario: AUTO prefers PipeWire
    Given AUTO is the audio system, as it ships
    Then it resolves to JACK (PipeWire's interface) when a server answers, else PulseAudio where it has devices, else RtAudio's own choice
    And an output made on AUTO is on that system, a system asked for by name is kept, and a gate's deterministic device resolves nothing
    And landed on JACK, the window reads the sample rate and block from the server, as for JACK chosen by hand

  # audio_systems_missing
  Scenario: A system the build lacks
    Then a device on it neither opens nor starts, and says which system is not available

  # audio_device_jack
  Scenario: Playing through JACK (or PipeWire's JACK)
    Given a JACK or PipeWire server is running (skipped where none is)
    Then the song plays through it: the callback is pulled and what reaches the server is not silent
    And it keeps time at whatever block size the server runs, even one other than it reported when the client opened, as PipeWire's JACK does when its graph runs at another quantum
    And stopping returns rather than waiting on a block that never comes

  # bdd_audio_system
  Scenario: The AUDIO picker
    Then it lists AUTO and every system the build can open
    And an unknown system is refused with a reason, a listed one is chosen and shown
    And a gate's deterministic device is never replaced by a real one
