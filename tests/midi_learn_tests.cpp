// features/midi_learn.feature: the MIDI input's controller feed and the
// bindings the song keeps.
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/project/project.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

using namespace blokkily;
namespace {

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

void feed() {
    MidiInput input(MidiInput::Mode::deterministic);
    require(input.open(std::size_t{0}), "the deterministic input opens");
    const auto send = [&](std::uint8_t a, std::uint8_t b, std::uint8_t c) {
        const std::array<std::uint8_t, 3> message{a, b, c};
        require(input.inject(message), "the message is taken");
    };
    send(0xB0, 7, 100);  // volume, channel 1
    send(0xB3, 74, 12);  // cutoff, channel 4
    send(0xB0, 64, 127); // the sustain pedal is not offered
    send(0xB0, 101, 0);  // nor the RPN pair
    send(0xB0, 123, 0);  // nor a channel-mode message
    send(0x90, 60, 100); // a note is not a controller
    ControlChange change;
    require(input.next_control_change(change) && change.channel == 0 && change.controller == 7 &&
                change.value == 100, "CC 7 on channel 1 arrives");
    require(input.next_control_change(change) && change.channel == 3 && change.controller == 74 &&
                change.value == 12, "CC 74 on channel 4 arrives");
    require(!input.next_control_change(change), "nothing else is offered");
    // The feed keeps the latest 256 and never blocks the port.
    for (int i = 0; i < 300; ++i) send(0xB0, 1, static_cast<std::uint8_t>(i % 128));
    int count = 0;
    while (input.next_control_change(change)) ++count;
    require(count == 256, "a full feed drops the newest, never blocks: got " + std::to_string(count));
}

void records() {
    Project project;
    project.song.tracks.resize(3);
    project.song.modulators.push_back(Modulator{Modulator::Kind::macro, "Macro 1"});
    project.song.midi_bindings = {{0, 7, MidiBinding::Target::track_gain, 2},
                                  {3, 74, MidiBinding::Target::macro, 0},
                                  {0, 10, MidiBinding::Target::master_gain, 0}};
    const auto text = ProjectFile::serialize(project);
    require(text.find("midi_learn 1 7 gain 2") != std::string::npos &&
                text.find("midi_learn 4 74 macro 0") != std::string::npos,
            "bindings are saved, channels counted from 1");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), error);
    require(loaded->song.midi_bindings == project.song.midi_bindings, "they load as saved");
    require(ProjectFile::serialize(Project{}).find("midi_learn") == std::string::npos,
            "a song without bindings saves no record");
    auto twice = text + "midi_learn 1 7 pan 0\n";
    require(!ProjectFile::parse(twice, &error).has_value(), "one binding per controller");
    auto missing = text;
    missing.replace(missing.find("gain 2"), 6, "gain 9");
    require(!ProjectFile::parse(missing, &error).has_value(), "a binding to a track that is not there is refused");
}

void track_removal() {
    Song song;
    song.tracks.resize(3);
    song.midi_bindings = {{0, 1, MidiBinding::Target::track_gain, 0},
                          {0, 2, MidiBinding::Target::track_pan, 1},
                          {0, 3, MidiBinding::Target::track_mute, 2},
                          {0, 4, MidiBinding::Target::master_gain, 0}};
    (void)song.remove_track(1);
    require(song.midi_bindings.size() == 3, "the removed track's binding goes");
    require(song.midi_bindings[0].index == 0 && song.midi_bindings[1].index == 1 &&
                song.midi_bindings[1].controller == 3 && song.midi_bindings[2].index == 0,
            "the others follow their tracks; master is untouched");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        const std::string which = argv[1];
        if (which == "feed") feed();
        else if (which == "records") records();
        else if (which == "track_removal") track_removal();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
