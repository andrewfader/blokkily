#pragma once

// Lyrics as a producer types them, and as the notes carry them. A line such
// as "A-ma-zing grace how sweet" becomes one syllable per note: words split
// at spaces, and a word's hyphens split it into syllables that keep their
// trailing "-" ("A-", "ma-", "zing"), so the word can be rejoined. A lone
// "_" holds the previous syllable over another note (a melisma). Any script
// works: only ASCII spaces, tabs, newlines, "-" and "_" are read, so Hebrew
// text, its vowel points and its maqaf pass through untouched.

#include <string>
#include <string_view>
#include <vector>

namespace blokkily::notation {

[[nodiscard]] std::vector<std::string> split_lyrics(std::string_view text);
// The inverse: syllables ending in "-" join the next one, others are
// separated by a space.
[[nodiscard]] std::string join_lyrics(const std::vector<std::string>& syllables);

} // namespace blokkily::notation
