// Executable scenarios for features/record_everything.feature, part 1 (plan
// item 2.5): multi-track arm, MIDI channel routing, and the on-screen perform
// queue. Each case is its own CTest test, record_<case>.
//
// Every audio claim is read off what the production render callback wrote,
// through the deterministic pump: two tracks, each playing the real CLAP
// fixture through the production adapter, one panned hard left and one hard
// right, so the left channel's energy is track 0 and the right channel's is
// track 1. A route that reaches the wrong track is heard on the wrong side.

#include "support/audio_probe.hpp"

#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/midi/input_routes.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/project.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

using Bytes = std::array<std::uint8_t, 3>;

// What the two sides of the bus carried over some frames.
struct Heard {
    double left = 0.0;
    double right = 0.0;
};

std::string describe(const Heard& heard) {
    std::ostringstream out;
    out << "left rms " << heard.left << ", right rms " << heard.right;
    return out.str();
}

// The fixture holds 0.25 while a key is down; hard-panned, that side of the
// bus carries 0.25 and the other nothing.
constexpr double sounding = 0.2;
constexpr double silent = 1e-6;

bool only_left(const Heard& heard) { return heard.left > sounding && heard.right < silent; }
bool only_right(const Heard& heard) { return heard.left < silent && heard.right > sounding; }
bool both(const Heard& heard) { return heard.left > sounding && heard.right > sounding; }
bool neither(const Heard& heard) { return heard.left < silent && heard.right < silent; }

// Two CLAP tracks on one engine, played by a deterministic MIDI input and
// rendered through the production callback.
class Rig {
public:
    explicit Rig(std::size_t tracks = 2) {
        Pattern pattern(1920, 480);
        song.patterns = {{"Take", std::move(pattern)}};
        song.tracks.resize(tracks);
        for (std::size_t track = 0; track < tracks; ++track) {
            song.tracks[track].name = "Track " + std::to_string(track);
            song.tracks[track].mix.pan = track % 2 == 0 ? -1.0 : 1.0;
            song.clips.push_back({track, 0, 0, 1});
        }
        std::string error;
        for (std::size_t track = 0; track < tracks; ++track) {
            engine.set_instrument(track, ClapPluginInstance::create(
                                             BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error));
            require(engine.has_instrument(track),
                    "the CLAP fixture must load through the production adapter: " + error);
        }
        require(engine.prepare(song, 48000.0, 1024, 0, &error), "prepare: " + error);
        require(keyboard.open(std::size_t{0}, &error), "open the deterministic input: " + error);
        engine.connect_input(&keyboard.queue());
        require(output.open(engine) && output.start(), "the production output must start");
        route();
    }

    // What the application does whenever arm, channel or selection change.
    void route() { keyboard.set_routes(midi_routes(song, selected)); }

    void arm(std::size_t track, bool armed, int channel = -1) {
        song.tracks.at(track).input.armed = armed;
        song.tracks.at(track).input.midi_channel = static_cast<std::int8_t>(channel);
        route();
    }

    void send(std::uint8_t status, int key, int velocity) {
        const Bytes message{status, static_cast<std::uint8_t>(key),
                            static_cast<std::uint8_t>(velocity)};
        require(keyboard.inject(message), "the deterministic input takes the message");
    }

    // The next `frames` frames of the bus, through the production callback.
    Heard render(std::size_t frames = 2048) {
        std::vector<float> left;
        std::vector<float> right;
        std::vector<float> stereo;
        while (left.size() < frames) {
            const std::size_t now = std::min<std::size_t>(1024, frames - left.size());
            stereo.assign(now * 2, -1.0F);
            require(output.pump(stereo), "the production callback must render");
            left.insert(left.end(), stereo.begin(), stereo.begin() + static_cast<long>(now));
            right.insert(right.end(), stereo.begin() + static_cast<long>(now), stereo.end());
        }
        return {probe::rms(left), probe::rms(right)};
    }

    Song song;
    std::size_t selected = 0;
    MidiInput keyboard{MidiInput::Mode::deterministic};
    SongEngine engine;
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
};

