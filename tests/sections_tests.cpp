// Sections (features/per_stem_editing.feature): a song's patterns grouped into
// sections, one part per track. Proved on the model and the project file:
// a song written before sections plays exactly the same notes once it has
// them; adding and removing tracks and sections keeps one part per section
// and track; and a song with sections saves and loads byte for byte.
//
// Run with a case name; each case is its own CTest test (sections_<case>).

#include "blokkily/model/song.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

void note(Pattern& pattern, Tick start, int key) {
    Trigger trigger;
    trigger.start = start;
    trigger.duration = 120;
    trigger.musical_data = Note{static_cast<std::int16_t>(key), 0.8F, 0.0F};
    (void)pattern.add(trigger);
}

// What a track plays, as (start, key) pairs in the order arrange() gives.
std::vector<std::pair<Tick, int>> heard(const Song& song, std::size_t track) {
    std::vector<std::pair<Tick, int>> notes;
    for (const auto& scheduled : song.arrange(track).notes)
        notes.emplace_back(scheduled.start, scheduled.key);
    std::sort(notes.begin(), notes.end());
    return notes;
}

// Three tracks; VERSE played by tracks 0 and 1 (edit once, see everywhere,
// as songs before sections did), CHORUS by track 2, and a launcher cell of
// VERSE on track 1.
Song legacy_song() {
    Song song;
    song.tracks.resize(3);
    song.patterns = {{"VERSE", Pattern(1920, 480)}, {"CHORUS", Pattern(1920, 480)}};
    note(song.patterns[0].pattern, 0, 60);
    note(song.patterns[0].pattern, 960, 64);
    note(song.patterns[1].pattern, 480, 67);
    song.clips = {{0, 0, 0, 2}, {1, 0, 1920, 1}, {2, 1, 0, 1}};
    song.launcher.add_scene("A");
    song.launcher.set_slot(0, 1, SceneSlot{0});
    return song;
}

void adopt_keeps_what_plays() {
    auto song = legacy_song();
    const auto before = std::make_tuple(heard(song, 0), heard(song, 1), heard(song, 2));
    song.adopt_sections();
    std::string why;
    require(song.consistent(&why), "adopted song is consistent: " + why);
    require(song.sections.size() == 2 && song.sections[0].name == "VERSE" &&
                song.sections[1].name == "CHORUS",
            "each pattern becomes a section of the same name");
    require(song.patterns.size() == 6, "every section has a part for each of three tracks");
    require(std::make_tuple(heard(song, 0), heard(song, 1), heard(song, 2)) == before,
            "every track plays exactly the notes it played before");
    // Tracks 0 and 1 shared VERSE; each now has a part of its own.
    const auto verse0 = song.part(0, 0);
    const auto verse1 = song.part(0, 1);
    require(verse0 && verse1 && *verse0 != *verse1, "two players get two parts");
    require(song.patterns[*verse1].pattern.events().size() == 2,
            "the second player's part is a copy of the notes");
    note(song.patterns[*verse1].pattern, 1440, 72);
    require(song.patterns[*verse0].pattern.events().size() == 2,
            "editing one track's part leaves the other track's alone");
    require(song.launcher.slot(0, 1) && song.launcher.slot(0, 1)->pattern == *verse1,
            "the launcher cell moves onto the copy");
    // A track that did not play VERSE has an empty part of it, placed where
    // VERSE is and its own timeline is free.
    const auto verse2 = song.part(0, 2);
    require(verse2 && song.patterns[*verse2].pattern.events().empty(), "an empty part");
    const auto chorus0 = song.part(1, 0);
    require(chorus0 && song.patterns[*chorus0].pattern.events().empty(),
            "track 0 has an empty CHORUS part");
    // Track 2's bar 1 is CHORUS, so VERSE's empty part only fills bar 2.
    int verse2_clips = 0;
    for (const auto& clip : song.clips)
        if (clip.pattern == *verse2) {
            ++verse2_clips;
            require(clip.start == 1920, "the empty part fills only the free bar");
        }
    require(verse2_clips == 1, "the empty part is placed once");
    // Adopting twice changes nothing.
    const auto patterns = song.patterns.size();
    const auto clips = song.clips.size();
    song.adopt_sections();
    require(song.patterns.size() == patterns && song.clips.size() == clips,
            "a song with sections is left alone");
}

