Feature: Track freeze
  FRZ on a mixer strip renders the track's instrument and inserts - what
  reaches its fader - to audio through the engine the speakers hear, and
  plays that render in their place, so they are not loaded. The fader, pan,
  sends, mute and solo still work on it. The track's own audio clips are in
  the render and are not heard twice. FRZ again, or one undo, unfreezes it:
  the instrument and inserts come back holding what they held. A frozen
  track is saved frozen. Its render runs to the end of the track's tail, so
  the song lasts until that tail ends.

  # freeze_sounds_the_same
  Scenario: Frozen, the song sounds the same without the track's processors
    Given a CLAP synth through a latent CLAP effect, faded and panned, beside another track
    When the track is frozen
    Then its instrument and insert are not loaded, and the other track's is
    And the bounce is the live bounce sample for sample, then silence
    And a fader moved after the freeze sounds as it would have unfrozen

  # freeze_other_clips
  Scenario: The track's own clips play once
    Given the track also plays an audio clip
    Then frozen, the clip is heard once, inside the render

  # freeze_records
  Scenario: A frozen track is saved
    Then frozen and freeze_clip records save and load it, and a frozen track without its render is refused

  # bdd_freeze
  Scenario: FRZ in the window
    Given the CLAP synth on track 0, in the real window
    When the producer clicks FRZ on its strip
    Then the instrument is unloaded and the export is unchanged
    And one undo thaws it, a save and load keep it frozen, and FRZ again unfreezes it unchanged
