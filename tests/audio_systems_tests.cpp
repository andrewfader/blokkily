// features/audio_systems.feature: the audio systems this build can open, and
// a device on one that is missing refused with a reason.
// features/audio_settings.feature: the devices each system offers.
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

// AUTO plays through PipeWire's JACK when a server answers, else PulseAudio
// where it has devices, else what RtAudio chooses; what it resolves to is a
// system this build can open, and an output made on AUTO is on it.
void auto_system() {
    const auto chosen = RtAudioOutput::auto_system();
    std::cout << "AUTO -> " << (chosen.empty() ? "RtAudio's choice" : chosen) << '\n';
    bool known = chosen.empty();
    for (const auto& system : RtAudioOutput::audio_systems()) known = known || system.name == chosen;
    require(known, "AUTO resolves to a system this build can open, or leaves the choice to RtAudio");
    RtAudioOutput output(RtAudioOutput::Mode::device, "");
    require(output.system() == chosen, "an output made on AUTO is on the system AUTO resolves to");
    RtAudioOutput named(RtAudioOutput::Mode::device, "alsa");
    require(named.system() == "alsa", "a system asked for by name is kept");
    if (chosen == "jack")
        require(RtAudioOutput::devices("").empty(), "on JACK the patchbay routes, so AUTO lists no devices");
    else if (chosen == "pulse")
        require(!RtAudioOutput::devices("").empty(), "AUTO on PulseAudio lists its devices");
    RtAudioOutput deterministic(RtAudioOutput::Mode::deterministic, "");
    require(deterministic.system().empty(), "a gate's deterministic device resolves nothing");
}

void devices() {
    require(RtAudioOutput::devices("no-such-system").empty(),
            "a system the build lacks offers no devices");
    // Whatever this machine has: none is fine, but each one listed is named,
    // plays or records, and offers only rates a device can run at.
    for (const auto& system : RtAudioOutput::audio_systems()) {
        for (const auto& device : RtAudioOutput::devices(system.name)) {
            std::cout << system.name << ": " << device.name << " (" << device.outputs << " out, "
                      << device.inputs << " in)\n";
            require(!device.name.empty(), "each device is named");
            require(device.outputs > 0 || device.inputs > 0, "each plays or records");
            for (const auto rate : device.sample_rates)
                require(rate >= 8000 && rate <= 768000, "each rate is one a device runs at");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        const std::string which = argv[1];
        if (which == "listed") listed();
        else if (which == "missing") missing();
        else if (which == "devices") devices();
        else if (which == "auto") auto_system();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
