// features/comping_and_punch.feature, run by bdd_comping: two takes of the
// same bars in the real window; the TAKE badge chooses between them, SPLIT
// cuts them at the playhead, PUNCH arms the punch range, and the export is
// the takes chosen.
#include "verify/harness.hpp"
#include "song_model.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_comping(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "comping failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto click = [&](QQuickItem* item) {
        if (item) ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        layout();
    };
    const QString folder = QFileInfo(ctx.parser.value("project")).path();
    QDir().mkpath(folder);
    // Two takes of the same second: A steady at 0.1, B at 0.3.
    const auto steady = [&](const QString& name, float level) {
        const auto path = (folder + "/" + name).toStdString();
        std::vector<float> samples(48000, level);
        blokkily::WaveWriter writer;
        std::string error;
        check(writer.open(path, 48000, blokkily::WaveFormat::float32, &error) &&
              writer.write(samples, samples, &error) && writer.close(&error));
        return blokkily::AudioFileRef{path, 48000, 48000, 2};
    };
    blokkily::Song song;
    song.patterns.clear();
    song.tracks.clear();
    song.clips.clear();
    blokkily::Track vocal;
    vocal.name = "VOCAL";
    song.tracks.push_back(vocal);
    const auto section = song.add_section("VERSE", 1920, 480);
    check(song.place_section(section, 0) == 1);
    song.audio_files = {steady("take-a.wav", 0.1F), steady("take-b.wav", 0.3F)};
    for (std::size_t file = 0; file < 2; ++file) {
        blokkily::AudioClip clip;
        clip.id = song.next_audio_clip_id();
        clip.track = 0;
        clip.file = file;
        clip.length_frames = 48000;
        clip.comped_out = file == 0; // A kept, B plays, as a loop recording leaves them
        song.audio_clips.push_back(clip);
    }
    const auto a = song.audio_clips[0].id;
    const auto b = song.audio_clips[1].id;
    s.replace(std::move(song));
    c.flushRecompile();
    s.selectTrack(0);
    layout();

    const auto exported = [&](const QString& name) {
        const QString file = folder + "/" + name + ".wav";
        check(c.exportAudioFile(file, "FLOAT32"));
        auto wave = blokkily::read_wave(file.toStdString());
        check(wave.has_value());
        return wave ? wave->interleaved : std::vector<float>{};
    };
    // The left channel of a centred strip: cos(pi/4) of the take.
    const auto at = [](const std::vector<float>& left, std::size_t frame) {
        return 2 * frame < left.size() ? left[2 * frame] / 0.70710678F : -1.0F;
    };
    check(std::abs(at(exported("b"), 1000) - 0.3F) < 1e-5F);

    // B's badge: TAKE 2/2; clicking it plays the next take, A.
    auto* badge = ctx.named(QString("takeBadge%1").arg(b));
    check(VerifyContext::usable(badge, 14, 10));
    check(badge && badge->childItems().value(0) &&
          badge->childItems().value(0)->property("text").toString().endsWith("2/2"));
    click(badge);
    check(s.audioClip(static_cast<qint64>(a)).value("compedOut").toBool() == false);
    check(s.audioClip(static_cast<qint64>(b)).value("compedOut").toBool() == true);
    check(std::abs(at(exported("a"), 1000) - 0.1F) < 1e-5F);
    check(s.undo());
    check(s.audioClip(static_cast<qint64>(b)).value("compedOut").toBool() == false);
    check(s.redo());
    ctx.reached("comping: the TAKE badge plays the next take, one undo a click");

    // SPLIT at the playhead, half a second in; B for the second half.
    c.seekToStep(4.0); // 480 ticks: half a second at 120 BPM
    layout();
    check(ctx.transport.tick() == 480.0);
    auto* split = ctx.named("splitTakesButton");
    check(VerifyContext::usable(split, 40, 18));
    click(split);
    check(s.song().audio_clips.size() == 4);
    qint64 b_second = -1;
    for (const auto& clip : s.song().audio_clips)
        if (clip.file == 1 && clip.start == 480) b_second = static_cast<qint64>(clip.id);
    check(b_second >= 0 && s.chooseTake(b_second));
    const auto comped = exported("comped");
    std::cerr << "comped export: " << at(comped, 1000) << " then " << at(comped, 30000) << '\n';
    check(std::abs(at(comped, 1000) - 0.1F) < 1e-5F && std::abs(at(comped, 30000) - 0.3F) < 1e-5F);
    ctx.reached("comping: SPLIT, and a different take after the cut, are what exports");

    // PUNCH arms the punch range on the loop's IN and OUT.
    auto* punch = ctx.named("punchButton");
    check(VerifyContext::usable(punch, 40, 18));
    check(!s.punch());
    click(punch);
    check(s.punch() && s.song().loop.punching() && punch && punch->property("on").toBool());
    // All of it is saved and loaded.
    check(c.saveProject(ctx.parser.value("project")));
    check(c.loadProject(ctx.parser.value("project")));
    layout();
    check(s.punch());
    int comped_out = 0;
    for (const auto& clip : s.song().audio_clips) comped_out += clip.comped_out ? 1 : 0;
    check(comped_out == 2);
    ctx.reached("comping: PUNCH and the chosen takes survive a save and a load");
    check(ctx.save_screenshot());
    ctx.finish("takes chosen, split and punched in the window, heard in the export");
}

[[maybe_unused]] const bool registered = register_scenario("comping", run_comping);

} // namespace
} // namespace blokkily::verify
