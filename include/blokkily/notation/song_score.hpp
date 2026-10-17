#pragma once

// The song as a score, and a score into the song: what the window's
// MusicXML export and import do (features/musicxml_window.feature).

#include "blokkily/model/song.hpp"
#include "blokkily/notation/staff.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace blokkily::notation {

// The song as a score: one part per track that has clips, each the track's
// clips laid on the song's timeline - every repeat, trims and cuts honoured -
// under the song's meters, with the clef of the track's part and the key of
// the section wherever a clip starts. A song with no clips gives section
// `fallback_section` instead, one part per track. The tempo is the song's
// first.
[[nodiscard]] Score song_score(const Song& song, const std::string& title, std::size_t fallback_section = 0);

// The notes a track plays, as one pattern on the song's timeline: what
// song_score writes for it, and what an export is compared with.
[[nodiscard]] Pattern track_timeline(const Song& song, std::size_t track, Tick end);

struct ScoreLanding {
    std::size_t section = 0;
    std::vector<std::size_t> tracks; // one per staff, in score order
    Tick start = 0;                  // where its clips were placed
    int notes = 0;
    int dropped = 0;                 // outside MIDI 0-127
    bool fresh = false;              // the song had no notes before
};

// Adds the score to the song as a new section, long enough for every staff,
// with one track per staff (named after its part) holding that staff's
// notes, lyrics, clef, key and meter, and the section's clips placed after
// the arrangement's last. A song with no notes at all reuses its tracks and
// takes the score's meter and tempo. The song must have sections.
ScoreLanding add_score(Song& song, const Score& score);

} // namespace blokkily::notation
