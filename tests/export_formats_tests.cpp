// features/export_formats.feature: MP3 and Ogg Vorbis bounces, and stems.
// Every file is read back and compared with what the engine renders.
#include "engine_graph.hpp"
#include "blokkily/audio/audio_file.hpp"
#include "blokkily/audio/bounce.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace blokkily;
namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }

Song make_song() {
    Song song;
    song.patterns = {{"Export", Pattern(1920, 480)}};
    for (int step = 0; step < 4; ++step) {
        Trigger trigger;
        trigger.start = step * 480;
        trigger.duration = 360;
        trigger.musical_data = Note{static_cast<std::uint8_t>(60 + step * 4), 1.0F};
        (void)song.patterns[0].pattern.add(trigger);
    }
    song.tracks.assign(2, Track{});
    song.tracks[0].name = "Lead/Hook";
    song.tracks[1].name = "Bass";
    song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
    song.tracks[1].instrument = song.tracks[0].instrument;
    song.tracks[0].mix.pan = -0.5;
    song.tracks[1].mix.pan = 0.6;
    song.tracks[1].mix.gain_db = -4;
    song.returns.resize(1);
    song.returns[0].name = "Room";
    song.tracks[0].sends.push_back({0, -6, false});
    song.clips = {{0, 0, 0, 2}, {1, 0, 0, 2}};
    return song;
}

void prepare(SongEngine& engine, const Song& song) {
    auto graph = populate_graph(engine, song, {}, {});
    std::string error;
    require(graph.error.empty(), graph.error);
    require(engine.prepare(song, 48000, 512, 0, &error, {}), error);
    load_fresh_state(engine, song, graph);
}

BounceReport bounce(SongEngine& engine, const std::filesystem::path& path,
                    ExportContainer container = ExportContainer::wav,
                    std::optional<OutputTap> source = {}) {
    std::string error;
    BounceOptions options;
    options.container = container;
    options.source = source;
    const auto report = bounce_song(engine, path, WaveFormat::float32, 4800, options, &error);
    require(report.has_value(), error);
    return *report;
}

DecodedAudio decode(const std::filesystem::path& path) {
    std::string error;
    auto audio = decode_audio_file(path, &error);
    require(audio.has_value(), path.string() + ": " + error);
    return *audio;
}

std::string magic(const std::filesystem::path& path, std::size_t bytes) {
    std::ifstream in(path, std::ios::binary);
    std::string head(bytes, '\0');
    in.read(head.data(), static_cast<std::streamsize>(bytes));
    return head;
}

// A lossy file read back is the engine's render, give or take what the codec
// throws away: after the best alignment (an encoder may pad the start), the
// residual is at least `snr_db` below the signal in both channels.
void require_same_audio(const DecodedAudio& reference, const DecodedAudio& lossy, double snr_db,
                        const std::string& what) {
    require(lossy.info.sample_rate == reference.info.sample_rate, what + ": sample rate kept");
    require(lossy.info.channels == 2 && !lossy.right.empty(), what + ": stereo kept");
    const auto frames = reference.left.size();
    require(lossy.left.size() + 64 >= frames, what + ": the whole song is in the file");
    require(lossy.left.size() <= frames + 4800, what + ": no more than an encoder frame of padding");
    long best_lag = 0;
    double best = -1.0;
    for (long lag = 0; lag <= 2400; ++lag) {
        double dot = 0.0;
        for (std::size_t i = 0; i + static_cast<std::size_t>(lag) < lossy.left.size() && i < frames; i += 7)
            dot += static_cast<double>(reference.left[i]) * lossy.left[i + static_cast<std::size_t>(lag)];
        if (dot > best) { best = dot; best_lag = lag; }
    }
    for (const auto channel : {0, 1}) {
        const auto& a = channel == 0 ? reference.left : reference.right;
        const auto& b = channel == 0 ? lossy.left : lossy.right;
        double signal = 0.0;
        double noise = 0.0;
        for (std::size_t i = 0; i < frames && i + static_cast<std::size_t>(best_lag) < b.size(); ++i) {
            const double d = static_cast<double>(b[i + static_cast<std::size_t>(best_lag)]) - a[i];
            signal += static_cast<double>(a[i]) * a[i];
            noise += d * d;
        }
        require(signal > 1.0, what + ": the render is not silent");
        const double snr = 10.0 * std::log10(signal / std::max(noise, 1e-20));
        std::cout << what << " channel " << channel << " lag " << best_lag << " SNR " << snr << " dB\n";
        require(snr >= snr_db, what + ": the file holds the engine's render (SNR " +
                                   std::to_string(snr) + " dB)");
    }
}

