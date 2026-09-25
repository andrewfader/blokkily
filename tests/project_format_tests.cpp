// Executable scenarios for features/project_format.feature: the project file
// format policy (plan §F-F). Each block names the scenario it proves.

#include "blokkily/project/paths.hpp"
#include "blokkily/project/project.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace blokkily;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::cerr << "FAILED: " << what << '\n';
    ++failures;
}

// A format 4 file as the build before this policy wrote it: every record kind
// format 4 defines, with values that print back exactly. The instrument path
// carries an escaped space and the state is the three bytes 00 01 02.
const std::string v4_body =
    "name My%20Song\n"
    "tempo 128\n"
    "master -1.5\n"
    "tuning Custom 1200 60 3 0 400 700\n"
    "scale Major 7 0 200 400 500 700 900 1100\n"
    "harmony 2 1\n"
    "pattern Verse 1920 480\n"
    "trigger 0 1 0 240 0 1 1 0 note 60 0.5 0 0\n"
    "lock 0 1 cutoff 3 modulation 0.25\n"
    "trigger 0 2 480 240 5 0.75 2 1 chord 62 1 10 3 0 4 7 0 -13.5 2\n"
    "pattern Chorus 3840 480\n"
    "track Lead -7.5 0.25 1 0 CLAP /plugins/a%20b.clap com.example.lead AAEC\n"
    "track Pad 0 0 0 1 VST3 /plugins/pad.vst3 ~ ~\n"
    "clip 0 0 0 2\n"
    "clip 1 1 3840 1\n";

// The same session as format 5 writes it since the timebase (item 1.2): the
// legacy tempo line is read but never written; the tempo is a tempo point at
// tick 0, and the meter is written beside it.
const std::string v5_body = [] {
    std::string body = v4_body;
    body.erase(body.find("tempo 128\n"), std::string("tempo 128\n").size());
    return body + "tempo_point 0 128 step\nmeter 0 4 4\n";
}();

std::string read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

