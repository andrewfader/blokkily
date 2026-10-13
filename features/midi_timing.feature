Feature: A played note keeps its timing to the sample
  A keyboard's messages are stamped as they arrive on the MIDI port's thread.
  The render callback places each one in the next device block at the sample
  that matches where it arrived in the period before it. A note is therefore
  heard one block late, which is constant, instead of anywhere up to a block
  late, which would make the spacing between notes jitter. A take records each
  note at the sample it was heard, so playing a take back keeps the timing
  it was played with. Input from the on-screen surfaces carries no stamp and
  sounds at the start of the next block, as before.

  Executable: tests/midi_timing_tests.cpp (CTest midi_timing_<case>). The
  audio comes through the production render callback from the real CLAP
  fixture, which holds a steady level while a key is down. The engine's
  input clock is set to a clock the test advances, so where a note lands
  never depends on the wall clock.

  # midi_timing_placed
  Scenario: A note is heard at the sample it was played at, one block later
    Given 512-sample blocks at 48 kHz and a key going down half way through one
    When the next block is rendered
    Then the CLAP fixture is silent for its first 256 samples and sounding from sample 256
    And a key let go a quarter of the way through the block after stops it at sample 128

  # midi_timing_spacing, realtime_midi_timing
  Scenario: Notes played evenly are heard evenly, whatever the block size
    Given eight keys struck 1000 samples apart in time, across 512-sample blocks
    When the blocks are rendered
    Then each note starts 1000 samples after the one before it, to the sample

  # midi_timing_chunks
  Scenario: A note late in a long device block is placed in the chunk it falls in
    Given an engine that renders in chunks of 256 samples and a device block of 1024
    And a key going down 70% of the way through a period
    When the next device block is rendered
    Then the note starts on sample 716 of that block

  # midi_timing_recorded
  Scenario: A take records a note where it was heard
    Given recording armed on a running song and a key going down half way through a block
    Then the captured note is stamped 256 samples into the block it sounded in, at that sample's tick
    And written into the pattern and played back, it starts on that sample

  # midi_timing_unstamped
  Scenario: Unstamped input sounds at the start of the next block
    Given a key with no arrival time, as the on-screen keyboard sends
    When the next block is rendered
    Then it sounds from that block's first sample