void run(const std::string& test) {
    const auto directory = std::filesystem::path(BLOKKILY_EXPORT_WORK) / test;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    auto song = make_song();
    SongEngine engine;
    prepare(engine, song);
    const auto master = bounce(engine, directory / "master.wav");
    require(master.peak > 0.05F, "the master is audible");
    const auto reference = decode(directory / "master.wav");

    if (test == "mp3" || test == "ogg") {
        const auto container = test == "mp3" ? ExportContainer::mp3 : ExportContainer::ogg;
        const auto file = directory / (std::string("song") + container_extension(container));
        const auto report = bounce(engine, file, container);
        require(report.frames == master.frames, "the encoded bounce renders the same frames");
        require(report.peak == master.peak, "the encoded bounce renders the same mix");
        if (container == ExportContainer::ogg) require(magic(file, 4) == "OggS", "an Ogg stream");
        else {
            const auto head = magic(file, 3);
            require(head == "ID3" || (static_cast<unsigned char>(head[0]) == 0xFF &&
                                      (static_cast<unsigned char>(head[1]) & 0xE0) == 0xE0),
                    "an MPEG audio stream");
        }
        require_same_audio(reference, decode(file), 20.0, test);
        // The engine is left as a WAV export found it.
        const auto again = bounce(engine, directory / "again.wav");
        require(decode(directory / "again.wav").left == reference.left,
                "an encoded export leaves the next export unchanged");
        (void)again;
    } else if (test == "extension") {
        require(container_for_path("a/b.MP3") == ExportContainer::mp3, ".MP3 is MP3");
        require(container_for_path("x.ogg") == ExportContainer::ogg, ".ogg is Ogg");
        require(container_for_path("x.oga") == ExportContainer::ogg, ".oga is Ogg");
        require(container_for_path("x.wav") == ExportContainer::wav, ".wav is WAV");
        require(container_for_path("x") == ExportContainer::wav, "no extension is WAV");
        require(container_from_name("MP3") == ExportContainer::mp3 &&
                    container_from_name("OGG") == ExportContainer::ogg &&
                    container_from_name("WAV") == ExportContainer::wav,
                "names round-trip");
        require(stem_file_name(0, "Lead/Hook") == "01 Lead Hook", "a slash cannot leave the folder");
        require(stem_file_name(11, "  ") == "12 Track", "a blank name is Track");
    } else if (test == "stems" || test == "stems_sum") {
        BounceOptions options;
        options.include_metronome = true; // never in a stem
        std::uint64_t last_written = 0;
        std::uint64_t last_total = 0;
        options.progress = [&](std::uint64_t written, std::uint64_t total) {
            require(written >= last_written, "stem progress only moves forward");
            last_written = written;
            last_total = total;
        };
        std::string error;
        const auto stems = bounce_stems(engine, song, directory / "stems", WaveFormat::float32,
                                        4800, options, &error);
        require(stems.has_value(), error);
        require(last_written == last_total && last_total == 3 * master.frames,
                "progress counts every stem");
        require(stems->size() == 3, "two tracks and a return");
        require((*stems)[0].file.filename() == "01 Lead Hook.wav", "stem named after its track");
        require((*stems)[1].file.filename() == "02 Bass.wav", "stem named after its track");
        require((*stems)[2].file.filename() == "03 Room.wav", "stem named after its return");
        std::vector<DecodedAudio> read;
        for (const auto& stem : *stems) {
            require(std::filesystem::exists(stem.file), "stem written");
            read.push_back(decode(stem.file));
            require(read.back().left.size() == reference.left.size(), "stem as long as the master");
            require(stem.report.peak > 0.01F, "stem is audible: " + stem.name);
        }
        if (test == "stems") {
            // Each file is the bus's own render, sample for sample.
            const auto tap = bounce(engine, directory / "bass-tap.wav", ExportContainer::wav,
                                    OutputTap{BusKind::track, 1});
            (void)tap;
            require(decode(directory / "bass-tap.wav").left == read[1].left,
                    "the Bass file is the Bass bus");
            // Panned hard apart, so the files are not one signal copied.
            double l = 0, r = 0;
            for (std::size_t i = 0; i < read[0].left.size(); ++i) {
                l += std::abs(read[0].left[i]);
                r += std::abs(read[0].right[i]);
            }
            require(l > r * 1.5, "the lead stem keeps its left pan");
            // An MP3 stem export encodes the same files.
            BounceOptions mp3;
            mp3.container = ExportContainer::mp3;
            const auto encoded = bounce_stems(engine, song, directory / "mp3", WaveFormat::float32,
                                              4800, mp3, &error);
            require(encoded.has_value(), error);
            require((*encoded)[1].file.filename() == "02 Bass.mp3", "MP3 stems are .mp3");
            require_same_audio(read[1], decode((*encoded)[1].file), 20.0, "mp3 stem");
        } else {
            // With the master at unity and nothing on it, the stems are the mix.
            for (std::size_t i = 0; i < reference.left.size(); ++i) {
                const float l = read[0].left[i] + read[1].left[i] + read[2].left[i];
                const float r = read[0].right[i] + read[1].right[i] + read[2].right[i];
                require(std::abs(l - reference.left[i]) < 1e-5F &&
                            std::abs(r - reference.right[i]) < 1e-5F,
                        "stems sum to the master at frame " + std::to_string(i));
            }
            // A muted track's stem is silent: it is the mix that was heard.
            song.tracks[1].mix.mute = true;
            SongEngine muted;
            prepare(muted, song);
            const auto silent = bounce_stems(muted, song, directory / "muted", WaveFormat::float32,
                                             4800, BounceOptions{}, &error);
            require(silent.has_value(), error);
            require((*silent)[1].report.peak == 0.0F, "a muted track's stem is silent");
        }
    } else throw std::runtime_error("unknown case");
}
} // namespace

int main(int argc, char** argv) {
    try { require(argc == 2, "case required"); run(argv[1]); std::cout << "PASS " << argv[1] << '\n'; }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
