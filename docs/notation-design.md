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
| `duration.hpp` | `NoteValue` (breve..128th), dots, `ticks()`/`from_ticks()` against a ticks-per-beat |
| `note.hpp` | `NotationNote{spelled, value, dots, tie_start/tie_end, lyric, voice}`, `Rest` |
| `staff.hpp` | `Staff{clef, key, time, transpose}`, `Measure{notes}`, `Part{name, staves}`, `Score{parts, title, composer}` |
| `spelling.hpp` | `spell(midi_key, KeySignature) -> SpelledPitch` — enharmonic spelling |
| `layout.hpp` | `staff_position(spelled, clef) -> int` (diatonic steps above/below the clef's bottom line), accidental visibility vs key signature |
| `pattern_bridge.hpp` | `to_staff(pattern, key, clef, ticks_per_beat)`, `to_pattern(staff, ticks_per_beat)` — round-trips through the canonical pattern model; the pattern stays the single source of truth (repo invariant: editors are projections of one canonical pattern) |
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

## Quantization

`from_ticks()`: exact durations map to a value+dots; anything else becomes a
tied chain of values (whole+half, etc.). Rests are NOT invented — the staff
shows only the pattern's notes; measure filling is a view concern for later.

## MusicXML scope (v1)

- Export: `<score-partwise>`, part list, measures with attributes
  (divisions, key, time, clef), notes (pitch, duration, voice, type, dots,
  ties, lyrics), rests, barlines. One part per Blokkily track.
- Import: the same subset back. Unknown elements are skipped, never fatal.
- Tests: write → read round-trip equality on a score exercising every clef,
  all key signatures -7..+7, tuplets excluded v1 (documented), lyrics, ties.
- Repo rule: "export behaviour must read the written file back and compare it
  with what the engine renders" — the round-trip test plays the re-imported
  score through `to_pattern` and compares scheduled note keys/starts.

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

## Lyrics

`NotationNote.lyric` holds one syllable; `-` continues a melisma, `_` extends.
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
