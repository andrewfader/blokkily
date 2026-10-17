# Staff notation design (feature/staff-notation)

Goal: true music-theory notation in Blokkily — staves, clefs (including the
obscure ones), key/time signatures, note values, accidentals, lyrics — so a
user (or an agent) can write concertos, hymns, and choral music, and move
scores in and out through MusicXML.

## Non-goals for v1

- Engraving-beautiful output (spacing is readable, not Gould-perfect).
- Cantillation marks (te'amim) for Jewish liturgical chant. Unmetered chant
  staves and lyric underlay are in scope; te'amim are a separate system.
- Handwritten-style fonts; playback follows the existing engine.

## Module layout

New namespace `blokkily::notation`, headers under
`include/blokkily/notation/`, implementation under `src/notation/`:

| file | contents |
|---|---|
| `pitch.hpp` | `Step` (C..B), `Accidental` (-2..+2), `SpelledPitch{step, accidental, octave}`, MIDI conversion both ways |
| `clef.hpp` | `Clef` — see clef table below; `diatonic_offset()` of its reference line |
| `signature.hpp` | `KeySignature{fifths:-7..7}`, `TimeSignature{beats, beat_unit}` plus `unmetered()` |
| `duration.hpp` | `NoteValue` (breve..128th), dots, tuplets (3 in 2, ...), `ticks()`/`from_ticks()`/`notate_ticks()` against a ticks-per-beat |
| `note.hpp` | `NotationNote{spelled, duration, tie_start/tie_end, lyric, voice, chord, cents, velocity}`, `Rest{duration, voice}` |
| `staff.hpp` | `Staff{clef, key, time, transpose_semitones, transpose_octaves}`, `Measure{objects, clef?, key?, time?, pickup}`, `Part{name, staves}`, `Score{parts, title, composer}` |
| `spelling.hpp` | `spell(midi_key, KeySignature) -> SpelledPitch` — enharmonic spelling |
| `layout.hpp` | `staff_position(spelled, clef) -> int` (diatonic steps above/below the clef's bottom line), accidental visibility vs key signature |
| `pattern_bridge.hpp` | `to_staff(pattern, key, clef, time)`, `to_pattern(staff, ticks_per_beat, &dropped)`, `place(staff)` (where each object falls, and which accidentals print), `measure_ticks()` — round-trips through the canonical pattern model; the pattern stays the single source of truth (repo invariant: editors are projections of one canonical pattern) |
| `musicxml.hpp` | `write(Score) -> std::string`, `parse(std::string) -> Score` |

## Clef table (all required)

G clefs: French violin (G on line 1), treble (G on line 2).
F clefs: subbass (F on line 5... by the "line the clef centers on" convention:
F5 subbass, F4 bass, F3 baritone).
C clefs: soprano C1, mezzo-soprano C2, alto C3, tenor C4, baritone C5.
Plus: percussion clef (unpitched), and octave-transposing variants
(treble-8vb, bass-8va) as a `transpose_octaves` field on Staff, not new clefs.
`Clef::name()` returns the conventional English name for UI/menus.

## Pitch spelling

`spell()` picks the diatonic spelling consistent with the key signature,
preferring the fewest accidentals; ties break toward sharps in sharp keys and
flats in flat keys. Microtonal `cents` on a pattern note is preserved on the
`NotationNote` as a `cents` field but does not affect spelling (documented).

## Time: measures, voices, chords, rests

A measure holds its objects voice by voice, as MusicXML does: inside a voice
they follow each other in time, a note with `chord` set starts with the note
before it, and every voice runs from the measure's start. `to_staff()` places
the pattern's notes where they start: notes that start and end together are
one chord, a note that overlaps a held one goes to another voice, gaps are
rests, notes are split at barlines and tied over them, and the first voice
fills every bar. `notate_ticks()` writes an exact value with dots, a triplet
as a triplet, and anything else as a tied chain rounded to the 128th grid.
`to_pattern()` plays each voice of each measure from the measure's start,
joins ties, honours a measure's own meter and pickups, applies the staff's
transposition, and drops (and counts) notes outside MIDI 0-127.

Accidentals print by the bar's rule (`place()`): a note prints one when its
own differs from what its bar, or else the key, last said for its line and
octave; a tied end never prints one.

## MusicXML scope

- Export: `<score-partwise>` 4.0, part list, measures with `<attributes>`
  (divisions, keys, times or senza-misura, staves, clefs, transpose, in the
  schema's order) in the first measure and wherever a measure changes them,
  pickups as implicit measures, notes and rests voice by voice joined by
  `<backup>`, `<chord/>`, ties, dots, tuplets (`<time-modification>`), stemless
  chant heads, lyrics (syllabic begin/middle/end/single and extend lines),
  velocity as the note's `dynamics`, a retune as a fractional `<alter>`.
- Import: the same back. A note's time comes from its `<duration>`; a
  `<forward>` gap becomes a rest; a `<key>` or `<time>` with no number holds on
  every staff; `<transpose>` (chromatic and octave) and `<clef-octave-change>`
  set the sounding pitch. Grace and cue notes are skipped; unknown elements
  are ignored. A staff number outside 1-16, a non-positive divisions, an
  unsupported meter or a negative duration is refused with
  `std::runtime_error`, never a crash or a runaway allocation.
- Tests (`tests/musicxml_tests.cpp`): a varied pattern written to a file,
  read back from the file and compared with what the scheduler plays from the
  original - start, length, key, retune and velocity; every clef; keys -7..7;
  lyrics; grand staff with a pickup and a change of meter; a transposing
  part; chant; element order; a hand-written foreign document; hostile input.

## QML staff view

- New `ui/NotationView.qml` + C++ `QQuickItem` (`src/app/notation_item.*`)
  that renders a `Staff` using a SMuFL font. Font: Bravura (SIL OFL, bundled
  under `ui/fonts/` — redistributable). If the font file is absent the item
  renders staff lines and noteheads as fallback shapes and logs once.
- The view is a fourth projection of the canonical pattern (repo invariant):
  it observes the pattern model, never keeps its own copy.
- Repo rule: GUI changes need `bdd`/`e2e`/`integration`/`screenshot` tests and
  the PNG must be inspected with an image tool, not just checked for existence.
  Add `features/notation.feature` (Given/When/Then) mapping to the screenshot
  test.

### As built

- `ui/ScoreView.qml` + `src/app/score_model.{hpp,cpp}`: the model lays the
  open part out through `to_staff()` and `place()` - each note's x in staff
  spaces from the rhythm, barlines, a chord's one stem, two voices' stems
  apart (the higher voice up), ties to the note they continue into, triplet
  numbers, accidentals by the bar's rule, the key signature at each clef's
  conventional places, and the highest and lowest staff positions - and the
  Canvas draws it with Bravura glyphs (`ui/Smufl.js`), sizing the staff to fit
  and scrolling sideways for a long part. A note the step grid wrote shorter
  than its step is drawn to the end of the step.
- The clef of each part and the key and meter of each section are the song's
  (`PatternSlot::clef`, `Section::key_fifths/meter_beats/meter_unit/unmetered`),
  saved by `records_notation.cpp` and undone with the song; a section with no
  meter of its own follows the song's meter where it plays. The view's
  pickers and CHANT set them.
- The view switcher's SCORE chip shows it in the editors' place. It is not
  part of ALL, which keeps the tracker and the roll at usable sizes.
- Tests: `features/score_view.feature` + `bdd_score`, with inspected
  screenshots (treble, French violin, three sharps, chant, a long two-voice
  part), and the save/undo of the settings.
- Still open (follow-ups, not regressions): no note entry on the staff (lyrics
  are typed in the LYRICS field); one part at a time (no full score); no beams (flags only); tuplets show their number but no
  bracket; spacing is proportional but not Gould's; mid-piece changes of
  transposition are not modelled; the bridge never fills lyrics from a
  pattern - lyrics now live on the pattern's notes themselves.

## Lyrics

A pattern's `Trigger::lyric` holds the syllable sung on that step, saved by
`records_lyrics.cpp`, so lyrics are part of the song like its notes. The
SCORE view's LYRICS field (and MCP's `set_lyrics`) lays a typed line on the
open part's notes in time order through `split_lyrics()`. The view draws each
syllable under its note - Hebrew in the bundled Noto Sans Hebrew, so it reads
the same on every machine - and the hyphen of a word that goes on between it
and the next syllable, whichever way the script runs.

`NotationNote.lyric` holds one syllable; a trailing `-` ("Al-") says the word
goes on into the next syllable, and `_` holds the previous syllable under the
note as a melisma.
The QML view draws syllables under noteheads; Qt handles RTL shaping, so
Hebrew hymn text works without extra code.

## Chant / unmetered

`TimeSignature::unmetered()`: no time signature drawn, no barlines, optional
stemless noteheads (`NoteValue` with `stemless` flag). This covers nusach-style
prayer chant; metered hymns use normal signatures.

## Build & test

- New sources join `blokkily_core`; new tests join `blokkily_tests`
  (and `features/notation.feature` for the user-visible scenarios).
- Definition of done per repo AGENTS.md: full `cmake` configure + build,
  full `ctest --output-on-failure`, screenshot PNGs inspected, suite re-run
  after the final edit, exact check list reported.
- pugixml (MIT) is vendored under `third_party/pugixml/` for MusicXML —
  three files, no build changes beyond adding them to the core target.
