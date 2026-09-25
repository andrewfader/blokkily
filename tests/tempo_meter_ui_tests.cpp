// Executable scenarios for the model side of features/tempo_meter.feature
// (plan item 2.1): a pattern whose length is not sixteen steps. The LEN
// spinner changes a pattern's length through Pattern::with_length, and a
// pattern as long as a 7/8 bar is fourteen steps that play back to back with
// the bars. The audio claim is read off the production render callback (the
// deterministic pump) playing the real CLAP fixture, which sounds DC while a
// note is held, so every note is an edge.

#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string list(const std::vector<long long>& values) {
    std::ostringstream out;
    out << '{';
    for (std::size_t index = 0; index < values.size(); ++index)
        out << (index ? ", " : "") << values[index];
    out << '}';
    return out.str();
}

Trigger note_at(int step, int key) {
    Trigger trigger;
    trigger.start = step * 120;
    trigger.duration = 96;
    trigger.musical_data = Note{static_cast<std::int16_t>(key), 0.9F, 0.0F};
    return trigger;
}

// Scenario: shortening a pattern drops the steps past its new end and keeps
// everything else, identifiers included.
void pattern_with_length() {
    Pattern pattern(1920, 480);
    auto step0 = note_at(0, 36);
    step0.micro_offset = 7;
    step0.locks = {{"level", 0, 0.75, ParameterLock::Kind::automation}};
    const auto first = pattern.add(step0);
    const auto thirteenth = pattern.add(note_at(13, 40));
    const auto last = pattern.add(note_at(15, 43));

    const auto shorter = pattern.with_length(14 * 120);
    require(shorter.length() == 1680 && shorter.ticks_per_beat() == 480, "fourteen steps long");
    require(shorter.events().size() == 2, "the step past the end is dropped");
    const auto* kept = shorter.find(first);
    require(kept != nullptr && kept->micro_offset == 7 && kept->locks.size() == 1 &&
                std::get<Note>(kept->musical_data).key == 36,
            "a kept step keeps its id and everything it carries");
    require(shorter.find(thirteenth) != nullptr && shorter.find(last) == nullptr,
            "step 13 fits in fourteen steps, step 15 does not");

    // An id a dropped step had is never handed out again.
    auto grown = shorter.with_length(1920);
    const auto next = grown.add(note_at(15, 50));
    require(next != last && next > last, "a dropped step's id is not reused");
    require(grown.length() == 1920 && grown.events().size() == 3, "lengthened again");

    // A step that no longer fits is refused rather than kept outside.
    bool refused = false;
    try {
        auto tight = shorter;
        (void)tight.add(note_at(14, 60));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a fourteen-step pattern has no step 14");
}

// Scenario: a fourteen-step pattern is saved and loaded at its length.
void pattern_length_saved() {
    Project project;
    project.song.patterns = {{"SEVEN", Pattern(1680, 480)}};
    (void)project.song.patterns[0].pattern.add(note_at(13, 60));
    project.song.meter.set({1, 7, 8});
    project.song.clips = {{0, 0, 1920, 2}};
    const auto text = ProjectFile::serialize(project);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project loads: " + error);
    require(loaded->song.patterns.at(0).pattern.length() == 1680 &&
                loaded->song.patterns.at(0).pattern.events().size() == 1 &&
                loaded->song.patterns.at(0).pattern.events()[0].start == 1560,
            "the pattern comes back fourteen steps long with its last step");
    require(ProjectFile::serialize(*loaded) == text, "and saves byte-identically");
}

std::vector<long long> onsets(const std::vector<float>& samples) {
    std::vector<long long> found;
    bool below = true;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const bool now_below = std::abs(samples[index]) <= 1e-4F;
        if (below && !now_below) found.push_back(static_cast<long long>(index));
        below = now_below;
    }
    return found;
}

// Scenario: a pattern as long as a 7/8 bar repeats with the bars.
void fourteen_steps_in_seven_eight() {
    Song song;
    Pattern seven(1680, 480);
    (void)seven.add(note_at(0, 60));
    (void)seven.add(note_at(13, 62));
    song.patterns = {{"SEVEN", std::move(seven)}};
    song.tracks = {Track{}};
    song.tracks[0].mix.pan = -1.0;
    song.meter.set({1, 7, 8});
    // Three passes from the start of bar 2.
    song.clips = {{0, 0, song.meter.bar_start(1), 3}};

    SongEngine engine;
    std::string error;
    auto clap = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(clap != nullptr, "the CLAP fixture loads: " + error);
    engine.set_instrument(0, std::move(clap));
    require(engine.prepare(song, 48000.0, 512, 0, &error), "prepare: " + error);
    engine.set_playing(true);
    RtAudioOutput output(RtAudioOutput::Mode::deterministic);
    require(output.open(engine) && output.start(), "the production output starts");
    std::vector<float> left;
    std::vector<float> stereo(2048);
    while (left.size() < engine.song_samples()) {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        require(output.pump(stereo), "the production callback renders");
        left.insert(left.end(), stereo.begin(), stereo.begin() + 1024);
    }
    left.resize(engine.song_samples());
    // 50 samples a tick at 120 BPM. Bars 2 to 4 start at ticks 1920, 3600 and
    // 5280; step 13 of each is 1560 ticks in.
    std::vector<long long> expected;
    for (const long long bar : {1920LL, 3600LL, 5280LL}) {
        expected.push_back(bar * 50);
        expected.push_back((bar + 1560) * 50);
    }
    const auto found = onsets(left);
    require(engine.song_samples() == 6960 * 50, "the song ends with bar 4");
    require(found == expected, "notes sound at " + list(found) + ", wanted " + list(expected));
}

}  // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"pattern_with_length", pattern_with_length},
        {"pattern_length_saved", pattern_length_saved},
        {"fourteen_steps_in_seven_eight", fourteen_steps_in_seven_eight},
    };
    if (argc != 2 || cases.find(argv[1]) == cases.end()) {
        std::cerr << "usage: blokkily_tempo_meter_ui_tests <case>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& failure) {
        std::cerr << "FAIL " << argv[1] << ": " << failure.what() << '\n';
        return 1;
    }
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
}
