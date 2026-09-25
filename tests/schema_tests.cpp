// Executable scenarios for features/program_schema.feature: the program
// schema (plan §F-E, item 1.5). Each block names the scenario it proves.

#include "blokkily/model/song.hpp"
#include "blokkily/project/project.hpp"

#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    std::cerr << "FAILED: " << what << '\n';
    ++failures;
}

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int value : values) out.push_back(static_cast<std::byte>(value));
    return out;
}

AutomationTarget target(AutomationTarget::Kind kind, BusKind bus_kind, std::uint32_t bus,
                        std::int32_t slot, std::int32_t index = 0, std::string id = {}) {
    AutomationTarget out;
    out.kind = kind;
    out.processor = {bus_kind, bus, slot};
    out.parameter_index = index;
    out.parameter_id = std::move(id);
    return out;
}

// A project that uses every field the schema adds. Its song-level core records
// are whatever the core module writes; the new records are spelled out in
// `new_records` below, so the writer is checked against text, not itself.
Project full_project() {
    Project project;
    project.name = "Schema";
    auto& song = project.song;
    song.tracks.resize(2);
    song.tracks[0].name = "Lead";
    song.tracks[0].instrument = {"CLAP", "/plugins/lead.clap", "com.example.lead", {}};
    song.tracks[1].name = "Bass";

    song.audio_files = {{"/music/kick.wav", 48000, 48000, 1},
                        {"/music/Song/Song.audio/take 1.wav", 96000, 44100, 2}};
    song.audio_clips = {{7, 0, 0, 0, 0, 48000, -3.5, 10, 20},
                        {9, 1, 1, 960, 100, 1000, 0.0, 0, 0},
                        // Overlaps clip 7 on the same track (decision 6).
                        {3, 0, 0, 480, 0, 24000, 0.0, 0, 0}};

    ReturnBus reverb;
    reverb.name = "Reverb Bus";
    reverb.mix = {-6.0, 0.5, false, true};
    reverb.inserts.push_back({{"builtin", "reverb", "", {}}, false, {{0, 0.5}, {1, 0.25}}});
    song.returns.push_back(reverb);
    song.tracks[0].inserts.push_back(
        {{"CLAP", "/plugins/eq.clap", "com.example.eq", bytes({1, 2, 3})}, true, {}});
    song.master_inserts.push_back({{"builtin", "compressor", "", {}}, false, {{0, -18.0}}});
    song.tracks[0].sends.push_back({0, -12.0, false});
    song.tracks[1].sends.push_back({0, -3.0, true});

    auto& input = song.tracks[1].input;
    input.armed = true;
    input.source = TrackInput::Source::midi_and_audio;
    input.midi_channel = 9;
    input.audio_first_channel = 2;
    input.audio_channels = 1;
    input.monitor = TrackInput::Monitor::on;
    song.record_offset_samples = -128;

    using Kind = AutomationTarget::Kind;
    song.tracks[0].automation_mode = AutomationMode::latch;
    song.tracks[1].automation_mode = AutomationMode::write;
    song.tracks[0].automation.push_back(
        {target(Kind::gain, BusKind::track, 0, -1), {{0, -6.0}, {1920, 0.0}}});
    song.tracks[0].automation.push_back(
        {target(Kind::parameter, BusKind::track, 0, -1, 3, "cutoff"),
         {{0, 0.25}, {960, 0.25}, {960, 0.75}}});
    song.tracks[0].automation.push_back(
        {target(Kind::parameter, BusKind::track, 0, 0, 1), {{0, 0.5}}});
    song.tracks[1].automation.push_back(
        {target(Kind::pan, BusKind::ret, 0, -1), {{480, -0.5}}});
    song.tracks[1].automation.push_back(
        {target(Kind::parameter, BusKind::master, 0, 0, 0, "threshold"), {}});
    return project;
}

