Feature: True staff notation as a fourth projection of the pattern
  Blokkily's editors - tracker, piano roll, step grid - are projections of one
  canonical pattern. Staff notation joins them: real staves with real clefs,
  key and time signatures, spelled note values, accidentals, rests, ties,
  chords, voices, tuplets and lyrics. The model scenarios below are proved by
  blokkily_notation_tests (tests/notation_tests.cpp), one case per scenario;
  the score view is features/score_view.feature.

  Scenario: Pitches convert between spelling and MIDI both ways
    Given middle C written as C4
    When it converts to MIDI and back
    Then the key is 60 and the spelling round-trips for all 128 keys
    And B#3 also converts to 60 while Cb4 converts to 59

  Scenario: Every clef names itself and places the staff's bottom line
    Given the eleven clefs from French violin to percussion
    When each names itself and reports its bottom line's diatonic position
    Then the names are the conventional English ones
    And treble sits on E4, French violin on G4, soprano on C4, mezzo-soprano on A3
    And alto on F3, tenor on D3, both baritones on B2, bass on G2, subbass on E2
    And only percussion reports itself unpitched

  Scenario: Key signatures alter the steps they claim, no others
    Given D major and Bb major
    When each is asked what accidental it imposes on every step
    Then D major sharpens F and C and nothing else
    And Bb major flats B and E and nothing else
    And 8 sharps or 8 flats are rejected
    And the major and minor tonics spell themselves correctly, even F# major and Eb major

  Scenario: Enharmonic spelling follows the key signature
    Given D major, F major, C major, C# major and Cb major
    When MIDI keys are spelled under each
    Then F#4 in D major is never written Gb, and Bb4 in F major is never A#4
    And C major breaks chromatic ties toward the sharps
    And C# major spells MIDI 60 as B#3 while Cb major spells it C4 and 61 as Db4

  Scenario: Note values convert to ticks and back; odd lengths tie
    Given 480 ticks per beat
    When exact durations convert
    Then a quarter is 480 ticks, a dotted half is 1440, a breve is 3840
    And 720 ticks is a single dotted quarter while 500 ticks is not exact
    And 2880 ticks ties a whole to a half
    And 500 ticks rounds to the 128th-note grid and ties quarter plus 128th

  Scenario: A spelled pitch lands on the right line or space of its clef
    Given treble and bass clefs
    When spelled pitches are positioned
    Then E4 is treble's bottom line and middle C hangs two steps below it
    And G2 is bass's bottom line
    And an accidental prints exactly when the key signature does not already say it
    And ledger lines count correctly above and below the staff
    And 4/4 is metered while chant is unmetered with no barlines

  Scenario: A pattern becomes notation where its notes fall
    Given a dotted-quarter D4, a C major triad and an eighth E4 in a bar of 4/4
    When it renders to a staff in D major under treble clef
    Then the bar reads the D4, the triad as one chord, the E4 and a closing quarter rest
    And replaying the staff puts every note back at its tick, key and length

  Scenario: Silence between notes is written as rests
    Given quarter notes on beats one and three
    When the pattern renders
    Then a quarter rest stands between them and after them

  Scenario: A note across the barline is tied over it
    Given a half note starting on beat four
    When the pattern renders
    Then a quarter ends bar one tied into a quarter that starts bar two
    And a triad tied into bar two is followed by the next note at its own time

  Scenario: A note off the notation grid across the barline comes back as one note
    Given a note five ticks before a barline at 480 per beat, 100 ticks long
    When the pattern renders and is played back
    Then one note comes back, starting and ending where it did

  Scenario: A note held under moving ones gets its own voice
    Given a whole-note C3 under four quarter notes
    When the pattern renders
    Then the held note and the tune are written as two voices of one bar

  Scenario: A meter sets the bar length
    Given four bars of 3/4
    When the pattern renders
    Then there are four bars, an empty bar is one dotted-half rest, and it plays back four bars long

  Scenario: Triplets read as triplets
    Given three notes of 160 ticks
    When the pattern renders
    Then they are three eighth-note triplets and play back exactly

  Scenario: A retune and a velocity ride along with the spelling
    Given F#4 a quarter tone flat at half velocity, in E-flat major
    When the pattern renders and plays back
    Then it is spelled Gb4 with its retune, and the key, retune and velocity come back

  Scenario: An unmetered staff is one bar as long as its notes
    Given a chant line with a rest in it
    When it renders without a meter
    Then it is a single measure and plays back note for note

  Scenario: Every object of a staff is placed where it sounds
    Given a one-beat pickup, a bar of a quarter, a chord and a half over a
      held whole note, a 3/4 bar under a percussion clef, and a bar back in treble
    When the staff is placed
    Then each object starts where its voice has got to, counting the pickup as one beat
    And the chord tone starts with its chord, and the 3/4 bars last three beats
    And no accidental prints under the percussion clef, and they print again in treble

  Scenario: An accidental holds to the end of its bar
    Given F#4, F4, F4, then F#4 tied over the barline and F4 in bar two, in C major
    When the staff is placed
    Then the first F4 prints its natural, the second does not, the tied F#4 prints nothing
    And bar two starts afresh, and a key change there decides what prints

  Scenario: A staff plays back with its transposition, pickup and changes
    Given a B-flat clarinet staff, a treble-8vb staff, and a tune with a pickup then a change to 3/4
    When each plays back
    Then the clarinet's D4 sounds C4 and the 8vb staff sounds an octave down
    And the pickup lasts one beat and 3/4 holds from the bar where it is written
    And a note written above MIDI 127 is left out and counted

  Scenario: A line of lyrics becomes one syllable per note, in any script
    Given "A-ma-zing grace" and a line of pointed Hebrew with a hyphen and a space
    When each is split into syllables
    Then the words split at spaces and hyphens, each keeping its hyphen, and the Hebrew keeps its points
    And joining the syllables gives the line back
    And a syllable rides on its note to the staff and back, on a chord's first note and a tie's first piece only
    And a Hebrew syllable and the score's tempo come back from MusicXML byte for byte

  Scenario: The song is written as a score, its whole arrangement
    Given a track whose verse plays twice from bar 1 and once more from bar 4, trimmed to its second half, in an alto clef and two sharps, the song turning to 3/4 at bar 4
    When the song is written as a score
    Then the part keeps the alto clef and the key, and changes to 3/4 where the song does
    And it plays every repeat, and the trimmed clip from its second half, lyrics and all

  Scenario: A score comes into the song as a section of its own
    Given a choir part on two staves in F major and 3/4 at 66 BPM, with lyrics
    When it comes into a song with no notes
    Then each staff becomes a track named after the part, the lower in the bass clef
    And the section takes the key and the meter and is two bars long, and its clips start the arrangement
    And the song takes the score's meter and tempo
    When it comes into a song that has notes
    Then it gets new tracks and its clips go after the last, and the song's tempo is left alone
    When it comes into a song holding only a recorded vocal
    Then the vocal's track is left as it was, and the score's clips go after the recording at the song's tempo
    When it comes into a song with no notes whose other tracks are an instrument's second output and a frozen track
    Then the score takes only the track with an instrument of its own, and adds a new one for its second staff
    And a long Hebrew title names the section cut between letters, never inside one
