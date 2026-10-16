// features/audio_systems.feature: the audio systems this build can open, and
// a device on one that is missing refused with a reason.
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"

#include <iostream>
#include <stdexcept>

using namespace blokkily;
namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

void listed() {
    const auto systems = RtAudioOutput::audio_systems();
    require(!systems.empty(), "this build can open at least one audio system");
    for (const auto& system : systems) {
        std::cout << system.name << " (" << system.display << ")\n";
        require(!system.name.empty() && !system.display.empty() && system.name != "dummy",
                "each is named, and RtAudio's silent dummy is not offered");
        // Each can be asked for by name.
        RtAudioOutput output(RtAudioOutput::Mode::device, system.name);
        require(!output.is_open(), "made, not opened");
    }
#if defined(__linux__)
    bool alsa = false;
    for (const auto& system : systems) alsa = alsa || system.name == "alsa";
    require(alsa, "a Linux build offers ALSA");
#endif
}

void missing() {
    RtAudioOutput output(RtAudioOutput::Mode::device, "no-such-system");
    SongEngine engine;
    std::string error;
    require(!output.open(engine, 48000, 256, &error), "a system the build lacks does not open");
    std::cout << error << '\n';
    require(error.find("no-such-system") != std::string::npos && error.find("not available") != std::string::npos,
            "it says which system and why");
    require(!output.start(&error) && !output.is_running(), "nor does it start");
    output.close();
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        const std::string which = argv[1];
        if (which == "listed") listed();
        else if (which == "missing") missing();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
