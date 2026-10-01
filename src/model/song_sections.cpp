// Sections (song.hpp, Section): a song's patterns grouped into the sections a
// producer names, one part per track. Everything that plays a song still
// plays parts through clips, so this file only keeps the grouping whole.

#include "blokkily/model/song.hpp"

#include <algorithm>
#include <utility>

namespace blokkily {

namespace {

// Whether track `track` already has a clip sounding anywhere in [start, end).
bool track_busy(const Song& song, std::size_t track, Tick start, Tick end) {
    return std::any_of(song.clips.begin(), song.clips.end(), [&](const Clip& clip) {
        if (clip.track != track || clip.pattern >= song.patterns.size()) return false;
        const Tick span = clip.span(song.patterns[clip.pattern].pattern.length());
        return clip.start < end && start < clip.start + span;
    });
}

// Places part `part` on track `track` wherever another part of its section is
// placed and the track is free, so a part plays wherever its section does.
void mirror_placements(Song& song, std::size_t part) {
    const auto& slot = song.patterns[part];
    std::vector<Clip> placed;
    for (const auto& clip : song.clips) {
        if (clip.pattern >= song.patterns.size() || clip.pattern == part) continue;
        if (song.patterns[clip.pattern].section != slot.section) continue;
        const bool seen = std::any_of(placed.begin(), placed.end(), [&](const Clip& other) {
            return other.start == clip.start && other.repeats == clip.repeats &&
                   other.length == clip.length && other.offset == clip.offset;
        });
        if (!seen) placed.push_back(clip);
    }
    const Tick length = slot.pattern.length();
    for (auto clip : placed) {
        if (track_busy(song, slot.track, clip.start, clip.start + clip.span(length))) continue;
        clip.track = slot.track;
        clip.pattern = part;
        song.clips.push_back(clip);
    }
}

PatternSlot empty_part(const Section& section, std::size_t index, std::size_t track,
                       Tick length, Tick ticks_per_beat) {
    return PatternSlot{section.name, Pattern(length, ticks_per_beat), index, track};
}

} // namespace

std::optional<std::size_t> Song::part(std::size_t section, std::size_t track) const {
    if (section >= sections.size()) return std::nullopt;
    for (std::size_t index = 0; index < patterns.size(); ++index)
        if (patterns[index].section == section && patterns[index].track == track) return index;
    return std::nullopt;
}

bool Song::sections_whole() const {
    if (sections.empty()) return false;
    std::vector<int> parts(sections.size() * tracks.size(), 0);
    std::vector<Tick> lengths(sections.size(), 0);
    for (const auto& slot : patterns) {
        if (slot.section >= sections.size() || slot.track >= tracks.size()) return false;
        ++parts[slot.section * tracks.size() + slot.track];
        auto& length = lengths[slot.section];
        if (length == 0) length = slot.pattern.length();
        else if (length != slot.pattern.length()) return false;
    }
    return std::all_of(parts.begin(), parts.end(), [](int count) { return count == 1; });
}

void Song::adopt_sections() {
    if (tracks.empty() || sections_whole()) return;
    sections.clear();
    const std::size_t legacy = patterns.size();
    for (std::size_t index = 0; index < legacy; ++index) {
        sections.push_back({patterns[index].name});
        // The tracks that play this pattern, in the order they first do.
        std::vector<std::size_t> players;
        const auto note = [&](std::size_t track) {
            if (track < tracks.size() &&
                std::find(players.begin(), players.end(), track) == players.end())
                players.push_back(track);
        };
        for (const auto& clip : clips)
            if (clip.pattern == index) note(clip.track);
        for (const auto& scene : launcher.scenes)
            for (std::size_t track = 0; track < scene.cells.size(); ++track)
                if (scene.cells[track] && scene.cells[track]->pattern == index) note(track);
        const std::size_t owner = players.empty() ? 0 : players.front();
        patterns[index].section = index;
        patterns[index].track = owner;
        for (std::size_t track = 0; track < tracks.size(); ++track) {
            if (track == owner) continue;
            const bool plays = std::find(players.begin(), players.end(), track) != players.end();
            // A track that played the pattern keeps its notes as a part of its
            // own; any other track starts the section empty.
            auto part = plays ? patterns[index]
                              : empty_part(sections.back(), index, track,
                                           patterns[index].pattern.length(),
                                           patterns[index].pattern.ticks_per_beat());
            part.track = track;
            patterns.push_back(std::move(part));
            if (!plays) continue;
            const std::size_t copy = patterns.size() - 1;
            for (auto& clip : clips)
                if (clip.pattern == index && clip.track == track) clip.pattern = copy;
            for (auto& scene : launcher.scenes)
                if (track < scene.cells.size() && scene.cells[track] &&
                    scene.cells[track]->pattern == index)
                    scene.cells[track]->pattern = copy;
        }
    }
    for (std::size_t index = 0; index < patterns.size(); ++index) mirror_placements(*this, index);
}

std::size_t Song::add_section(std::string name, Tick length, Tick ticks_per_beat) {
    sections.push_back({std::move(name)});
    const std::size_t index = sections.size() - 1;
    for (std::size_t track = 0; track < tracks.size(); ++track)
        patterns.push_back(empty_part(sections[index], index, track, length, ticks_per_beat));
    return index;
}

void Song::add_track_parts(std::size_t track) {
    if (sections.empty() || track >= tracks.size()) return;
    for (std::size_t section = 0; section < sections.size(); ++section) {
        if (part(section, track)) continue;
        // As long as the section's other parts.
        Tick length = 1920;
        Tick beat = 480;
        for (const auto& slot : patterns)
            if (slot.section == section) {
                length = slot.pattern.length();
                beat = slot.pattern.ticks_per_beat();
                break;
            }
        patterns.push_back(empty_part(sections[section], section, track, length, beat));
        mirror_placements(*this, patterns.size() - 1);
    }
}

void Song::remove_pattern(std::size_t index) {
    if (index >= patterns.size()) return;
    patterns.erase(patterns.begin() + static_cast<std::ptrdiff_t>(index));
    std::erase_if(clips, [index](const Clip& clip) { return clip.pattern == index; });
    for (auto& clip : clips)
        if (clip.pattern > index) --clip.pattern;
    launcher.remove_pattern(index);
}

bool Song::remove_section(std::size_t section) {
    if (section >= sections.size() || sections.size() < 2) return false;
    for (std::size_t index = patterns.size(); index-- > 0;)
        if (patterns[index].section == section) remove_pattern(index);
    sections.erase(sections.begin() + static_cast<std::ptrdiff_t>(section));
    for (auto& slot : patterns)
        if (slot.section > section) --slot.section;
    return true;
}

std::size_t Song::place_section(std::size_t section, Tick start) {
    std::size_t placed = 0;
    for (std::size_t track = 0; track < tracks.size(); ++track) {
        const auto index = part(section, track);
        if (!index) continue;
        const Tick length = patterns[*index].pattern.length();
        if (track_busy(*this, track, start, start + length)) continue;
        clips.push_back({track, *index, start, 1});
        ++placed;
    }
    return placed;
}

} // namespace blokkily
