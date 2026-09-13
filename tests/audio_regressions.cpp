#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace blokkily;

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::unique_ptr<PluginInstance> instrument(bool vst = false) {
    auto result = vst ? std::unique_ptr<PluginInstance>(Vst3PluginInstance::create(
                           BLOKKILY_TEST_VST3_PATH, 0))
                      : std::unique_ptr<PluginInstance>(ClapPluginInstance::create(
                           BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test"));
    require(result != nullptr, "fixture must load through the production adapter");
    return result;
}

Song song_with(Pattern pattern) {
    Song song;
    song.patterns = {{"Regression", std::move(pattern)}};
    song.tracks = {{"Instrument", {}, {}}};
    song.tracks[0].mix.pan = -1.0;
    song.clips = {{0, 0, 0, 1}};
    return song;
}

void add_note(Pattern& pattern, Tick start, Tick duration) {
    Trigger note;
    note.start = start;
    note.duration = duration;
    note.musical_data = Note{60, 1.0F, 0.0F};
    (void)pattern.add(note);
}

std::vector<float> pump(AudioSource& source, std::size_t frames) {
    RtAudioOutput output(RtAudioOutput::Mode::deterministic);
    require(output.open(source) && output.start(), "production output must start");
    std::vector<float> stereo(frames * 2, -1.0F);
    require(output.pump(stereo), "production callback must render");
    return stereo;
}

// features/audio_reliability.feature: Live input keeps its sample order.
void live_order() {
    Pattern pattern(2048, 24000); // one sample per tick at 48 kHz / 120 BPM
    add_note(pattern, 100, 100);
    auto song = song_with(std::move(pattern));
    SongEngine engine;
    engine.set_instrument(0, instrument());
    require(engine.prepare(song, 120, 48000, 512), "prepare song");
    engine.set_playing(true);
    require(engine.play_live(0, {PluginEvent::Type::note_on, 0, 60, 1.0}), "queue live note");
    const auto audio = pump(engine, 512);
    require(audio[0] == 0.25F, "live note must sound at sample zero, before the scheduled note");
    require(audio[199] == 0.25F && audio[200] == 0.0F,
            "scheduled release must retain its exact sample offset");
}

// features/audio_reliability.feature: Dense patterns keep every note and release.
void dense(bool preview) {
    Pattern pattern(2048, 24000);
    for (Tick start = 0; start < 900; start += 3) add_note(pattern, start, 1);
    auto song = song_with(std::move(pattern));
    SongEngine engine;
    RealtimePlayback player(instrument());
    if (preview) {
        require(player.prepare(song.pattern(), 120, 48000, 1024), "prepare preview");
        player.set_playing(true);
    } else {
        engine.set_instrument(0, instrument());
        require(engine.prepare(song, 120, 48000, 1024), "prepare song");
        engine.set_playing(true);
    }
    const auto audio = pump(preview ? static_cast<AudioSource&>(player)
                                    : static_cast<AudioSource&>(engine), 1024);
    for (std::size_t sample = 0; sample < 1024; ++sample) {
        const float expected = sample < 900 && sample % 3 == 0 ? 0.25F : 0.0F;
        require(audio[sample] == expected, "dense playback dropped or shifted a note or release");
    }
}

// features/audio_reliability.feature: Preview accepts a larger device callback.
void large_preview() {
    Pattern pattern(2048, 24000);
    add_note(pattern, 0, 1500);
    RealtimePlayback player(instrument(true));
    require(player.prepare(pattern, 120, 48000, 128), "prepare VST3 preview");
    player.set_playing(true);
    const auto audio = pump(player, 1024);
    require(std::all_of(audio.begin(), audio.end(), [](float sample) { return sample == 0.25F; }),
            "device quantum larger than prepared block must still sound through VST3");
}

// features/audio_reliability.feature: Invalid timing is refused before activation.
void invalid_timing() {
    auto song = song_with(Pattern{});
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(), 0.0, -1.0}) {
        SongEngine engine;
        require(!engine.prepare(song, invalid, 48000, 512), "invalid tempo must be refused");
        require(!engine.prepare(song, 120, invalid, 512), "invalid sample rate must be refused");
        RealtimePlayback player(instrument());
        require(!player.prepare(song.pattern(), invalid, 48000, 512), "invalid preview tempo");
        require(!player.prepare(song.pattern(), 120, invalid, 512), "invalid preview sample rate");
    }
}

// features/audio_reliability.feature: Unsupported simultaneous density is explicit.
void density_limit() {
    Pattern pattern(2048, 24000);
    for (int count = 0; count < 256; ++count) add_note(pattern, 0, 100);
    auto song = song_with(pattern);
    SongEngine engine;
    engine.set_instrument(0, instrument());
    require(engine.prepare(song, 120, 48000, 512), "256 simultaneous events must fit");
    engine.set_playing(true);
    auto audio = pump(engine, 128);
    require(audio[0] == 0.25F && audio[100] == 0.0F, "boundary density must release correctly");
    add_note(pattern, 0, 100);
    std::string error;
    require(!engine.recompile(song_with(pattern), 120, 0, &error) && !error.empty(),
            "excess simultaneous events must produce an explicit error");
    engine.rewind();
    audio = pump(engine, 128);
    require(audio[0] == 0.25F && audio[100] == 0.0F,
            "failed recompile must preserve the playable arrangement");
    RealtimePlayback preview(instrument());
    require(preview.prepare(song.pattern(), 120, 48000, 512), "prepare boundary preview");
    require(!preview.prepare(pattern, 120, 48000, 512), "preview must refuse excess density");
    preview.set_playing(true);
    audio = pump(preview, 128);
    require(audio[0] == 0.25F && audio[100] == 0.0F,
            "failed preparation must not replace the playable preview");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a regression name");
        const std::string test = argv[1];
        if (test == "live_order") live_order();
        else if (test == "dense_song") dense(false);
        else if (test == "dense_preview") dense(true);
        else if (test == "large_preview") large_preview();
        else if (test == "invalid_timing") invalid_timing();
        else if (test == "density_limit") density_limit();
        else throw std::runtime_error("unknown regression");
        std::cout << test << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
