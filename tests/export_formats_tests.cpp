// features/export_formats.feature: MP3 and Ogg Vorbis bounces, and stems.
// Every file is read back and compared with what the engine renders.
#include "engine_graph.hpp"
#include "blokkily/audio/audio_file.hpp"
#include "blokkily/audio/bounce.hpp"

#include <algorithm>
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

// --- Dither ----------------------------------------------------------------

// Writes `left` (both channels) as a 16-bit WAV, dithered or not, and reads
// it back.
std::vector<float> through_16_bits(const std::filesystem::path& file, const std::vector<float>& left,
                                   bool dither) {
    WaveWriter writer;
    writer.set_dither(dither);
    std::string error;
    require(writer.open(file, 48000, WaveFormat::pcm16, &error), error);
    require(writer.write(left, left, &error), error);
    require(writer.close(&error), error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), error);
    std::vector<float> read(wave->frames);
    for (std::size_t i = 0; i < read.size(); ++i) read[i] = wave->interleaved[2 * i];
    return read;
}

std::vector<float> sine(double hz, double amplitude, std::size_t frames) {
    std::vector<float> samples(frames);
    for (std::size_t i = 0; i < frames; ++i)
        samples[i] = static_cast<float>(amplitude * std::sin(2.0 * 3.141592653589793 * hz * static_cast<double>(i) / 48000.0));
    return samples;
}