// Scenario: One key reaches every armed track. With nothing armed the
// selected track has it; with tracks armed, each armed track, and only those.
void routes_case() {
    Song song;
    song.tracks.resize(70);
    // Nothing armed: the selected track, on every channel (decision 4).
    auto routes = midi_routes(song, 3);
    for (const auto mask : routes) require(mask == track_bit(3), "nothing armed plays track 3");
    require(surface_routes(song, 3) == track_bit(3), "the surfaces play the selected track");

    // Armed tracks, and only they, whatever is selected.
    song.tracks[1].input.armed = true;
    song.tracks[5].input.armed = true;
    song.tracks[5].input.midi_channel = 9;
    routes = midi_routes(song, 3);
    for (std::size_t channel = 0; channel < 16; ++channel) {
        const TrackMask want = track_bit(1) | (channel == 9 ? track_bit(5) : 0);
        require(routes[channel] == want, "channel " + std::to_string(channel) + " routes");
    }
    // The surfaces have no channel: every armed track.
    require(surface_routes(song, 3) == (track_bit(1) | track_bit(5)), "surfaces reach both");

    // A track armed for audio only takes no notes; neither does a track past
    // the sixty-four an input can reach.
    song.tracks[1].input.source = TrackInput::Source::audio;
    song.tracks[5].input.armed = false;
    song.tracks[69].input.armed = true;
    require(!armed_for_notes(song, 1) && !armed_for_notes(song, 69), "not armed for notes");
    for (const auto mask : midi_routes(song, 2))
        require(mask == track_bit(2), "with no track armed for notes, the selected track");
    song.tracks[1].input.source = TrackInput::Source::midi_and_audio;
    require(armed_for_notes(song, 1), "MIDI and audio takes notes");
}

// Scenario: Two armed tracks both sound a key played once.
void armed_tracks_case() {
    Rig rig;
    rig.arm(0, true);
    rig.arm(1, true);
    require(neither(rig.render()), "silence before the key");
    rig.send(0x90, 60, 100);
    const auto held = rig.render();
    require(both(held), "both armed tracks sound the key: " + describe(held));
    rig.send(0x80, 60, 0);
    (void)rig.render(1024);
    const auto released = rig.render();
    require(neither(released), "the release reaches both: " + describe(released));

    // Disarming track 0 leaves the key on track 1 alone.
    rig.arm(0, false);
    rig.send(0x90, 62, 100);
    const auto one = rig.render();
    require(only_right(one), "only the armed track sounds: " + describe(one));
    rig.send(0x80, 62, 0);
    (void)rig.render(1024);
    require(neither(rig.render()), "and is released");
}

// Scenario: A track hears only its own MIDI channel.
void channel_masks_case() {
    Rig rig;
    rig.arm(0, true, 0);   // channel 1
    rig.arm(1, true, 1);   // channel 2
    const auto routes = rig.keyboard.routes();
    require(routes[0] == track_bit(0) && routes[1] == track_bit(1) && routes[2] == 0,
            "the sixteen masks say channel 1 -> track 0, channel 2 -> track 1");

    rig.send(0x90, 60, 100);   // channel 1
    const auto first = rig.render();
    require(only_left(first), "channel 1 is heard on track 0 alone: " + describe(first));
    rig.send(0x80, 60, 0);
    (void)rig.render(1024);

    rig.send(0x91, 60, 100);   // channel 2, the same key
    const auto second = rig.render();
    require(only_right(second), "channel 2 is heard on track 1 alone: " + describe(second));
    rig.send(0x81, 60, 0);
    (void)rig.render(1024);
    require(neither(rig.render()), "released");

    rig.send(0x92, 60, 100);   // channel 3 reaches no armed track
    require(neither(rig.render()), "channel 3 is not heard");
    rig.send(0x82, 60, 0);

    // The same key held on two channels is two keys: releasing one leaves the
    // other sounding.
    rig.send(0x90, 64, 100);
    rig.send(0x91, 64, 100);
    require(both(rig.render()), "both channels hold the key");
    rig.send(0x80, 64, 0);
    (void)rig.render(1024);
    const auto one_left = rig.render();
    require(only_right(one_left), "channel 2's key still sounds: " + describe(one_left));
    rig.send(0x81, 64, 0);
}

// Scenario: A key's release follows the tracks it went down on.
void release_follows_note_on_case() {
    Rig rig;
    rig.arm(0, true);
    rig.send(0x90, 60, 100);
    require(only_left(rig.render()), "the key sounds on track 0");
    // Mid-note, track 0 is disarmed and track 1 armed. The release still
    // reaches track 0, and track 1, which never heard the key, stays silent.
    rig.arm(0, false);
    rig.arm(1, true);
    rig.send(0x80, 60, 0);
    (void)rig.render(1024);
    const auto after = rig.render();
    require(neither(after), "the release reached the track the key went down on: " +
                                describe(after));

    // The same when a track's channel changes under a held key.
    rig.arm(1, true, 0);
    rig.send(0x90, 67, 100);
    require(only_right(rig.render()), "channel 1 plays track 1");
    rig.arm(1, true, 5);
    rig.send(0x80, 67, 0);
    (void)rig.render(1024);
    require(neither(rig.render()), "re-channelling does not strand the key");
}

