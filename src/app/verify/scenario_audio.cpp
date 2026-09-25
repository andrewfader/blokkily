// features/audio_clips.feature, run on its own by the bdd_audio_clips gate
// (`--scenario audio`). Audio files are imported into the real application
// as clips, drawn on the arrangement by the bar layout with their waveforms,
// dragged, trimmed, faded and turned down through synthesized mouse and wheel
// events on the rendered lane, heard sample for sample through the production
// render callback, taken back and forth through history, saved with paths
// relative to the project, bounced and read back, and reloaded with one file
// gone, which is flagged rather than played wrong.

#include "verify/harness.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <source_location>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

namespace fs = std::filesystem;

constexpr double rate = 48000.0;
// tone.wav: four seconds (two bars at 120 BPM) of a 220 Hz tone whose level
// swells and falls once a second, so its waveform is something to look at.
constexpr std::uint64_t tone_frames = 192000;
double tone(std::uint64_t frame, int channel) {
    const double t = static_cast<double>(frame) / rate;
    const double swell = 0.55 + 0.35 * std::sin(2.0 * std::numbers::pi * t);
    return 0.5 * swell * std::sin(2.0 * std::numbers::pi * 220.0 * t + 0.3 * channel);
}

bool write_file(const fs::path& file, std::uint32_t file_rate, std::uint64_t frames,
                double (*content)(std::uint64_t, int)) {
    std::vector<float> left(frames), right(frames);
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        left[frame] = static_cast<float>(content(frame, 0));
        right[frame] = static_cast<float>(content(frame, 1));
    }
    WaveWriter writer;
    return writer.open(file, file_rate, WaveFormat::float32) && writer.write(left, right) &&
           writer.close();
}

// gone.wav: a second at 44.1 kHz, a file resampled on import, which is later
// removed behind the project's back.
double blip(std::uint64_t frame, int) {
    return 0.4 * std::sin(2.0 * std::numbers::pi * 660.0 * static_cast<double>(frame) / 44100.0);
}