double energy(const std::vector<float>& samples, double hz) {
    // Goertzel over the whole buffer, scaled so a sine of amplitude A gives A^2.
    const double w = 2.0 * std::cos(2.0 * 3.141592653589793 * hz / 48000.0);
    double s1 = 0.0, s2 = 0.0;
    for (const float x : samples) {
        const double s0 = x + w * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - w * s1 * s2;
    return 4.0 * power / (static_cast<double>(samples.size()) * samples.size());
}

void dither_cases(const std::string& test, const std::filesystem::path& directory) {
    constexpr double step = 1.0 / 32768.0;
    if (test == "dither_quiet") {
        // A tone at a third of a step: rounded plainly it is silence; dithered
        // it survives at its level under a little hiss.
        const auto tone = sine(1000.0, step / 3.0, 48000);
        const auto plain = through_16_bits(directory / "plain.wav", tone, false);
        const auto dithered = through_16_bits(directory / "dithered.wav", tone, true);
        const double wanted = energy(tone, 1000.0);
        std::cout << "quiet tone: plain " << energy(plain, 1000.0) / wanted << ", dithered "
                  << energy(dithered, 1000.0) / wanted << " of its energy\n";
        require(std::all_of(plain.begin(), plain.end(), [](float x) { return x == 0.0F; }),
                "undithered, a tone under a step rounds to silence");
        require(std::abs(energy(dithered, 1000.0) / wanted - 1.0) < 0.25,
                "dithered, the tone comes through at its level");
        // The added noise is TPDF: mean 0, about half a step RMS.
        double sum = 0.0, square = 0.0;
        for (std::size_t i = 0; i < tone.size(); ++i) {
            const double error = (static_cast<double>(dithered[i]) - tone[i]) / step;
            sum += error;
            square += error * error;
        }
        const double mean = sum / static_cast<double>(tone.size());
        const double rms = std::sqrt(square / static_cast<double>(tone.size()));
        std::cout << "dither error mean " << mean << " rms " << rms << " steps\n";
        require(std::abs(mean) < 0.02 && rms > 0.45 && rms < 0.55, "the noise is TPDF: mean 0, half a step RMS");
    } else if (test == "dither_distortion") {
        // A tone 1.3 steps high: rounded plainly its error repeats with it,
        // as harmonics (distortion); dithered the error is noise, spread thin.
        const auto tone = sine(997.0, 1.3 * step, 48000);
        const auto error_harmonic = [&](const std::vector<float>& written) {
            std::vector<float> error(written.size());
            for (std::size_t i = 0; i < written.size(); ++i) error[i] = written[i] - tone[i];
            double harmonics = 0.0;
            for (int h = 2; h <= 7; ++h) harmonics += energy(error, 997.0 * h);
            return harmonics;
        };
        const double plain = error_harmonic(through_16_bits(directory / "plain.wav", tone, false));
        const double dithered = error_harmonic(through_16_bits(directory / "dithered.wav", tone, true));
        std::cout << "harmonics of the error: plain " << plain << ", dithered " << dithered << '\n';
        require(dithered < plain / 30.0, "dither turns the rounding's distortion into noise");
    } else if (test == "dither_deterministic") {
        const auto music = sine(440.0, 0.3, 24000);
        const auto first = through_16_bits(directory / "first.wav", music, true);
        const auto second = through_16_bits(directory / "second.wav", music, true);
        require(first == second, "the same mix writes the same samples");
        // A float file is never dithered.
        WaveWriter writer;
        writer.set_dither(true);
        std::string error;
        require(writer.open(directory / "float.wav", 48000, WaveFormat::float32, &error) &&
                    writer.write(music, music, &error) && writer.close(&error), error);
        const auto wave = read_wave(directory / "float.wav", &error);
        require(wave.has_value(), error);
        for (std::size_t i = 0; i < music.size(); ++i)
            require(wave->interleaved[2 * i] == music[i], "a float file is the mix exactly");
    }
}

void run(const std::string& test) {
    const auto directory = std::filesystem::path(BLOKKILY_EXPORT_WORK) / test;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    if (test.rfind("dither_", 0) == 0 && test != "dither_bounce") {
        dither_cases(test, directory);
        return;
    }
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
    } else if (test == "dither_bounce") {
        // A dithered 16-bit bounce is the float bounce plus half a step of
        // noise, and a second one is the same bytes.
        const auto bounced = [&](const char* name) {
            std::string error;
            BounceOptions options;
            options.dither = true;
            require(bounce_song(engine, directory / name, WaveFormat::pcm16, 4800, options, &error).has_value(), error);
            return decode(directory / name);
        };
        const auto first = bounced("first16.wav");
        const auto second = bounced("second16.wav");
        require(first.left == second.left && first.right == second.right, "dithered bounces repeat exactly");
        double square = 0.0;
        for (std::size_t i = 0; i < reference.left.size(); ++i) {
            const double error = (static_cast<double>(first.left[i]) - reference.left[i]) * 32768.0;
            square += error * error;
        }
        const double rms = std::sqrt(square / static_cast<double>(reference.left.size()));
        std::cout << "dithered bounce error " << rms << " steps rms\n";
        require(rms > 0.4 && rms < 0.6, "the dithered bounce is the mix plus half a step of noise");
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
    } else if (test == "stems_one_pass") {
        // Every stem is written by the same render: the song is played once,
        // a block at a time, so progress is told once a block, not once a
        // block for every stem.
        BounceOptions options;
        std::uint64_t calls = 0;
        options.progress = [&](std::uint64_t, std::uint64_t) { ++calls; };
        std::string error;
        const auto stems = bounce_stems(engine, song, directory / "stems", WaveFormat::float32,
                                        4800, options, &error);
        require(stems.has_value(), error);
        const auto blocks = (master.frames + 511) / 512;
        require(calls >= blocks - 1 && calls <= blocks + 2, "one block of render writes every stem");
    } else if (test == "range") {
        // Bar 2 alone: it starts from silence at its first sample and keeps
        // its tail, and holds what bar 2 of the whole bounce holds.
        const std::uint64_t bar = 96000;  // a bar of 4/4 at 120 BPM, 48 kHz
        BounceOptions options;
        options.start_sample = bar;
        options.end_sample = 2 * bar;
        std::string error;
        const auto report = bounce_song(engine, directory / "bar2.wav", WaveFormat::float32, 4800,
                                        options, &error);
        require(report.has_value(), error);
        require(report->frames == bar + 4800, "the stretch and its tail");
        const auto stretch = decode(directory / "bar2.wav");
        double difference = 0.0, level = 0.0;
        for (std::size_t i = 0; i < bar; ++i) {
            const double d = static_cast<double>(stretch.left[i]) - reference.left[bar + i];
            difference += d * d;
            level += static_cast<double>(reference.left[bar + i]) * reference.left[bar + i];
        }
        require(level > 0.0 && difference < level * 1e-4, "the stretch is bar 2 of the song");
        // Its stems are the same stretch.
        const auto stems = bounce_stems(engine, song, directory / "stems", WaveFormat::float32,
                                        4800, options, &error);
        require(stems.has_value(), error);
        for (const auto& stem : *stems) require(stem.report.frames == bar + 4800, "a stem of the stretch");
        // A stretch that is empty or runs past the song is refused.
        BounceOptions past;
        past.start_sample = bar;
        past.end_sample = 3 * bar;
        require(!bounce_song(engine, directory / "past.wav", WaveFormat::float32, 0, past, &error) &&
                    !std::filesystem::exists(directory / "past.wav"),
                "a range past the song is refused");
        BounceOptions empty;
        empty.start_sample = bar;
        empty.end_sample = bar;
        require(!bounce_song(engine, directory / "empty.wav", WaveFormat::float32, 0, empty, &error),
                "an empty range is refused");
    } else if (test == "cancel") {
        // Abandoned after a few blocks: nothing is left behind, and the next
        // export is the song as before.
        BounceOptions options;
        int asked = 0;
        options.cancelled = [&] { return ++asked >= 3; };
        std::string error;
        const auto report = bounce_song(engine, directory / "abandoned.wav", WaveFormat::float32,
                                        4800, options, &error);
        require(!report && error == "cancelled", "the render is abandoned");
        require(asked == 3, "it stops as soon as it is asked to");
        require(!std::filesystem::exists(directory / "abandoned.wav"), "its file is removed");
        BounceOptions stems_options;
        stems_options.cancelled = [] { return true; };
        require(!bounce_stems(engine, song, directory / "stems", WaveFormat::float32, 4800,
                              stems_options, &error) &&
                    !std::filesystem::exists(directory / "stems" / "01 Lead Hook.wav"),
                "abandoned stems leave no files");
        (void)bounce(engine, directory / "after.wav");
        require(decode(directory / "after.wav").left == reference.left,
                "an abandoned render leaves the next one unchanged");
    } else throw std::runtime_error("unknown case");
}
} // namespace

int main(int argc, char** argv) {
    try { require(argc == 2, "case required"); run(argv[1]); std::cout << "PASS " << argv[1] << '\n'; }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