// Scenario: A release the queue has no room for is owed, not lost.
void pending_release_case() {
    Rig rig;
    rig.arm(0, true);
    rig.arm(1, true);
    rig.send(0x90, 60, 100);
    require(both(rig.render()), "the key sounds on both tracks");

    // The queue is filled while the render callback is not draining it, with
    // events that change nothing: the fixture's level set to what it is.
    std::size_t filler = 0;
    while (rig.keyboard.queue().push({0, {PluginEvent::Type::parameter_value, 0, 0, 0.25, 0.0}}))
        ++filler;
    require(filler > 0, "the queue fills");
    rig.send(0x80, 60, 0);
    require(rig.keyboard.release_pending(), "the release is owed");
    // The callback drains the filler; the owed release has not been sent.
    (void)rig.render(1024);
    const auto still = rig.render();
    require(both(still), "the key still sounds until its release is sent: " + describe(still));
    // The next message the port delivers - here a controller the input does
    // not play - sends what is owed first.
    rig.send(0xB0, 1, 64);
    require(!rig.keyboard.release_pending(), "the owed release is sent");
    (void)rig.render(1024);
    const auto released = rig.render();
    require(neither(released), "both tracks are released: " + describe(released));
}

// Scenario: With nothing armed, the selected track plays what is played.
void nothing_armed_case() {
    Rig rig;
    rig.selected = 1;
    rig.route();
    rig.send(0x90, 60, 100);
    const auto first = rig.render();
    require(only_right(first), "the selected track 1 plays: " + describe(first));
    rig.send(0x80, 60, 0);
    (void)rig.render(1024);
    rig.selected = 0;
    rig.route();
    rig.send(0x95, 60, 100);   // any channel
    const auto second = rig.render();
    require(only_left(second), "the selected track 0 plays: " + describe(second));
    rig.send(0x85, 60, 0);
    (void)rig.render(1024);
    // Arming track 1 takes the keyboard off the selected track.
    rig.arm(1, true);
    rig.send(0x90, 62, 100);
    const auto armed = rig.render();
    require(only_right(armed), "the armed track plays, not the selected one: " + describe(armed));
    rig.send(0x80, 62, 0);
    // A track armed for audio only does not count: the selected track plays.
    rig.song.tracks[1].input.source = TrackInput::Source::audio;
    rig.route();
    (void)rig.render(1024);
    rig.send(0x90, 64, 100);
    const auto audio = rig.render();
    require(only_left(audio), "armed for audio only, the selected track plays: " +
                                  describe(audio));
    rig.send(0x80, 64, 0);
}

// Scenario: What the on-screen surfaces play is heard and recorded like a
// keyboard's.
void perform_queue_case() {
    Rig rig;
    rig.arm(0, true);
    rig.arm(1, true);
    // On a stopped song a performed note is heard, and nothing is recorded.
    rig.engine.set_recording(true);
    const auto targets = surface_routes(rig.song, rig.selected);
    require(targets == (track_bit(0) | track_bit(1)), "the surfaces reach both armed tracks");
    for (std::size_t track = 0; track < 2; ++track)
        require(rig.engine.perform(track, {PluginEvent::Type::note_on, 0, 60, 0.9, 0.0}),
                "perform a key");
    const auto stopped = rig.render();
    require(both(stopped), "a performed key sounds on a stopped song: " + describe(stopped));
    for (std::size_t track = 0; track < 2; ++track)
        require(rig.engine.perform(track, {PluginEvent::Type::note_off, 0, 60, 0.0, 0.0}),
                "release it");
    (void)rig.render(1024);
    require(neither(rig.render()), "released");
    CapturedEvent captured;
    require(!rig.engine.take_captured(captured), "nothing recorded while stopped");

    // Playing and recording: the key is captured once per track it reached,
    // at the sample of the block that sounded it, beside a MIDI key played
    // into the same block.
    require(!rig.engine.perform(9, {PluginEvent::Type::note_on, 0, 60, 0.9, 0.0}),
            "a track that does not exist is refused");
    rig.engine.set_playing(true);
    (void)rig.render(3 * 1024);
    const auto at = rig.engine.sample_position();
    for (std::size_t track = 0; track < 2; ++track)
        require(rig.engine.perform(track, {PluginEvent::Type::note_on, 0, 64, 0.7, 0.0}),
                "perform while recording");
    rig.send(0x90, 67, 100);
    const auto held = rig.render(1024);
    require(both(held), "heard on both tracks: " + describe(held));
    std::vector<CapturedEvent> heard;
    while (rig.engine.take_captured(captured)) heard.push_back(captured);
    require(heard.size() == 4, "a MIDI key and a performed key, each on two tracks: " +
                                   std::to_string(heard.size()));
    bool performed[2] = {false, false};
    bool played[2] = {false, false};
    for (const auto& event : heard) {
        require(event.sample == at, "captured at the block that sounded it");
        require(event.tick == tick_at_sample(rig.engine.published_clock(), at),
                "stamped with the tick it was heard at");
        require(event.track < 2 && event.event.type == PluginEvent::Type::note_on, "note-ons");
        if (event.event.key_or_parameter == 64 && event.event.value == 0.7)
            performed[event.track] = true;
        if (event.event.key_or_parameter == 67) played[event.track] = true;
    }
    require(performed[0] && performed[1] && played[0] && played[1],
            "each track captured both keys");
    for (std::size_t track = 0; track < 2; ++track)
        require(rig.engine.perform(track, {PluginEvent::Type::note_off, 0, 64, 0.0, 0.0}),
                "release the performed key");
    rig.send(0x80, 67, 0);
    (void)rig.render(1024);
    std::size_t releases = 0;
    while (rig.engine.take_captured(captured))
        releases += captured.event.type == PluginEvent::Type::note_off ? 1 : 0;
    require(releases == 4, "every release is captured on the track it reached");
}