const std::string new_records =
    "audiofile /music/kick.wav 48000 48000 1\n"
    "audiofile Song.audio/take%201.wav 96000 44100 2\n"
    "audioclip 7 0 0 0 0 48000 -3.5 10 20\n"
    "audioclip 9 1 1 960 100 1000 0 0 0\n"
    "audioclip 3 0 0 480 0 24000 0 0 0\n"
    "return Reverb%20Bus -6 0.5 0 1\n"
    "insert track 0 CLAP /plugins/eq.clap com.example.eq AQID 1 0\n"
    "insert return 0 builtin reverb ~ ~ 0 2 0 0.5 1 0.25\n"
    "insert master 0 builtin compressor ~ ~ 0 1 0 -18\n"
    "send 0 0 -12 post\n"
    "send 1 0 -3 pre\n"
    "input 1 1 midi+audio 9 2 1 on\n"
    "record-offset -128\n"
    "automode 0 latch\n"
    "automode 1 write\n"
    "automation 0 gain track 0 -1 0 ~ 2 0 -6 1920 0\n"
    "automation 0 parameter track 0 -1 3 cutoff 3 0 0.25 960 0.25 960 0.75\n"
    "automation 0 parameter track 0 0 1 ~ 1 0 0.5\n"
    "automation 1 pan return 0 -1 0 ~ 1 480 -0.5\n"
    "automation 1 parameter master 0 0 0 threshold 0\n";

// Every track field the schema adds, compared one by one.
bool same_tracks(const Song& a, const Song& b) {
    if (a.tracks.size() != b.tracks.size()) return false;
    for (std::size_t t = 0; t < a.tracks.size(); ++t) {
        const auto& x = a.tracks[t];
        const auto& y = b.tracks[t];
        if (x.inserts != y.inserts || x.sends != y.sends || x.input != y.input ||
            x.automation != y.automation || x.automation_mode != y.automation_mode ||
            x.instrument != y.instrument || x.mix != y.mix || x.name != y.name)
            return false;
    }
    return true;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

std::vector<AutomationPoint> ramp(Tick from, Tick to, Tick step, double (*shape)(Tick)) {
    std::vector<AutomationPoint> out;
    for (Tick at = from; at <= to; at += step) out.push_back({at, shape(at)});
    return out;
}

} // namespace