void tracks_and_sections_stay_whole() {
    auto song = legacy_song();
    song.adopt_sections();
    std::string why;

    song.tracks.push_back(Track{});
    song.add_track_parts(3);
    require(song.consistent(&why), "a new track gets a part in every section: " + why);
    const auto verse3 = song.part(0, 3);
    require(verse3 && song.patterns[*verse3].pattern.events().empty(), "its parts are empty");
    bool placed = false;
    for (const auto& clip : song.clips)
        placed = placed || (clip.track == 3 && clip.pattern == *verse3 && clip.start == 0);
    require(placed, "its VERSE part plays wherever VERSE does");

    const auto bridge = song.add_section("BRIDGE", 960, 480);
    require(bridge == 2 && song.consistent(&why), "a new section has a part per track: " + why);
    require(song.patterns[*song.part(bridge, 1)].pattern.length() == 960, "of its length");
    const auto clips = song.clips.size();
    require(song.place_section(bridge, 4 * 1920) == 4 && song.clips.size() == clips + 4,
            "placing a section places every track's part");
    require(song.place_section(bridge, 4 * 1920) == 0, "and never on a track that is busy");

    require(!song.remove_track(1).empty() && song.consistent(&why),
            "removing a track removes its parts: " + why);
    require(song.patterns.size() == 9, "three sections by three tracks remain");
    require(song.remove_section(0) && song.consistent(&why),
            "removing a section removes its parts and clips: " + why);
    require(song.sections.size() == 2 && song.sections[0].name == "CHORUS",
            "later sections move down");
    for (const auto& clip : song.clips)
        require(song.patterns[clip.pattern].section < 2, "no clip names a removed part");
    require(song.remove_section(0) && !song.remove_section(0), "the last section stays");
}

void project_round_trip() {
    Project project;
    project.song = legacy_song();
    project.song.adopt_sections();
    const auto text = ProjectFile::serialize(project, {});
    require(text.find("\nsection VERSE\n") != std::string::npos &&
                text.find("\npart 0 0 0\n") != std::string::npos,
            "sections and parts are written");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error, {});
    require(loaded.has_value(), "a song with sections loads: " + error);
    require(loaded->song.sections == project.song.sections, "the sections come back");
    for (std::size_t index = 0; index < project.song.patterns.size(); ++index)
        require(loaded->song.patterns[index].section == project.song.patterns[index].section &&
                    loaded->song.patterns[index].track == project.song.patterns[index].track,
                "every part comes back as the part it was");
    require(ProjectFile::serialize(*loaded, {}) == text, "and saves byte for byte the same");

    // A file written before sections loads without them, unchanged.
    Project legacy;
    legacy.song = legacy_song();
    const auto old = ProjectFile::serialize(legacy, {});
    require(old.find("section ") == std::string::npos, "no section records without sections");
    const auto reread = ProjectFile::parse(old, &error, {});
    require(reread && reread->song.sections.empty() &&
                ProjectFile::serialize(*reread, {}) == old,
            "a file without sections round-trips unchanged");

    // A part record for a pattern that does not exist is refused.
    auto broken = text;
    broken += "part 99 0 0\n";
    require(!ProjectFile::parse(broken, &error, {}), "a stray part record is refused");
}

} // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "";
    try {
        if (which == "adopt_keeps_what_plays") adopt_keeps_what_plays();
        else if (which == "tracks_and_sections_stay_whole") tracks_and_sections_stay_whole();
        else if (which == "project_round_trip") project_round_trip();
        else {
            std::cerr << "unknown case: " << which << '\n';
            return 2;
        }
    } catch (const std::exception& failure) {
        std::cerr << "FAILED: " << failure.what() << '\n';
        return 1;
    }
    return 0;
}
