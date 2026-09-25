// Plays a note into Blokkily through the system's real MIDI layer. Every other
// MIDI test injects bytes into a deterministic input, which proves the decode,
// the tuning, the routing, and the recording, but never that a port opened by
// name actually delivers what a keyboard sends. This opens a virtual output
// port the way a controller's driver would, connects the production MidiInput
// to it through the MIDI server, and fails unless the notes sent there are
// heard through the engine and captured into a take.
//
// The audio side is the deterministic pump, so no sound hardware is needed.
// Exits 77 when the machine has no MIDI server to talk to, which CTest reads
// as a skip.

#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <RtMidi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int exit_skip = 77;   // CTest's SKIP_RETURN_CODE

int fail(const char* why) {
    std::fprintf(stderr, "MIDI DEVICE FAIL: %s\n", why);
    return 1;
}

// The port thread delivers on its own schedule, so arrival is waited for, but
// never for longer than a generous bound.
template <typename Predicate>
bool wait_for(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string clap_path;
    for (int index = 1; index + 1 < argc; ++index)
        if (std::string(argv[index]) == "--clap-fixture") clap_path = argv[index + 1];
    if (clap_path.empty()) return fail("pass --clap-fixture <path>");

    // The keyboard: a virtual port, which is what a controller's driver makes.
    std::unique_ptr<RtMidiOut> keyboard;
    try {
        keyboard = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "Test Controller");
        keyboard->openVirtualPort("Test Keyboard");
    } catch (const RtMidiError& error) {
        std::printf("SKIP: no MIDI server (%s)\n", error.getMessage().c_str());
        return exit_skip;
    }

    blokkily::MidiInput input(blokkily::MidiInput::Mode::device);
    const auto ports = input.ports();
    if (std::none_of(ports.begin(), ports.end(), [](const std::string& port) {
            return port.find("Test Keyboard") != std::string::npos;
        }))
        return fail("the virtual keyboard is not among the ports the input lists");
    std::string error;
    if (!input.open(std::string("Test Keyboard"), &error)) return fail(error.c_str());
    if (!input.is_open() || input.port_name().find("Test Keyboard") == std::string::npos)
        return fail("the input did not open the port it was asked for");

    blokkily::Song song;
    blokkily::SongEngine engine;
    engine.set_instrument(0, blokkily::ClapPluginInstance::create(
                                 clap_path, "dev.blokkily.test", &error));
    if (!engine.has_instrument(0)) return fail("the CLAP fixture did not load");
    if (!engine.prepare(song, 120.0, 48000.0, 256, 0, &error)) return fail(error.c_str());
    engine.connect_input(&input.queue());
    input.set_track(0);

    blokkily::RtAudioOutput output(blokkily::RtAudioOutput::Mode::deterministic);
    if (!output.open(engine, 48000, 256, &error) || !output.start(&error))
        return fail(error.c_str());
    std::vector<float> block(512, 0.0F);
    const auto heard = [&] {
        std::fill(block.begin(), block.end(), 0.0F);
        if (!output.pump(block)) return -1.0F;
        return *std::max_element(block.begin(), block.end());
    };
    if (heard() != 0.0F) return fail("the engine sounded before a key was pressed");

    // A key goes down on the controller and is heard through the callback.
    std::vector<unsigned char> message{0x90, 60, 100};
    keyboard->sendMessage(&message);
    if (!wait_for([&] { return input.notes_received() == 1; }))
        return fail("a note sent to the port never reached the input");
    if (heard() <= 0.1F) return fail("the note reached the input but was not heard");
    message = {0x80, 60, 0};
    keyboard->sendMessage(&message);
    if (!wait_for([&] { return heard() == 0.0F; }))
        return fail("releasing the key did not silence the instrument");

    // With the song playing and armed, what is played is captured.
    engine.set_playing(true);
    engine.set_recording(true);
    (void)heard();
    message = {0x90, 64, 127};
    keyboard->sendMessage(&message);
    if (!wait_for([&] { return heard() > 0.1F; })) return fail("the recorded note was not heard");
    message = {0x80, 64, 0};
    keyboard->sendMessage(&message);
    if (!wait_for([&] { return heard() == 0.0F; })) return fail("the recorded note did not end");
    blokkily::CapturedEvent captured;
    if (!engine.take_captured(captured) ||
        captured.event.type != blokkily::PluginEvent::Type::note_on ||
        captured.event.key_or_parameter != 64 || captured.event.value != 1.0)
        return fail("the key-down was not captured");
    const auto pressed = captured.sample;
    if (!engine.take_captured(captured) ||
        captured.event.type != blokkily::PluginEvent::Type::note_off ||
        captured.sample <= pressed)
        return fail("the key-up was not captured after the key-down");

    output.stop();
    const auto port = input.port_name();
    input.close();
    std::printf("MIDI DEVICE PASS | port=%s | pressed at %llu, released at %llu\n",
                port.c_str(), static_cast<unsigned long long>(pressed),
                static_cast<unsigned long long>(captured.sample));
    return 0;
}