void run_audio(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    // A failed check says where, so a red gate names its line.
    const auto check = [&ctx](bool ok,
                              std::source_location where = std::source_location::current()) {
        if (!ok) std::cerr << "audio clips: check failed at line " << where.line() << '\n';
        ctx.check(ok);
    };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto settle = [](int milliseconds) { VerifyContext::settle(milliseconds); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto say = [](const std::string& what) { std::cerr << "audio clips: " << what << '\n'; };

    const QString project = ctx.parser.value("project");
    const fs::path folder = fs::path(project.toStdString()).parent_path();
    const fs::path tone_file = folder / "audio" / "tone.wav";
    const fs::path gone_file = folder / "audio" / "gone.wav";
    std::error_code ignored;
    fs::remove_all(folder / "audio", ignored);
    fs::create_directories(folder / "audio");
    check(!project.isEmpty() && write_file(tone_file, 48000, tone_frames, tone) &&
          write_file(gone_file, 44100, 44100, blip));
    check(window->setProperty("view", "ALL"));
    controller.setTempo(120.0);
    lay_out();
    settle(50);

    auto* lane = ctx.named("audioClipLane");
    const auto x_of = [&](double tick) {
        QVariant out;
        QMetaObject::invokeMethod(lane, "xOfTick", Q_RETURN_ARG(QVariant, out),
                                  Q_ARG(QVariant, tick));
        return out.toDouble();
    };
    const auto clip_row = [&](qint64 id) { return song.audioClip(id); };
    const auto clip = [&](qint64 id) -> const blokkily::AudioClip* {
        for (const auto& found : song.song().audio_clips)
            if (static_cast<qint64>(found.id) == id) return &found;
        return nullptr;
    };
    // A drag on a rendered item: press, four moves, release; then the event
    // loop runs the edit the release queued.
    // The pointer's path is fixed in the window when the button goes down:
    // the item follows the pointer, so its own coordinates would drift.
    const auto drag = [&](QQuickItem* item, QPointF from, QPointF by) {
        if (item == nullptr) return false;
        auto* content = window->contentItem();
        const QPointF pressed = item->mapToItem(content, from);
        ctx.mouse_at(content, pressed, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 4; ++step)
            ctx.mouse_at(content, pressed + by * (step / 4.0), QEvent::MouseMove, Qt::NoButton,
                         Qt::LeftButton);
        ctx.mouse_at(content, pressed + by, QEvent::MouseButtonRelease, Qt::LeftButton,
                     Qt::NoButton);
        settle(60);
        lay_out();
        return true;
    };
    const auto centre_of = [](const QQuickItem* item) {
        return QPointF(item->width() / 2.0, item->height() / 2.0);
    };
    // What the production callback plays from `tick`, left then right.
    std::vector<float> stereo;
    const auto render = [&](double tick, std::size_t frames) {
        if (!ctx.transport.playing()) controller.togglePlayback();
        controller.seekToTick(tick);
        controller.flushRecompile();
        stereo.assign(frames * 2, -9.0F);
        const bool ok = ctx.output != nullptr && ctx.output->pump(stereo);
        if (!ok) stereo.assign(frames * 2, -9.0F);
        return ok;
    };
    const float centre = strip_gain(MixerStrip{}, false).left;
    // The rendered left channel against the file's frames from `frame` on.
    const auto plays_file = [&](std::size_t from, std::uint64_t frame, std::size_t count,
                                double gain) {
        for (std::size_t index = 0; index < count; ++index) {
            const float expected = static_cast<float>(tone(frame + index, 0) * gain) * centre;
            if (std::abs(stereo[from + index] - expected) > 2e-6F) {
                say("sample " + std::to_string(from + index) + " is " +
                    std::to_string(stereo[from + index]) + ", the file says " +
                    std::to_string(expected));
                return false;
            }
        }
        return true;
    };
    const auto silent = [&](std::size_t from, std::size_t count) {
        for (std::size_t index = from; index < from + count; ++index)
            if (std::abs(stereo[index]) > 1e-9F) return false;
        return true;
    };

    // 1. +AUD asks for a file; cancelling it changes nothing.
    {
        auto* add = ctx.named("addAudioButton");
        check(VerifyContext::usable(add, 30.0, 20.0));
        const int tracks = song.trackCount();
        ctx.click_at(add, add != nullptr ? centre_of(add) : QPointF(), Qt::LeftButton);
        settle(60);
        auto* dialog = window->findChild<QObject*>("audioImportDialog");
        check(dialog != nullptr && dialog->property("visible").toBool());
        if (dialog != nullptr) QMetaObject::invokeMethod(dialog, "reject");
        settle(60);
        check(song.trackCount() == tracks && song.song().audio_clips.empty());
        reached("audio clips: +AUD asks for a file, and a cancel changes nothing");
    }

    // 2. An import decodes off the control thread and lands as one step.
    qint64 id = 0;
    int audio_track = -1;
    {
        audio_track = controller.addAudioTrack();
        check(audio_track == 2 && song.song().tracks.at(2).name == "AUDIO 1" &&
              song.song().tracks.at(2).instrument.format.empty());
        check(controller.importAudio(QString::fromStdString(tone_file.string()), audio_track,
                                     1920.0));
        // Nothing is placed until the decode is done.
        check(controller.importsPending() == 1 && song.song().audio_clips.empty());
        for (int wait = 0; wait < 400 && controller.importsPending() > 0; ++wait) settle(25);
        check(controller.importsPending() == 0 && song.song().audio_clips.size() == 1);
        id = controller.lastImportedClip();
        const auto* placed = clip(id);
        check(id > 0 && placed != nullptr && placed->track == 2 && placed->start == 1920 &&
              placed->offset_frames == 0 && placed->length_frames == tone_frames &&
              song.song().audio_files.at(placed->file).path == tone_file);
        // One undo takes the whole import back, file and clip; redo returns it.
        check(song.undo() && song.song().audio_clips.empty() && song.song().audio_files.empty());
        check(song.redo() && clip(id) != nullptr);
        check(controller.engine() != nullptr);
        reached("audio clips: an import decodes in the background and lands as one step");
    }

    // 3. The lane draws the clip on its bars, with its waveform.
    const auto clip_item = [&] { return ctx.named(QString("audioClip%1").arg(id)); };
    {
        settle(50);
        lay_out();
        auto* item = clip_item();
        check(VerifyContext::usable(lane, 300.0, 60.0));
        check(VerifyContext::usable(item, 40.0, 16.0));
        // The clip's edges on the ruler's bars: from bar 2 to the end of bar 3.
        auto* bar2 = ctx.named("rulerBar1");
        auto* bar4 = ctx.named("rulerBar3");
        if (item != nullptr && bar2 != nullptr && bar4 != nullptr) {
            const double left = item->mapToScene({0, 0}).x();
            const double right = item->mapToScene({item->width(), 0}).x();
            const double ruler_left = bar2->mapToScene({0, 0}).x();
            const double ruler_end = bar4->mapToScene({0, 0}).x();
            if (std::abs(left - ruler_left) > 3.0 || std::abs(right - ruler_end) > 3.0)
                say("clip spans " + std::to_string(left) + ".." + std::to_string(right) +
                    ", the ruler " + std::to_string(ruler_left) + ".." +
                    std::to_string(ruler_end));
            check(std::abs(left - ruler_left) <= 3.0 && std::abs(right - ruler_end) <= 3.0);
        } else {
            check(false);
        }
        auto* waveform = ctx.named(QString("waveform%1").arg(id));
        check(waveform != nullptr && waveform->property("buckets").toInt() >= 20);
        auto* row = ctx.named("arrangeRow2");
        check(item != nullptr && row != nullptr &&
              std::abs(item->mapToScene({0, 0}).y() - row->mapToScene({0, 1}).y()) <= 1.5);
        reached("audio clips: the lane places the clip on its bars, with its waveform");
    }

    // 4. Heard sample for sample through the production callback, and the
    // strip's mute acts on it.
    {
        check(render(1920.0 - 20.0, 2048));   // 1000 samples before the clip
        check(silent(0, 1000) && plays_file(1000, 0, 1048, 1.0));
        song.toggleMute(audio_track);
        check(render(1920.0, 1024) && silent(0, 1024));
        song.toggleMute(audio_track);
        check(render(1920.0 + 480.0, 1024) && plays_file(0, 24000, 1024, 1.0));
        reached("audio clips: the clip is heard sample for sample, and its strip mutes it");
    }

    // 5. Dragging moves it on the grid and between tracks, keeping its id.
    {
        auto* body = ctx.named(QString("audioClipBody%1").arg(id));
        const double by = x_of(5760.0) - x_of(1920.0);
        check(drag(body, body != nullptr ? centre_of(body) : QPointF(), {by, 0.0}));
        const auto* moved = clip(id);
        if (moved != nullptr)
            say("dragged by " + std::to_string(by) + " px to tick " +
                std::to_string(moved->start) + " on track " + std::to_string(moved->track));
        check(moved != nullptr && moved->start == 5760 && moved->track == 2);
        check(render(1920.0, 1024) && silent(0, 1024));
        check(render(5760.0, 1024) && plays_file(0, 0, 1024, 1.0));
        // Up one row: onto track 1, the same tick; undo puts it back.
        body = ctx.named(QString("audioClipBody%1").arg(id));
        check(drag(body, body != nullptr ? centre_of(body) : QPointF(), {0.0, -24.0}));
        moved = clip(id);
        check(moved != nullptr && moved->track == 1 && moved->start == 5760);
        check(song.undo() && clip(id) != nullptr && clip(id)->track == 2 &&
              clip(id)->start == 5760);
        settle(30);
        lay_out();
        reached("audio clips: dragging moves the clip on the grid and between tracks");
    }

    // 6. Trimming either edge hides the file in place.
    {
        auto* end = ctx.named(QString("trimEnd%1").arg(id));
        const double by_end = x_of(8640.0) - x_of(9600.0);
        check(drag(end, {3.0, end != nullptr ? end->height() - 4.0 : 0.0}, {by_end, 0.0}));
        const auto* trimmed = clip(id);
        check(trimmed != nullptr && trimmed->start == 5760 && trimmed->length_frames == 144000);
        auto* start = ctx.named(QString("trimStart%1").arg(id));
        const double by_start = x_of(6240.0) - x_of(5760.0);
        check(drag(start, {3.0, start != nullptr ? start->height() - 4.0 : 0.0},
                   {by_start, 0.0}));
        trimmed = clip(id);
        check(trimmed != nullptr && trimmed->start == 6240 && trimmed->offset_frames == 24000 &&
              trimmed->length_frames == 120000);
        // The frame that sounded at tick 6240 before the trim still does.
        check(render(5760.0, 48000) && silent(0, 24000) && plays_file(24000, 24000, 4096, 1.0));
        check(render(8640.0 - 10.0, 1024) && plays_file(0, 144000 - 500, 500, 1.0) &&
              silent(500, 524));
        reached("audio clips: trimming either edge reveals or hides the file in place");
    }

    // 7. Fades from the handles, gain from the wheel; undo takes them back.
    {
        auto* fade_in = ctx.named(QString("fadeIn%1").arg(id));
        const double in_to = x_of(6720.0) - x_of(6240.0);
        const double in_from = fade_in != nullptr ? fade_in->x() + 4.0 : 0.0;
        check(drag(fade_in, {4.0, 4.0}, {in_to - in_from, 0.0}));
        auto* fade_out = ctx.named(QString("fadeOut%1").arg(id));
        const double out_to = x_of(8160.0) - x_of(6240.0);
        const double out_from = fade_out != nullptr ? fade_out->x() + 4.0 : 0.0;
        check(drag(fade_out, {4.0, 4.0}, {out_to - out_from, 0.0}));
        const auto* faded = clip(id);
        const auto near_beat = [](std::uint64_t frames) {
            return frames > 24000 - 1500 && frames < 24000 + 1500;
        };
        check(faded != nullptr && near_beat(faded->fade_in_frames) &&
              near_beat(faded->fade_out_frames));
        if (faded != nullptr)
            say("fades " + std::to_string(faded->fade_in_frames) + " / " +
                std::to_string(faded->fade_out_frames) + " frames");
        // The fade-in starts from silence and rises.
        const auto rms = [&](std::size_t from, std::size_t count) {
            double sum = 0.0;
            for (std::size_t index = from; index < from + count; ++index)
                sum += static_cast<double>(stereo[index]) * stereo[index];
            return std::sqrt(sum / static_cast<double>(count));
        };
        check(render(6240.0, 2048) && std::abs(stereo[0]) < 1e-9F &&
              rms(1500, 480) > 3.0 * rms(0, 480));

        // Six notches of the wheel down: -6 dB, one step of history.
        const bool could_undo = song.canUndo();
        for (int notch = 0; notch < 6; ++notch) {
            auto* body = ctx.named(QString("audioClipBody%1").arg(id));
            if (body == nullptr) break;
            const QPointF scene = body->mapToScene(centre_of(body));
            QWheelEvent wheel(scene, window->mapToGlobal(scene), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &wheel);
            settle(30);
        }
        check(could_undo && clip(id) != nullptr && std::abs(clip(id)->gain_db + 6.0) < 1e-9);
        // Tick 7200 is between the fades: file frame 24000 + 960 ticks.
        const double quieter = db_to_linear(-6.0);
        check(render(7200.0, 1024) && plays_file(0, 72000, 1024, quieter));
        check(song.undo() && clip(id)->gain_db == 0.0);
        check(render(7200.0, 1024) && plays_file(0, 72000, 1024, 1.0));
        check(song.redo() && std::abs(clip(id)->gain_db + 6.0) < 1e-9);
        check(render(7200.0, 1024) && plays_file(0, 72000, 1024, quieter));
        lay_out();
        reached("audio clips: fades and gain shape what is heard, and undo takes them back");
    }

    // 8. Saved relative to the project, bounced as heard, reloaded, and a
    // file gone missing is flagged instead of played.
    {
        check(controller.importAudio(QString::fromStdString(gone_file.string()), 1, 0.0));
        for (int wait = 0; wait < 400 && controller.importsPending() > 0; ++wait) settle(25);
        const qint64 gone = controller.lastImportedClip();
        check(gone != id && clip(gone) != nullptr && clip(gone)->length_frames == 44100);
        check(controller.saveProjectFile(project));
        std::ifstream saved(project.toStdString());
        std::stringstream text;
        text << saved.rdbuf();
        check(text.str().find("audiofile audio/tone.wav ") != std::string::npos &&
              text.str().find("audiofile audio/gone.wav ") != std::string::npos);

        // The export is what the engine plays: read back, sample for sample.
        const QString bounce = QString::fromStdString((folder / "audio-clips-bounce.wav").string());
        check(controller.exportAudioFile(bounce, "FLOAT32"));
        std::string error;
        const auto read = blokkily::read_wave(bounce.toStdString(), &error);
        const auto samples = controller.engine() != nullptr ? controller.engine()->song_samples()
                                                            : 0;
        check(read.has_value() && read->channels == 2 && read->frames >= samples && samples > 0);
        if (read.has_value() && samples > 0 && render(0.0, samples)) {
            bool same = true;
            for (std::uint64_t frame = 0; frame < samples && same; ++frame)
                same = read->interleaved[frame * 2] == stereo[frame] &&
                       read->interleaved[frame * 2 + 1] == stereo[samples + frame];
            check(same);
            check(!silent(0, samples));
        } else {
            check(false);
        }

        // Reloaded, the clips come back with their ids and play.
        check(controller.loadProjectFile(project));
        settle(50);
        check(clip(id) != nullptr && clip(id)->start == 6240 && clip(gone) != nullptr &&
              song.missingAudioFiles() == 0);
        check(render(7200.0, 1024) && plays_file(0, 72000, 1024, db_to_linear(-6.0)));

        // The 44.1 kHz file is removed behind the project's back.
        fs::remove(gone_file, ignored);
        check(controller.loadProjectFile(project));
        settle(50);
        lay_out();
        check(song.missingAudioFiles() == 1 && clip_row(gone).value("missing").toBool() &&
              !clip_row(id).value("missing").toBool());
        check(render(0.0, 4096) && silent(0, 4096));
        check(render(7200.0, 1024) && plays_file(0, 72000, 1024, db_to_linear(-6.0)));
        auto* flagged = ctx.named(QString("audioClip%1").arg(gone));
        check(VerifyContext::usable(flagged, 20.0, 16.0));
        reached("audio clips: saved relative, bounced as heard, a missing file flagged");
    }

    if (ctx.transport.playing()) controller.togglePlayback();
    controller.seekToTick(6240.0);
    settle(50);
    lay_out();
    reached("audio clips: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | clips=" << song.song().audio_clips.size()
            << " | missing=" << song.missingAudioFiles()
            << " | import=" << controller.importStatus().toStdString();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("audio", run_audio);

}  // namespace
}  // namespace blokkily::verify
