#include "blokkily/notation/lyrics.hpp"

namespace blokkily::notation {

std::vector<std::string> split_lyrics(std::string_view text) {
    std::vector<std::string> syllables;
    std::string current;
    const auto end_word = [&] {
        if (!current.empty()) syllables.push_back(current);
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
