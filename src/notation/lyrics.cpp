#include "blokkily/notation/lyrics.hpp"

namespace blokkily::notation {

std::string fit_lyric(std::string text) {
    if (text.size() <= max_lyric_bytes) return text;
    // A syllable that goes on to the next keeps its hyphen.
    const bool joins = text.back() == '-';
    std::size_t end = max_lyric_bytes - (joins ? 1 : 0);
    // Back off a UTF-8 continuation byte (10xxxxxx) to its character's start.
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) --end;
    text.resize(end);
    if (joins) text += '-';
    return text;
}

std::vector<std::string> split_lyrics(std::string_view text) {
    std::vector<std::string> syllables;
    std::string current;
    const auto end_word = [&] {
        if (!current.empty()) syllables.push_back(fit_lyric(current));
        current.clear();
    };
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            end_word();
        } else if (c == '-' && !current.empty()) {
            current += '-';
            end_word();
        } else if (c != '-') {
            current += c;
        }
    }
    end_word();
    return syllables;
}

std::string join_lyrics(const std::vector<std::string>& syllables) {
    std::string text;
    for (std::size_t i = 0; i < syllables.size(); ++i) {
        text += syllables[i];
        const bool joins = syllables[i].size() > 1 && syllables[i].back() == '-';
        if (!joins && i + 1 < syllables.size()) text += ' ';
    }
    return text;
}

} // namespace blokkily::notation