int main() {
    std::string error;

    // Scenario: A project saved in format 4 still opens.
    {
        const auto project = ProjectFile::parse("blokkily-project 4\n" + v4_body, &error);
        check(project.has_value(), "a format 4 file parses");
        if (project) {
            check(project->name == "My Song", "v4 name");
            check(project->song.tempo.points == std::vector<TempoPoint>{{0, 128.0, false}},
                  "v4 tempo is one tempo point at tick 0");
            check(project->song.master_gain_db == -1.5, "v4 master");
            check(project->song.tuning.name == "Custom" &&
                      project->song.tuning.degrees.size() == 3,
                  "v4 tuning");
            check(project->song.scale.cents.size() == 7, "v4 scale");
            check(project->song.root_degree == 2 && project->song.auto_scale, "v4 harmony");
            check(project->song.patterns.size() == 2, "v4 patterns");
            check(project->song.patterns[0].pattern.events().size() == 2, "v4 triggers");
            check(project->song.tracks.size() == 2 &&
                      project->song.tracks[0].instrument.path == "/plugins/a b.clap" &&
                      project->song.tracks[0].instrument.state.size() == 3 &&
                      project->song.tracks[0].mix.mute && project->song.tracks[1].mix.solo,
                  "v4 tracks");
            check(project->song.clips.size() == 2, "v4 clips");
            // Nothing is lost or reshaped: the header changes, and the legacy
            // tempo line becomes the tempo point it means (plan C3).
            check(ProjectFile::serialize(*project) == "blokkily-project 5\n" + v5_body,
                  "a v4 file saves as the same records under the v5 header");
        }
    }

    // Scenario: Saving the same project twice gives the same bytes.
    {
        const std::string v5_text = "blokkily-project 5\n" + v5_body;
        const auto project = ProjectFile::parse(v5_text, &error);
        check(project.has_value(), "a format 5 file parses");
        if (project) {
            const auto first = ProjectFile::serialize(*project);
            const auto second = ProjectFile::serialize(*project);
            check(first == second, "serialize is byte-identical twice in a row");
            check(first == v5_text, "serialize reproduces the file it was parsed from");

            const auto folder = std::filesystem::temp_directory_path() /
                                ("blokkily-project-format-" + std::to_string(::getpid()));
            std::filesystem::remove_all(folder);
            const auto one = folder / "one.blok";
            const auto two = folder / "nested" / "two.blok";
            check(ProjectFile::save(*project, one, &error), "first save");
            const auto reopened = ProjectFile::load(one, &error);
            check(reopened.has_value(), "the saved file reloads");
            if (reopened) check(ProjectFile::save(*reopened, two, &error), "second save");
            check(read_file(one) == v5_text && read_file(two) == v5_text,
                  "both saved files are byte-identical to the file opened");
            std::filesystem::remove_all(folder);
        }
    }

    // Scenario: A file from before the song model is refused.
    for (const char* header : {"blokkily-project 3\n", "blokkily-project 2\n",
                               "blokkily-project 6\n", "blokkily-project 99\n"}) {
        error.clear();
        check(!ProjectFile::parse(header + v4_body, &error).has_value() && !error.empty(),
              header);
    }
    // Records stay strict inside the accepted range.
    error.clear();
    check(!ProjectFile::parse("blokkily-project 5\n" + v4_body + "nonsense 1\n", &error) &&
              error == "unknown record: nonsense",
          "an unknown record is refused in v5");

    // Scenario: The legacy tempo line is read in every format.
    for (const char* header : {"blokkily-project 4\n", "blokkily-project 5\n"}) {
        const auto project = ProjectFile::parse(
            std::string(header) +
                "tempo 97.5\npattern P 1920 480\ntrack T 0 0 0 0 CLAP /a.clap ~ ~\n",
            &error);
        check(project.has_value() &&
                  project->song.tempo.points == std::vector<TempoPoint>{{0, 97.5, false}},
              header);
    }

    // Scenario: A file inside the project folder is referenced relative to it.
    {
        const std::filesystem::path base = "/music/My Song";
        const std::filesystem::path inside = "/music/My Song/My Song.audio/take 1.wav";
        const auto stored = to_project_relative(inside, base);
        check(stored == std::filesystem::path("My Song.audio/take 1.wav"),
              "a file under base_dir is written relative");
        check(stored.is_relative(), "the stored path is relative");
        check(resolve_project_path(stored, base) == inside,
              "the relative path resolves back to the absolute file");
        check(to_project_relative(inside, "/music/My Song/") == stored,
              "a trailing separator on base_dir does not matter");
        check(resolve_project_path(stored, "/music/My Song/") == inside,
              "resolving against base_dir/ gives the same file");

        const std::filesystem::path outside = "/samples/kick.wav";
        check(to_project_relative(outside, base) == outside,
              "a file outside base_dir keeps its absolute path");
        check(to_project_relative("/music/My Song2/x.wav", base) == "/music/My Song2/x.wav",
              "a sibling folder sharing a name prefix is outside");
        check(resolve_project_path(outside, base) == outside, "absolute paths resolve as-is");
        check(to_project_relative(inside, {}) == inside, "no base_dir leaves the path alone");
        check(resolve_project_path("a/b.wav", {}) == "a/b.wav",
              "no base_dir leaves a relative path alone");
        check(to_project_relative(base, base) == base, "base_dir itself stays absolute");
    }

    // Record files that name no file are unaffected by base_dir: serializing
    // with a base directory gives exactly the same text as without one.
    {
        const auto project = ProjectFile::parse("blokkily-project 4\n" + v4_body, &error);
        check(project && ProjectFile::serialize(*project, "/plugins") ==
                             ProjectFile::serialize(*project),
              "core records ignore base_dir");
        const auto reread = project
            ? ProjectFile::parse(ProjectFile::serialize(*project), &error, "/plugins")
            : std::nullopt;
        check(reread && reread->song.tracks[0].instrument.path == "/plugins/a b.clap",
              "instrument paths are not rewritten");
    }

    if (failures != 0) {
        std::cerr << failures << " project format check(s) failed\n";
        return 1;
    }
    std::cout << "project format checks passed\n";
    return 0;
}
