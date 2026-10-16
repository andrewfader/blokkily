// features/export_formats.feature, run by the bdd_export_formats gate: the
// rail's format chips, MP3 and Ogg bounces, stems, and the scope video, each
// read back and compared with what the engine renders.
#include "verify/harness.hpp"
#include "song_model.hpp"
#include "blokkily/audio/audio_file.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QProcess>

#include <cmath>
#include <iostream>
#include <set>
#include <source_location>

namespace blokkily::verify {
namespace {

// The best-aligned signal-to-residual ratio of `copy` against `reference`
// (left channels), in dB: how much of the engine's render a lossy file holds.
double aligned_snr(const std::vector<float>& reference, const std::vector<float>& copy,
                   long max_lag) {
    long best_lag = 0;
    double best = -1e30;
    for (long lag = -max_lag; lag <= max_lag; ++lag) {
        double dot = 0.0;
        for (std::size_t i = 0; i < reference.size(); i += 5) {
            const long j = static_cast<long>(i) + lag;
            if (j >= 0 && j < static_cast<long>(copy.size()))
                dot += static_cast<double>(reference[i]) * copy[static_cast<std::size_t>(j)];
        }
        if (dot > best) { best = dot; best_lag = lag; }
    }
    double signal = 0.0;
    double noise = 0.0;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const long j = static_cast<long>(i) + best_lag;
        const double c = j >= 0 && j < static_cast<long>(copy.size()) ? copy[static_cast<std::size_t>(j)] : 0.0;
        signal += static_cast<double>(reference[i]) * reference[i];
        noise += (c - reference[i]) * (c - reference[i]);
    }
    return signal <= 0.0 ? -1e9 : 10.0 * std::log10(signal / std::max(noise, 1e-20));
}

bool run_ffmpeg(const QStringList& arguments, QByteArray* out = nullptr) {
    QProcess process;
    process.start(qEnvironmentVariable("BLOKKILY_FFMPEG", "ffmpeg"),
                  QStringList{"-hide_banner", "-nostdin", "-loglevel", "error", "-y"} + arguments);
    if (!process.waitForStarted(10000)) return false;
    process.closeWriteChannel();
    QByteArray collected;
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(1000);
        collected += process.readAllStandardOutput();
    }
    collected += process.readAllStandardOutput();
    if (out != nullptr) *out = collected;
    if (process.exitCode() != 0) std::cerr << process.readAllStandardError().toStdString();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void run_export_formats(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "export formats failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));

    // Three CLAP tracks, each its own part of one two-bar section, panned
    // apart and at different pitches, so every lane has a waveform of its own.
    blokkily::Song song;
    song.patterns.clear();
    song.tracks.clear();
    song.clips.clear();
    const auto instrument = s.song().tracks[0].instrument;
    const char* names[] = {"Lead", "Bass", "Arp"};
    for (int t = 0; t < 3; ++t) {
        blokkily::Track track;
        track.name = names[t];
        track.instrument = instrument;
        track.mix.pan = t == 0 ? -0.6 : t == 1 ? 0.0 : 0.6;
        track.mix.gain_db = -6;
        song.tracks.push_back(track);
    }
    const auto section = song.add_section("VERSE", 3840, 480);
    for (std::size_t t = 0; t < 3; ++t) {
        const auto slot = song.part(section, t);
        check(slot.has_value());
        if (!slot) continue;
        for (int step = 0; step < 8; ++step) {
            blokkily::Trigger trigger;
            trigger.start = step * 480;
            trigger.duration = 300;
            trigger.musical_data = blokkily::Note{
                static_cast<std::uint8_t>((t == 1 ? 36 : t == 0 ? 64 : 76) + (step % 4) * 3), 0.9F};
            // The fixture's oscillator, not its steady test level: a scope
            // of a constant is a flat line.
            trigger.locks = {{"tone", 1, 1.0, blokkily::ParameterLock::Kind::automation}};
            (void)song.patterns[*slot].pattern.add(trigger);
        }
    }
    check(song.place_section(section, 0) == 3);
    s.replace(std::move(song));
    c.flushRecompile();
    check(c.saveProject(ctx.parser.value("project")));
    layout();

    // The rail: the format chips are usable, and choosing MP3 takes the
    // depth chips out of play, since an MP3 has none.
    for (const char* name : {"exportFormatWAV", "exportFormatMP3", "exportFormatOGG",
                             "exportStemsButton", "exportScopeButton", "exportDepthPCM24"})
        check(VerifyContext::usable(ctx.named(name), 36, 18));
    auto* mp3_chip = ctx.named("exportFormatMP3");
    auto* depth = ctx.named("exportDepthPCM24");
    check(depth && depth->isEnabled());
    if (mp3_chip) ctx.click_at(mp3_chip, {mp3_chip->width() / 2, mp3_chip->height() / 2}, Qt::LeftButton);
    layout();
    check(mp3_chip && mp3_chip->property("on").toBool());
    check(depth && !depth->isEnabled());
    ctx.reached("export formats: choosing MP3 on the rail");

    // MP3 and Ogg bounces hold the mix the WAV bounce holds.
    const QString base = ctx.parser.value("export");
    const QString stem_base = QFileInfo(base).path() + "/" + QFileInfo(base).completeBaseName();
    QDir().mkpath(QFileInfo(base).path());
    check(c.exportAudioFile(base, "FLOAT32"));
    std::string error;
    const auto wav = blokkily::decode_audio_file(base.toStdString(), &error);
    check(wav.has_value());
    float peak = 0.0F;
    if (wav) for (const float v : wav->left) peak = std::max(peak, std::abs(v));
    check(peak > 0.05F);
    for (const char* extension : {".mp3", ".ogg"}) {
        const QString file = stem_base + extension;
        check(c.exportAudioFile(file, "FLOAT32"));
        check(c.exportStatus().startsWith(QFileInfo(file).fileName()));
        const auto lossy = blokkily::decode_audio_file(file.toStdString(), &error);
        check(lossy.has_value());
        if (wav && lossy) {
            check(lossy->info.sample_rate == wav->info.sample_rate && lossy->info.channels == 2);
            const double snr = aligned_snr(wav->left, lossy->left, 2400);
            std::cerr << extension << " SNR " << snr << " dB\n";
            check(snr > 20.0);
        }
    }
    ctx.reached("export formats: MP3 and Ogg hold the engine's render");

    // DITHER, on unless turned off, for a 16- or 24-bit WAV; out of play for
    // a float file or a lossy one.
    auto* dither = ctx.named("exportDitherButton");
    check(VerifyContext::usable(dither, 50, 12));
    check(ctx.window->property("exportDither").toBool());
    check(dither && !dither->isEnabled()); // MP3 is chosen
    for (const char* name : {"exportFormatWAV", "exportDepthPCM16"}) {
        auto* chip = ctx.named(name);
        if (chip) ctx.click_at(chip, {chip->width() / 2, chip->height() / 2}, Qt::LeftButton);
        layout();
    }
    check(dither && dither->isEnabled() && dither->property("on").toBool());
    if (dither) ctx.click_at(dither, {dither->width() / 2, dither->height() / 2}, Qt::LeftButton);
    layout();
    check(!ctx.window->property("exportDither").toBool());
    if (dither) ctx.click_at(dither, {dither->width() / 2, dither->height() / 2}, Qt::LeftButton);
    layout();
    check(ctx.window->property("exportDither").toBool());
    // A dithered 16-bit export is the float mix plus half a step of noise;
    // undithered it is the mix rounded.
    const auto error_rms = [&](const QString& file) {
        const auto read = blokkily::decode_audio_file(file.toStdString(), &error);
        check(read.has_value() && wav.has_value());
        if (!read || !wav) return 0.0;
        double square = 0.0;
        for (std::size_t i = 0; i < wav->left.size(); ++i) {
            const double e = (static_cast<double>(read->left[i]) - wav->left[i]) * 32768.0;
            square += e * e;
        }
        return std::sqrt(square / static_cast<double>(wav->left.size()));
    };
    check(c.exportAudioFile(stem_base + "-16-dithered.wav", "PCM16", false, true));
    check(c.exportStatus().endsWith("dithered"));
    check(c.exportAudioFile(stem_base + "-16-plain.wav", "PCM16", false, false));
    check(!c.exportStatus().contains("dithered"));
    const double dithered_rms = error_rms(stem_base + "-16-dithered.wav");
    const double plain_rms = error_rms(stem_base + "-16-plain.wav");
    std::cerr << "16-bit error: dithered " << dithered_rms << ", plain " << plain_rms << " steps rms\n";
    check(dithered_rms > 0.4 && dithered_rms < 0.6 && plain_rms < 0.35);
    auto* f32 = ctx.named("exportDepthFLOAT32");
    if (f32) ctx.click_at(f32, {f32->width() / 2, f32->height() / 2}, Qt::LeftButton);
    layout();
    check(dither && !dither->isEnabled());
    ctx.reached("export formats: DITHER dithers a 16-bit WAV, and only that");

    // Stems: one file per track, named after it, each its own bus.
    const QString stems_dir = stem_base + "-stems";
    QDir(stems_dir).removeRecursively();
    check(c.exportTrackFiles(stems_dir, "OGG"));
    check(c.exportStatus().startsWith("3 stems · OGG"));
    const QStringList files = QDir(stems_dir).entryList(QDir::Files, QDir::Name);
    check(files == QStringList({"01 Lead.ogg", "02 Bass.ogg", "03 Arp.ogg"}));
    const QString wav_stems = stem_base + "-wav-stems";
    QDir(wav_stems).removeRecursively();
    check(c.exportTrackFiles(wav_stems, "WAV", "FLOAT32"));
    std::vector<std::vector<float>> stem_left;
    for (const char* name : {"01 Lead", "02 Bass", "03 Arp"}) {
        const auto exact = blokkily::decode_audio_file((wav_stems + "/" + name + ".wav").toStdString(), &error);
        const auto ogg = blokkily::decode_audio_file((stems_dir + "/" + name + ".ogg").toStdString(), &error);
        check(exact.has_value() && ogg.has_value());
        if (!exact || !ogg || !wav) continue;
        check(exact->left.size() == wav->left.size());
        check(aligned_snr(exact->left, ogg->left, 2400) > 20.0);
        stem_left.push_back(exact->left);
    }
    // Panned and pitched apart, the files are three signals, not one copied,
    // and with nothing on the master they add up to the bounce.
    if (stem_left.size() == 3 && wav) {
        check(stem_left[0] != stem_left[1] && stem_left[1] != stem_left[2]);
        double worst = 0.0;
        for (std::size_t i = 0; i < wav->left.size(); ++i)
            worst = std::max(worst, std::abs(static_cast<double>(stem_left[0][i]) + stem_left[1][i] +
                                             stem_left[2][i] - wav->left[i]));
        check(worst < 1e-5);
    }
    ctx.reached("export formats: every track a file of its own");

    // The scope video: H.264 at 1280 x 720 and 30 fps, as long as the song,
    // whose sound is the master bounce and whose picture is the lanes.
    const QString video = stem_base + "-scope.mp4";
    QFile::remove(video);
    check(c.scopeVideoAvailable());
    check(c.exportScopeVideo(video));
    std::cerr << c.exportStatus().toStdString() << '\n';
    check(c.exportStatus().contains("3 lanes"));
    check(QFileInfo(video).size() > 10000);
    const QString audio_back = stem_base + "-scope-audio.wav";
    check(run_ffmpeg({"-i", video, "-map", "0:a", "-c:a", "pcm_f32le", "-ar", "48000", audio_back}));
    const auto heard = blokkily::decode_audio_file(audio_back.toStdString(), &error);
    check(heard.has_value());
    if (heard && wav) {
        const double snr = aligned_snr(wav->left, heard->left, 4800);
        std::cerr << "video audio SNR " << snr << " dB\n";
        check(snr > 15.0);
        check(std::abs(static_cast<long>(heard->left.size()) - static_cast<long>(wav->left.size())) < 4800);
    }
    QByteArray tiny;
    check(run_ffmpeg({"-i", video, "-map", "0:v", "-f", "rawvideo", "-pix_fmt", "gray", "-s", "32x18", "-"}, &tiny));
    const int frames = static_cast<int>(tiny.size() / (32 * 18));
    const int expected = wav ? static_cast<int>(std::ceil(static_cast<double>(wav->left.size()) / wav->info.sample_rate * 30.0)) : 0;
    std::cerr << "video frames " << frames << " expected " << expected << '\n';
    check(frames == expected);
    // A frame in the middle of the song, kept as evidence beside the window.
    const QString frame_png = QFileInfo(ctx.parser.value("screenshot")).path() + "/export-scope-frame.png";
    QFile::remove(frame_png);
    check(run_ffmpeg({"-ss", "1.1", "-i", video, "-frames:v", "1", frame_png}));
    const QImage frame(frame_png);
    check(frame.width() == 1280 && frame.height() == 720);
    if (!frame.isNull()) {
        // Each lane's colour is drawn: the three tracks' and the master's ink.
        std::set<QRgb> colours;
        int lead = 0, bass = 0, arp = 0, master = 0;
        // The rows each lane's colour reaches: a waveform swings through
        // its lane, where a flat line (a scope of silence or of a constant)
        // is a row or two.
        std::set<int> lead_rows, bass_rows, arp_rows, master_rows;
        for (int y = 0; y < frame.height(); y += 2)
            for (int x = 0; x < frame.width(); x += 2) {
                const QColor p = frame.pixelColor(x, y);
                colours.insert(p.rgb() & 0xF0F0F0);
                const auto near = [&](int r, int g, int b) {
                    return std::abs(p.red() - r) < 40 && std::abs(p.green() - g) < 40 && std::abs(p.blue() - b) < 40;
                };
                if (near(0xc8, 0xff, 0x3d)) { ++lead; lead_rows.insert(y); }
                if (near(0x4d, 0x7c, 0xff)) { ++bass; bass_rows.insert(y); }
                if (near(0xff, 0xb3, 0x40)) { ++arp; arp_rows.insert(y); }
                if (y > frame.height() * 2 / 3 && near(0xe8, 0xe9, 0xed)) { ++master; master_rows.insert(y); }
            }
        std::cerr << "scope colours " << colours.size() << " lead " << lead << " bass " << bass
                  << " arp " << arp << " master " << master << " rows " << lead_rows.size() << ' '
                  << bass_rows.size() << ' ' << arp_rows.size() << ' ' << master_rows.size() << '\n';
        check(colours.size() > 8 && lead > 50 && bass > 50 && arp > 50 && master > 50);
        check(lead_rows.size() > 40 && bass_rows.size() > 40 && arp_rows.size() > 40 &&
              master_rows.size() > 40);
    }
    ctx.reached("export formats: the scope video shows every track over the master");

    layout();
    check(VerifyContext::usable(ctx.named("exportStatus"), 100, 10));
    check(ctx.save_screenshot());
    ctx.finish("MP3/Ogg bounces, stems and the scope video read back against the engine");
}

[[maybe_unused]] const bool registered = register_scenario("export_formats", run_export_formats);

} // namespace
} // namespace blokkily::verify
