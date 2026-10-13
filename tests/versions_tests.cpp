// features/versions.feature: the song's notes and the project's versions in
// the project file. Each case is its own CTest test (versions_<case>).

#include "blokkily/project/project.hpp"

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace blokkily;

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

Song song_with_note(double bpm) {
    Song song;
    song.tempo.points.front().bpm = bpm;
    Trigger trigger;
    trigger.start = 120;
    trigger.duration = 120;
    trigger.musical_data = Note{64, 0.8F};
    (void)song.patterns.front().pattern.add(trigger);
    return song;
}

// Scenario: Notes and versions are saved with the project
void project_case() {
    Project project;
    project.song = song_with_note(120.0);
    project.song.notes = "hook in bar nine\nmix: less reverb, \"more\" bass 100%";
    Project then;
    then.song = song_with_note(90.0);
    project.versions = {{"FIRST", 1760000000, ProjectFile::serialize(then)},
                        {"mix two", 1760003600, ProjectFile::serialize(project)}};
    const auto text = ProjectFile::serialize(project);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project parses back: " + error);
    require(ProjectFile::serialize(*loaded) == text, "the text round-trips byte for byte");
    require(loaded->song.notes == project.song.notes, "the notes come back, every character");
    require(loaded->versions == project.versions, "every version comes back, in order");
    // A version is the song as it was: it opens as a project of its own.
    const auto first = ProjectFile::parse(loaded->versions.front().snapshot, &error);
    require(first.has_value() && first->song.tempo.points.front().bpm == 90.0 &&
                first->song.patterns.front().pattern.events().size() == 1 &&
                first->versions.empty(),
            "a version opens as the song it was, with no versions of its own: " + error);
}

// Scenario: A project with neither is unchanged
void unchanged_case() {
    Project project;
    project.song = song_with_note(120.0);
    const auto text = ProjectFile::serialize(project);
    require(text.find("\nnotes ") == std::string::npos &&
                text.find("\nversion ") == std::string::npos,
            "a song with no notes and no versions writes neither record");
}

// Scenario: A malformed version or a second notes record is refused
void malformed_case() {
    Project project;
    project.song = song_with_note(120.0);
    const auto text = ProjectFile::serialize(project);
    std::string error;
    require(!ProjectFile::parse(text + "version FIRST -5 x\n", &error).has_value(),
            "a version saved before 1970 is refused");
    require(!ProjectFile::parse(text + "version FIRST\n", &error).has_value(),
            "a version without its time and snapshot is refused");
    require(!ProjectFile::parse(text + "notes a\nnotes b\n", &error).has_value(),
            "a project has one notes record");
}
} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"project", project_case}, {"unchanged", unchanged_case}, {"malformed", malformed_case}};
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_versions_tests <case>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& failure) {
        std::cerr << argv[1] << ": " << failure.what() << '\n';
        return 1;
    }
    std::cout << argv[1] << ": ok\n";
    return 0;
}