// Scenario: Arm and channel are saved with the project.
void project_case() {
    Project project;
    project.song.patterns = {{"Take", Pattern(1920, 480)}};
    project.song.tracks.resize(3);
    project.song.clips = {{0, 0, 0, 1}};
    project.song.tracks[1].input.armed = true;
    project.song.tracks[1].input.midi_channel = 2;
    project.song.tracks[2].input.armed = true;
    const auto text = ProjectFile::serialize(project);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "parse: " + error);
    for (std::size_t track = 0; track < 3; ++track)
        require(loaded->song.tracks[track].input == project.song.tracks[track].input,
                "track " + std::to_string(track) + " keeps its arm and channel");
    require(midi_routes(loaded->song, 0) == midi_routes(project.song, 0),
            "the loaded song routes the keyboard the same way");
    require(ProjectFile::serialize(*loaded) == text, "byte-identical round trip");
}

// Scenario: Sustain pedal (CC 64) holds notes until the pedal is released.
void sustain_pedal_case() {
    Rig rig;
    rig.arm(0, true);
    require(!rig.keyboard.is_sustain_active(0), "sustain initially inactive");

    // 1. Play note 60 down on track 0.
    rig.send(0x90, 60, 100);
    const auto sounding = rig.render();
    require(only_left(sounding), "key down sounds: " + describe(sounding));

    // 2. Press sustain pedal down (CC 64 = 127).
    rig.send(0xB0, 64, 127);
    require(rig.keyboard.is_sustain_active(0), "sustain pedal is active");

    // 3. Release key 60 physically (finger up).
    rig.send(0x80, 60, 0);

    // 4. Note must STILL be sounding through the synth!
    const auto sustained = rig.render();
    require(only_left(sustained), "key sustained by pedal after physical release: " + describe(sustained));

    // 5. Release sustain pedal (CC 64 = 0).
    rig.send(0xB0, 64, 0);
    require(!rig.keyboard.is_sustain_active(0), "sustain pedal released");

    // 6. Now the note must be released and silent.
    (void)rig.render(1024);
    const auto stopped = rig.render();
    require(neither(stopped), "releasing sustain pedal releases the sustained note: " + describe(stopped));

    // 7. Test re-striking while sustained: note on, pedal down, note off, note on again.
    rig.send(0x90, 62, 100);
    rig.send(0xB0, 64, 127);
    rig.send(0x80, 62, 0);
    rig.send(0x90, 62, 110);
    const auto restruck = rig.render();
    require(only_left(restruck), "restruck key sounds: " + describe(restruck));
    rig.send(0xB0, 64, 0);
    const auto still_held = rig.render();
    require(only_left(still_held), "key still physically held continues sounding: " + describe(still_held));
    rig.send(0x80, 62, 0);
    (void)rig.render(1024);
    require(neither(rig.render()), "key release silences the note");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const std::string name = argv[1];
        if (name == "routes") routes_case();
        else if (name == "armed_tracks") armed_tracks_case();
        else if (name == "channel_masks") channel_masks_case();
        else if (name == "release_follows_note_on") release_follows_note_on_case();
        else if (name == "pending_release") pending_release_case();
        else if (name == "nothing_armed") nothing_armed_case();
        else if (name == "perform_queue") perform_queue_case();
        else if (name == "project") project_case();
        else if (name == "sustain_pedal") sustain_pedal_case();
        else throw std::runtime_error("unknown case " + name);
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