int main() {
    std::string error;
    const std::filesystem::path base = "/music/Song";

    // Scenario: A song using every new field saves and reopens unchanged.
    // Scenario: An audio file inside the project folder is stored relative to it.
    {
        const auto project = full_project();
        std::string why;
        check(project.song.consistent(&why), "the full song is consistent: " + why);

        Project core_only = project;
        core_only.song = Song{};
        core_only.song.patterns = project.song.patterns;
        core_only.song.clips = project.song.clips;
        for (const auto& track : project.song.tracks) {
            Track plain;
            plain.name = track.name;
            plain.instrument = track.instrument;
            plain.mix = track.mix;
            core_only.song.tracks.push_back(plain);
        }
        core_only.song.tracks.erase(core_only.song.tracks.begin());
        const auto expected = ProjectFile::serialize(core_only, base) + new_records;

        const auto first = ProjectFile::serialize(project, base);
        check(first == expected, "the new records are written as specified:\n" + first);
        const auto reopened = ProjectFile::parse(first, &error, base);
        check(reopened.has_value(), "the full project reopens: " + error);
        if (reopened) {
            const auto& song = reopened->song;
            check(ProjectFile::serialize(*reopened, base) == first,
                  "a second save is byte-identical");
            check(song.audio_files == project.song.audio_files,
                  "audio files read back, the relative one as the same absolute file");
            check(song.audio_files.size() == 2 &&
                      song.audio_files[1].path ==
                          std::filesystem::path("/music/Song/Song.audio/take 1.wav"),
                  "the relative audio path resolves inside the project folder");
            check(song.audio_clips == project.song.audio_clips, "audio clips read back");
            check(song.returns == project.song.returns, "returns read back");
            check(song.master_inserts == project.song.master_inserts,
                  "master inserts read back");
            check(song.record_offset_samples == -128, "record offset reads back");
            check(same_tracks(song, project.song),
                  "inserts, sends, inputs, lanes and modes read back");
        }
        // Without a project folder the file keeps its absolute path.
        const auto absolute = ProjectFile::serialize(project);
        check(absolute.find("audiofile /music/Song/Song.audio/take%201.wav ") !=
                  std::string::npos,
              "without a project folder the audio path stays absolute");
    }

    // Scenario: A project from before these records opens with their defaults.
    {
        Project plain;
        plain.song.tracks.resize(3);
        auto text = ProjectFile::serialize(plain);
        const std::string v4 = "blokkily-project 4\n" + text.substr(text.find('\n') + 1);
        const auto project = ProjectFile::parse(v4, &error);
        check(project.has_value(), "a v4 text parses: " + error);
        if (project) {
            const auto& song = project->song;
            check(song.audio_files.empty() && song.audio_clips.empty() && song.returns.empty() &&
                      song.master_inserts.empty() && song.record_offset_samples == 0,
                  "song-level schema fields take their defaults");
            bool tracks_default = song.tracks.size() == 3;
            for (const auto& track : song.tracks)
                tracks_default = tracks_default && track.inserts.empty() && track.sends.empty() &&
                                 track.input == TrackInput{} && !track.input.armed &&
                                 track.input.source == TrackInput::Source::midi &&
                                 track.automation.empty() &&
                                 track.automation_mode == AutomationMode::read;
            check(tracks_default, "track schema fields take their defaults");
            const auto saved = ProjectFile::serialize(*project);
            check(saved == text, "saving writes none of the new records");
            std::istringstream lines(saved);
            std::string line;
            while (std::getline(lines, line))
                for (const char* record : {"audiofile", "audioclip", "return", "insert", "send",
                                           "input", "record-offset", "automode", "automation"})
                    check(!starts_with(line, std::string(record) + ' '),
                          std::string("no ") + record + " record in a default save");
        }
    }

    // Scenario: A damaged record is refused with a reason.
    {
        const auto base_text = ProjectFile::serialize(Project{});
        const struct {
            const char* records;
            const char* reason;
        } cases[] = {
            {"audiofile ~ 100 48000 1\n", "malformed audiofile record"},
            {"audiofile a.wav 0 48000 1\n", "malformed audiofile record"},
            {"audiofile a.wav 100 48000\n", "malformed audiofile record"},
            {"audiofile a.wav 100 48000 0\n", "malformed audiofile record"},
            {"audioclip 0 0 0 0 0 10 0 0 0\n", "malformed audioclip record"},
            {"audioclip 1 0 0 0 0 10 loud 0 0\n", "malformed audioclip record"},
            {"audioclip 1 0 0 0 0 0 0 0 0\n", "malformed audioclip record"},
            {"audioclip 1 0 0 -5 0 10 0 0 0\n", "malformed audioclip record"},
            {"audioclip 1 0 0 0 0 10 0 0 0\n", "audio file that does not exist"},
            {"audiofile a.wav 100 48000 1\naudioclip 1 5 0 0 0 10 0 0 0\n",
             "audio clip refers to a track that does not exist"},
            {"audiofile a.wav 100 48000 1\naudioclip 1 0 0 0 50 60 0 0 0\n",
             "runs past the end of its file"},
            {"audiofile a.wav 100 48000 1\naudioclip 1 0 0 0 0 10 0 6 5\n",
             "fades are longer than the clip"},
            {"audiofile a.wav 100 48000 1\naudioclip 4 0 0 0 0 10 0 0 0\n"
             "audioclip 4 0 0 20 0 10 0 0 0\n",
             "two audio clips share an id"},
            {"audiofile a.wav 100 48000 1\naudioclip 1 0 0 0 0 10 nan 0 0\n",
             "gain is not finite"},
            {"return R 0 0 2 0\n", "malformed return record"},
            {"return R 0 0\n", "malformed return record"},
            {"insert track 9 CLAP x ~ ~ 0 0\n", "insert refers to a bus that does not exist"},
            {"insert return 0 CLAP x ~ ~ 0 0\n", "insert refers to a bus that does not exist"},
            {"insert master 1 CLAP x ~ ~ 0 0\n", "insert refers to a bus that does not exist"},
            {"insert bus 0 CLAP x ~ ~ 0 0\n", "malformed insert record"},
            {"insert master 0 CLAP x ~ ~ 0 2 0 1\n", "malformed insert record"},
            {"insert master 0 CLAP x ~ ~ 2 0\n", "malformed insert record"},
            {"insert master 0 CLAP x ~ !!!! 0 0\n", "malformed insert record"},
            {"insert master 0 CLAP x ~ ~ 0 1 zero 1\n", "malformed insert parameter"},
            {"insert master 0 ~ x ~ ~ 0 0\n", "effect slot names no plugin format"},
            {"insert master 0 CLAP x ~ ~ 0 2 5 1 5 2\n", "effect parameter is stored twice"},
            {"send 0 0 -3 post\n", "send refers to a return that does not exist"},
            {"return R 0 0 0 0\nsend 3 0 -3 post\n", "send refers to a track that does not exist"},
            {"return R 0 0 0 0\nsend 0 0 -3 sideways\n", "malformed send record"},
            {"return R 0 0 0 0\nsend 0 0 -3 post\nsend 0 0 -6 pre\n",
             "two sends feed the same return"},
            {"input 0 1 midi 16 0 2 auto\n", "malformed input record"},
            {"input 0 1 guitar -1 0 2 auto\n", "malformed input record"},
            {"input 0 1 midi -1 0 3 auto\n", "malformed input record"},
            {"input 0 1 midi -1 0 2 loud\n", "malformed input record"},
            {"input 0 2 midi -1 0 2 auto\n", "malformed input record"},
            {"input 7 1 midi -1 0 2 auto\n", "input refers to a track that does not exist"},
            {"record-offset x\n", "malformed record-offset record"},
            {"record-offset 1 2\n", "malformed record-offset record"},
            {"automode 0 sometimes\n", "malformed automode record"},
            {"automode 4 read\n", "automode refers to a track that does not exist"},
            {"automation 0 gain track 0 -1 0 ~ 2 10 0 5 1\n", "automation points are out of order"},
            {"automation 0 gain track 0 -1 0 ~ 3 0 1 0 2 0 3\n",
             "automation points are out of order"},
            {"automation 0 gain track 0 -1 0 ~ 3 0 1\n", "malformed automation record"},
            {"automation 0 volume track 0 -1 0 ~ 0\n", "malformed automation record"},
            {"automation 0 gain track 0 -1 0 ~ 1 0 many\n", "malformed automation point"},
            {"automation 2 gain track 0 -1 0 ~ 0\n",
             "automation refers to a track that does not exist"},
            {"automation 0 gain track 4 -1 0 ~ 0\n",
             "automation lane targets a bus that does not exist"},
            {"automation 0 parameter track 0 2 0 ~ 0\n", "effect slot that does not exist"},
            {"automation 0 gain track 0 0 0 ~ 0\n", "names a processor slot"},
            {"automation 0 parameter master 0 -1 0 ~ 0\n", "instrument of a bus that has none"},
            {"automation 0 pan track 0 -1 0 ~ 1 0 2\n", "pan automation point is outside"},
            {"automation 0 gain track 0 -1 0 ~ 0\nautomation 0 gain track 0 -1 7 x 0\n",
             "two automation lanes drive the same control"},
        };
        for (const auto& bad : cases) {
            error.clear();
            const auto parsed = ProjectFile::parse(base_text + bad.records, &error);
            check(!parsed.has_value() && error.find(bad.reason) != std::string::npos,
                  std::string("refused with '") + bad.reason + "': " + bad.records +
                      "  got: " + error);
        }
    }

    // Scenario: Overlapping audio clips are allowed.
    {
        Song song;
        song.audio_files = {{"/a.wav", 1000, 48000, 2}};
        song.audio_clips = {{1, 0, 0, 0, 0, 1000, 0, 0, 0}, {2, 0, 0, 100, 0, 1000, 0, 0, 0},
                            {5, 0, 0, 100, 500, 500, 0, 250, 250}};
        std::string why;
        check(song.consistent(&why), "overlapping audio clips on one track are consistent: " + why);
        check(song.next_audio_clip_id() == 6, "the next audio clip id is unused");
    }

    // Scenario: The song refuses references that do not resolve.
    {
        const auto refused = [](const Song& song, const std::string& reason) {
            std::string why;
            check(!song.consistent(&why) && why.find(reason) != std::string::npos,
                  "refused with '" + reason + "', got '" + why + "'");
        };
        Song base_song;
        base_song.returns.resize(1);
        check(base_song.consistent(), "a song with one return is consistent");
        {
            Song song = base_song;
            song.clips.push_back({7, 0, 0, 1});
            refused(song, "a clip refers to a track or pattern");
        }
        {
            Song song = base_song;
            song.audio_files.push_back({"/a.wav", 10, 0, 1});
            refused(song, "an audio file has no path, frames, rate or channels");
        }
        {
            Song song = base_song;
            song.tracks[0].sends.push_back({1, 0.0, false});
            refused(song, "a send refers to a return that does not exist");
        }
        {
            Song song = base_song;
            song.tracks[0].sends.push_back({0, std::nan(""), false});
            refused(song, "a send level is not finite");
        }
        {
            Song song = base_song;
            song.tracks[0].input.midi_channel = 16;
            refused(song, "the input is out of range");
        }
        {
            Song song = base_song;
            song.tracks[0].input.audio_channels = 0;
            refused(song, "the input is out of range");
        }
        {
            Song song = base_song;
            song.returns[0].inserts.push_back({{"CLAP", "x", "", {}}, false, {{1, 0}, {1, 2}}});
            refused(song, "return 0: an effect parameter is stored twice");
        }
        {
            Song song = base_song;
            song.returns[0].mix.gain_db = std::nan("");
            refused(song, "a return's strip is not finite");
        }
        {
            Song song = base_song;
            song.tracks[0].automation.push_back(
                {target(AutomationTarget::Kind::gain, BusKind::ret, 0, -1),
                 {{10, 0.0}, {5, 0.0}}});
            refused(song, "an automation lane is out of order or not finite");
        }
        {
            Song song = base_song;
            song.tracks[0].automation.push_back(
                {target(AutomationTarget::Kind::parameter, BusKind::track, 0, -1, -1), {}});
            refused(song, "negative parameter index");
        }
        {
            // Two tracks automating one return's gain would fight each other.
            Song song = base_song;
            song.tracks.resize(2);
            song.tracks[0].automation.push_back(
                {target(AutomationTarget::Kind::gain, BusKind::ret, 0, -1), {}});
            song.tracks[1].automation.push_back(
                {target(AutomationTarget::Kind::gain, BusKind::ret, 0, -1), {}});
            refused(song, "two automation lanes drive the same control");
            song.tracks[1].automation[0].target.kind = AutomationTarget::Kind::pan;
            check(song.consistent(), "gain and pan of one bus are different controls");
        }
    }

    // Scenario: Removing a track re-indexes everything that names a track.
    {
        using Kind = AutomationTarget::Kind;
        Song song;
        song.patterns.resize(2);
        song.tracks.resize(3);
        song.tracks[0].name = "A";
        song.tracks[1].name = "B";
        song.tracks[2].name = "C";
        song.returns.resize(2);
        song.clips = {{0, 0, 0, 1}, {1, 1, 0, 1}, {2, 1, 1920, 2}};
        song.audio_files = {{"/a.wav", 1000, 48000, 1}};
        song.audio_clips = {{1, 0, 0, 0, 0, 100, 0, 0, 0},
                            {2, 1, 0, 0, 0, 100, 0, 0, 0},
                            {3, 2, 0, 480, 0, 100, 0, 0, 0}};
        song.tracks[0].sends = {{0, -6.0, false}};
        song.tracks[1].sends = {{0, -3.0, false}};
        song.tracks[2].sends = {{1, -9.0, true}};
        song.tracks[2].inserts.push_back({{"CLAP", "x", "", {}}, false, {}});
        song.tracks[0].automation = {
            {target(Kind::gain, BusKind::track, 0, -1), {{0, 0.0}}},
            {target(Kind::gain, BusKind::track, 1, -1), {{0, 1.0}}},                // B
            {target(Kind::parameter, BusKind::track, 2, 0, 4), {{0, 2.0}}},         // C
            {target(Kind::pan, BusKind::ret, 1, -1), {{0, 0.5}}},
        };
        song.tracks[2].automation = {{target(Kind::gain, BusKind::track, 2, -1), {{0, 3.0}}}};
        check(song.consistent(), "the three-track song is consistent");

        Song unchanged = song;
        check(unchanged.remove_track(3).empty() && unchanged.tracks.size() == 3,
              "removing a track that does not exist changes nothing");
        Song single;
        check(single.remove_track(0).empty() && single.tracks.size() == 1,
              "the only track is never removed");

        const auto map = song.remove_track(1);
        check(map.size() == 3 && map[0] == std::optional<std::size_t>{0} && !map[1] &&
                  map[2] == std::optional<std::size_t>{1},
              "the index map sends 0->0, 1->nothing, 2->1");
        check(song.tracks.size() == 2 && song.tracks[0].name == "A" && song.tracks[1].name == "C",
              "the middle track is gone");
        check(song.clips.size() == 2 && song.clips[0].track == 0 && song.clips[1].track == 1 &&
                  song.clips[1].start == 1920,
              "pattern clips re-index");
        check(song.audio_clips.size() == 2 && song.audio_clips[0].id == 1 &&
                  song.audio_clips[0].track == 0 && song.audio_clips[1].id == 3 &&
                  song.audio_clips[1].track == 1,
              "audio clips re-index and keep their ids");
        check(song.tracks[1].sends.size() == 1 && song.tracks[1].sends[0].bus == 1 &&
                  song.tracks[1].sends[0].pre_fader && song.tracks[0].sends[0].bus == 0,
              "sends move with their track and still name the same return");
        const auto& lanes = song.tracks[0].automation;
        check(lanes.size() == 3 && lanes[0].target.processor.bus == 0 &&
                  lanes[1].target.processor == ProcessorAddress{BusKind::track, 1, 0} &&
                  lanes[1].target.parameter_index == 4 &&
                  lanes[2].target.processor == ProcessorAddress{BusKind::ret, 1, -1},
              "lanes targeting the removed track go; the rest re-index; return targets stay");
        check(song.tracks[1].automation.size() == 1 &&
                  song.tracks[1].automation[0].target.processor.bus == 1,
              "the moved track's own lane follows it");
        std::string why;
        check(song.consistent(&why), "the song stays consistent: " + why);
    }

    // Scenario: Unused audio files are dropped when saving.
    {
        Song song;
        song.audio_files = {{"/a.wav", 100, 48000, 1}, {"/b.wav", 100, 48000, 1},
                            {"/c.wav", 100, 48000, 1}};
        song.audio_clips = {{1, 0, 2, 0, 0, 10, 0, 0, 0}, {2, 0, 0, 0, 0, 10, 0, 0, 0}};
        const auto map = song.prune_audio_files();
        check(map.size() == 3 && map[0] == std::optional<std::size_t>{0} && !map[1] &&
                  map[2] == std::optional<std::size_t>{1},
              "the file map drops the unused file");
        check(song.audio_files.size() == 2 && song.audio_files[0].path == "/a.wav" &&
                  song.audio_files[1].path == "/c.wav",
              "only files a clip plays remain");
        check(song.audio_clips[0].file == 1 && song.audio_clips[1].file == 0,
              "clips still name their files");
        check(song.consistent(), "the pruned song is consistent");
    }

    // Scenario: An automation lane is a ramped envelope with jumps.
    {
        AutomationLane lane;
        check(!lane.value_at(0).has_value(), "an empty lane has no value");
        lane.points = {{100, 0.0}, {200, 1.0}, {200, 5.0}, {300, 5.0}, {400, 3.0}};
        check(lane.well_formed(), "a lane with a jump is well formed");
        check(near(*lane.value_at(0), 0.0), "before the first point the lane holds it");
        check(near(*lane.value_at(150), 0.5), "halfway along a ramp");
        check(near(*lane.value_at(199), 0.99), "just before the jump");
        check(near(*lane.value_at(200), 5.0), "at a jump the lane takes the leaving value");
        check(near(*lane.value_at(350), 4.0), "a falling ramp");
        check(near(*lane.value_at(10000), 3.0), "after the last point the lane holds it");
        lane.points.push_back({400, 1.0});
        lane.points.push_back({400, 2.0});
        check(!lane.well_formed(), "three points on one tick are refused");
    }

    // Scenario: Recording a pass replaces only the range it covers.
    {
        AutomationLane lane;
        lane.points = {{0, 0.0}, {1000, 1.0}};
        const std::vector<AutomationPoint> pass{{400, 0.875}, {500, 0.75}, {600, 0.625}};
        check(lane.write_pass(400, 600, pass), "a pass is written");
        const std::vector<AutomationPoint> expected{{0, 0.0},     {400, 0.4}, {400, 0.875},
                                                    {500, 0.75},  {600, 0.625}, {600, 0.6},
                                                    {1000, 1.0}};
        bool same = lane.points.size() == expected.size();
        for (std::size_t i = 0; same && i < expected.size(); ++i)
            same = lane.points[i].at == expected[i].at &&
                   near(lane.points[i].value, expected[i].value);
        check(same, "the pass replaces the range with jumps where it meets the old envelope");
        check(near(*lane.value_at(300), 0.3) && near(*lane.value_at(800), 0.8),
              "outside the pass the old ramp is unchanged");
        check(near(*lane.value_at(450), 0.8125), "inside the pass the lane follows it");
        check(lane.well_formed(), "the written lane is well formed");

        AutomationLane seamless;
        seamless.points = {{0, 0.0}, {1000, 1.0}};
        const std::vector<AutomationPoint> meeting{{400, 0.4}, {600, 0.6}};
        check(seamless.write_pass(400, 600, meeting), "a pass that meets the envelope");
        check(seamless.points.size() == 4 && seamless.points[1].at == 400 &&
                  seamless.points[2].at == 600,
              "a pass that meets the envelope leaves no jumps");

        AutomationLane empty;
        check(empty.write_pass(0, 960, pass), "a pass into an empty lane");
        check(empty.points == pass, "an empty lane becomes the pass");

        AutomationLane untouched;
        untouched.points = {{0, 0.0}};
        const std::vector<AutomationPoint> outside{{700, 1.0}};
        const std::vector<AutomationPoint> backwards{{500, 1.0}, {450, 1.0}};
        check(!untouched.write_pass(400, 600, outside) &&
                  !untouched.write_pass(400, 600, backwards) &&
                  !untouched.write_pass(600, 400, pass) && !untouched.write_pass(0, 10, {}) &&
                  untouched.points.size() == 1,
              "a pass outside its range, out of order, reversed or empty changes nothing");
    }

    // Scenario: Thinning keeps the shape of the envelope.
    {
        AutomationLane line;
        line.points = ramp(0, 960, 10, [](Tick at) { return static_cast<double>(at) / 960.0; });
        line.thin(1e-9);
        check(line.points.size() == 2 && line.points.front().at == 0 &&
                  line.points.back().at == 960,
              "a straight ramp thins to its two ends");

        AutomationLane triangle;
        triangle.points = ramp(0, 960, 10, [](Tick at) {
            return at <= 480 ? static_cast<double>(at) / 480.0
                             : static_cast<double>(960 - at) / 480.0;
        });
        triangle.thin(1e-9);
        check(triangle.points.size() == 3 && triangle.points[1].at == 480,
              "a triangle keeps its peak");

        AutomationLane jumpy;
        jumpy.points = ramp(0, 470, 10, [](Tick) { return 0.25; });
        jumpy.points.push_back({480, 0.25});
        jumpy.points.push_back({480, 0.75});
        for (auto& point : ramp(490, 960, 10, [](Tick) { return 0.75; })) jumpy.points.push_back(point);
        jumpy.thin(1e-9);
        check(jumpy.points.size() == 4 && jumpy.points[1].at == 480 &&
                  jumpy.points[2].at == 480 && near(jumpy.points[2].value, 0.75),
              "a jump survives thinning");

        const auto wave = [](Tick at) { return std::sin(static_cast<double>(at) / 150.0); };
        AutomationLane dense;
        dense.points = ramp(0, 3840, 5, wave);
        const auto original = dense.points;
        const double tolerance = 0.01;
        dense.thin(tolerance);
        check(dense.well_formed(), "a thinned lane is well formed");
        check(dense.points.size() < original.size() / 4,
              "thinning drops most of a dense recording (kept " +
                  std::to_string(dense.points.size()) + " of " +
                  std::to_string(original.size()) + ")");
        bool within = true;
        for (const auto& point : original)
            within = within && std::abs(*dense.value_at(point.at) - point.value) <= tolerance + 1e-12;
        check(within, "every dropped point stays within the tolerance");
    }

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "program schema: all scenarios passed\n";
    return 0;
}
