// features/clip_warp.feature, run on its own by the bdd_clip_warp gate
// (`--scenario clip_warp`). A click track recorded at 100 BPM and a 1 kHz tone
// are imported into the real application; the warp panel is opened from a
// clip's W button and driven with synthesized clicks and typed keys; the clip
// is seen RENDERING (and heard silent) while the worker renders, then heard
// through the production render callback on the song's beats, an octave up,
// twice as long; undo and redo take the edits back and forth; the warp is
// saved, loaded and exported, and the export read back is what was played.

#include "verify/harness.hpp"

#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QVariant>

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
constexpr int click_beats = 12;
constexpr double click_period = rate * 60.0 / 100.0;   // 100 BPM
constexpr std::uint64_t tone_frames = 96000;            // two seconds, a bar at 120

double click(std::uint64_t frame) {
    const double beat = std::floor(static_cast<double>(frame) / click_period);
    const double into = static_cast<double>(frame) - std::llround(beat * click_period);
    if (into < 0.0 || into >= 240.0) return 0.0;
    return 0.8 * std::sin(2.0 * std::numbers::pi * 3000.0 * into / rate) * std::exp(-into / 60.0);
}

double tone(std::uint64_t frame) {
    return 0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(frame) / rate);
}

bool write_file(const fs::path& file, std::uint64_t frames, double (*content)(std::uint64_t)) {
    std::vector<float> samples(frames);
    for (std::uint64_t frame = 0; frame < frames; ++frame)
        samples[frame] = static_cast<float>(content(frame));
    WaveWriter writer;
    return writer.open(file, static_cast<std::uint32_t>(rate), WaveFormat::float32) &&
           writer.write(samples, samples) && writer.close();
}

// Where each click starts: the first sample over the threshold, then
// nothing for most of a beat.
std::vector<std::size_t> onsets(const float* samples, std::size_t count) {
    std::vector<std::size_t> found;
    for (std::size_t index = 0; index < count; ++index) {
        if (std::abs(samples[index]) <= 0.1F) continue;
        if (!found.empty() && index < found.back() + 9000) continue;
        found.push_back(index);
    }
    return found;
}

double goertzel(const float* samples, std::size_t count, double hz) {
    const double coefficient = 2.0 * std::cos(2.0 * std::numbers::pi * hz / rate);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const double s0 = samples[index] + coefficient * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::max(0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2);
}

double dominant(const float* samples, std::size_t count) {
    double best = 0.0, energy = 0.0;
    for (double hz = 500.0; hz <= 3000.0; hz += 2.0) {
        const double here = goertzel(samples, count, hz);
        if (here > energy) {
            energy = here;
            best = hz;
        }
    }
    return best;
}

void run_clip_warp(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const auto check = [&ctx](bool ok,
                              std::source_location where = std::source_location::current()) {
        if (!ok) std::cerr << "clip warp: check failed at line " << where.line() << '\n';
        ctx.check(ok);
    };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto settle = [](int milliseconds) { VerifyContext::settle(milliseconds); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto say = [](const std::string& what) { std::cerr << "clip warp: " << what << '\n'; };

    const QString project = ctx.parser.value("project");
    const fs::path folder = fs::path(project.toStdString()).parent_path();
    const fs::path click_file = folder / "audio" / "clicks100.wav";
    const fs::path tone_file = folder / "audio" / "tone1k.wav";
    std::error_code ignored;
    fs::remove_all(folder / "audio", ignored);
    fs::create_directories(folder / "audio");
    check(!project.isEmpty() &&
          write_file(click_file, static_cast<std::uint64_t>(click_period * click_beats), click) &&
          write_file(tone_file, tone_frames, tone));
    check(window->setProperty("view", "ALL"));
    controller.setTempo(120.0);
    // The two instrument tracks the session opens with are muted: only the
    // clips are listened to.
    song.toggleMute(0);
    song.toggleMute(1);
    lay_out();

    const auto clip = [&](qint64 id) -> const blokkily::AudioClip* {
        for (const auto& found : song.song().audio_clips)
            if (static_cast<qint64>(found.id) == id) return &found;
        return nullptr;
    };
    const auto import = [&](const fs::path& file) -> qint64 {
        const int track = controller.addAudioTrack();
        if (!controller.importAudio(QString::fromStdString(file.string()), track, 0.0)) return 0;
        for (int wait = 0; wait < 400 && controller.importsPending() > 0; ++wait) settle(25);
        return controller.lastImportedClip();
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
    const auto silent = [&](std::size_t from, std::size_t count) {
        for (std::size_t index = from; index < from + count; ++index)
            if (std::abs(stereo[index]) > 1e-6F) return false;
        return true;
    };
    // The clicks heard from `stereo`'s left channel, each within 1 ms of a
    // beat of the 120 BPM song.
    const auto clicks_on_beats = [&](const float* left, std::size_t count) {
        const auto found = onsets(left, count);
        bool ok = found.size() == static_cast<std::size_t>(click_beats);
        double worst = 0.0;
        for (std::size_t beat = 0; beat < found.size() && beat < click_beats; ++beat)
            worst = std::max(worst, std::abs(static_cast<double>(found[beat]) -
                                             static_cast<double>(beat) * 24000.0) / 48.0);
        say(std::to_string(found.size()) + " clicks, the worst " + std::to_string(worst) +
            " ms off its beat");
        return ok && worst <= 1.0;
    };
    const auto open_panel = [&](qint64 id) {
        if (auto* panel = window->findChild<QObject*>("clipWarpPanel"))
            QMetaObject::invokeMethod(panel, "close");
        settle(20);
        lay_out();
        auto* button = ctx.named(QString("warpButton%1").arg(id));
        check(VerifyContext::usable(button, 14.0, 9.0));
        ctx.click_at(button, button != nullptr ? QPointF(button->width() / 2, button->height() / 2)
                                               : QPointF(),
                     Qt::LeftButton);
        settle(40);
        lay_out();
        // The popup is not an item; what it shows is.
        auto* popup = window->findChild<QObject*>("clipWarpPanel");
        auto* content = ctx.named("clipWarpContent");
        const bool open = popup != nullptr && popup->property("opened").toBool() &&
                          popup->property("clipId").toLongLong() == id;
        return open ? content : nullptr;
    };
    // Types `text` into a rendered field of the panel and presses Return.
    // Without `then_settle` nothing queued runs afterwards: a rendition the
    // worker finishes meanwhile is not collected until the loop turns.
    const auto type_into = [&](const char* field_name, const QString& text,
                               bool then_settle = true) {
        auto* field = ctx.named(field_name);
        check(VerifyContext::usable(field, 40.0, 20.0));
        if (field == nullptr) return;
        ctx.click_at(field, {field->width() / 2, field->height() / 2}, Qt::LeftButton);
        settle(20);
        ctx.chord_key(Qt::Key_A, Qt::ControlModifier);
        for (const QChar character : text)
            ctx.type_key(static_cast<Qt::Key>(character.unicode()), QString(character));
        ctx.type_key(Qt::Key_Return, "\r");
        if (then_settle) settle(20);
    };

    // 1. The W button of an imported clip opens its warp panel.
    const qint64 clicks = import(click_file);
    const qint64 tones = import(tone_file);
    const int click_track = clicks > 0 && clip(clicks) ? static_cast<int>(clip(clicks)->track) : -1;
    const int tone_track = tones > 0 && clip(tones) ? static_cast<int>(clip(tones)->track) : -1;
    check(clicks > 0 && tones > 0 && click_track == 2 && tone_track == 3);
    song.toggleMute(tone_track);   // the clicks alone, first
    {
        auto* panel = open_panel(clicks);
        check(panel != nullptr && panel->isVisible() && VerifyContext::usable(panel, 220.0, 150.0));
        check(VerifyContext::usable(ctx.named("warpFollow"), 80.0, 20.0));
        check(VerifyContext::usable(ctx.named("warpState"), 30.0, 10.0));
        reached("clip warp: the W button opens the clip's warp panel");
    }

    // 2. FOLLOW TEMPO detects 100 BPM; the clip is RENDERING, and silent, while
    // the worker renders it.
    {
        auto* follow = ctx.named("warpFollow");
        ctx.click_at(follow, follow != nullptr ? QPointF(follow->width() / 2, follow->height() / 2)
                                               : QPointF(),
                     Qt::LeftButton);
        check(clip(clicks) != nullptr && clip(clicks)->warp.follow_tempo &&
              clip(clicks)->warp.source_bpm == 100.0 && song.canUndo());
        // The recompile the edit asked for runs now; the rendition cannot be
        // back before the event loop turns.
        controller.flushRecompile();
        check(song.audioClip(clicks).value("rendering").toBool() &&
              controller.warpRendersPending() >= 1);
        auto* veil = ctx.named(QString("renderingVeil%1").arg(clicks));
        check(veil != nullptr && veil->isVisible() && VerifyContext::usable(veil, 40.0, 16.0));
        auto* state = ctx.named("warpState");
        check(state != nullptr && state->property("text").toString() == "RENDERING…");
        auto* bpm = ctx.named("warpSourceBpm");
        check(bpm != nullptr && bpm->property("text").toString() == "100.00");
        check(render(0.0, 48000) && silent(0, 48000));
        reached("clip warp: following the tempo detects 100 BPM and renders, silent meanwhile");
    }

    // 3. Rendered, every click is on a beat of the 120 BPM song.
    {
        controller.waitForWarpRenders();
        lay_out();
        check(!song.audioClip(clicks).value("rendering").toBool() &&
              controller.warpRendersPending() == 0);
        auto* veil = ctx.named(QString("renderingVeil%1").arg(clicks));
        check(veil != nullptr && !veil->isVisible());
        auto* state = ctx.named("warpState");
        check(state != nullptr && state->property("text").toString() == "READY");
        // The clip now ends on beat 12 of the song, not on 14.4.
        check(std::abs(song.audioClip(clicks).value("endTick").toDouble() - 12 * 480.0) < 1.0);
        check(render(0.0, 12 * 24000) && clicks_on_beats(stereo.data(), 12 * 24000));
        reached("clip warp: the click track plays on the song's beats");
    }

    // 4. Typed into the panel: +12 semitones plays the tone an octave up at
    // the same length; a stretch of 2 doubles it and keeps the pitch.
    const auto tone_window = [&](double& hz, double& ratio_to_1k) {
        const float* middle = stereo.data() + 24000;
        hz = dominant(middle, 48000);
        ratio_to_1k = goertzel(middle, 48000, hz) / std::max(1e-12, goertzel(middle, 48000, 1000.0));
    };
    const auto sounds_until = [&](std::size_t frames) {
        std::size_t last = 0;
        for (std::size_t index = 0; index < frames; ++index)
            if (std::abs(stereo[index]) > 0.05F) last = index;
        return last;
    };
    {
        song.toggleMute(click_track);
        song.toggleMute(tone_track);
        auto* panel = open_panel(tones);
        check(panel != nullptr && panel->isVisible());
        type_into("warpSemitones", "12");
        check(clip(tones) != nullptr && clip(tones)->warp.semitones == 12);
        controller.waitForWarpRenders();
        double hz = 0.0, ratio = 0.0;
        check(render(0.0, 3 * 48000));
        tone_window(hz, ratio);
        const auto last = sounds_until(3 * 48000);
        say("+12 st: " + std::to_string(hz) + " Hz, sounding until " + std::to_string(last));
        check(std::abs(hz - 2000.0) <= 4.0 && ratio > 100.0);
        check(last + 1 >= tone_frames - 240 && last < tone_frames);

        type_into("warpRatio", "2");
        check(clip(tones) != nullptr && clip(tones)->warp.ratio == 2.0);
        controller.waitForWarpRenders();
        lay_out();
        check(std::abs(song.audioClip(tones).value("endTick").toDouble() - 3840.0) < 1.0);
        auto* box = ctx.named(QString("audioClip%1").arg(tones));
        auto* bar3 = ctx.named("rulerBar2");
        check(box != nullptr && bar3 != nullptr &&
              std::abs(box->mapToScene({box->width(), 0}).x() - bar3->mapToScene({0, 0}).x()) <= 3.0);
        check(render(0.0, 5 * 48000));
        tone_window(hz, ratio);
        const auto stretched = sounds_until(5 * 48000);
        say("x2: " + std::to_string(hz) + " Hz, sounding until " + std::to_string(stretched));
        check(std::abs(hz - 2000.0) <= 4.0 && stretched + 1 >= 2 * tone_frames - 240 &&
              stretched < 2 * tone_frames);
        auto* badge = ctx.named(QString("warpBadge%1").arg(tones));
        check(badge != nullptr && badge->isVisible() &&
              badge->property("text").toString().contains("+12st"));
        reached("clip warp: typed pitch and stretch are heard an octave up and twice as long");
    }

    // 5. Undo takes the stretch, then the pitch, back; redo returns them.
    {
        double hz = 0.0, ratio = 0.0;
        check(song.undo() && clip(tones)->warp.ratio == 1.0 && clip(tones)->warp.semitones == 12);
        controller.waitForWarpRenders();
        check(render(0.0, 3 * 48000));
        check(sounds_until(3 * 48000) < tone_frames);
        check(song.undo() && clip(tones)->warp == blokkily::ClipWarp{});
        check(render(0.0, 3 * 48000));
        tone_window(hz, ratio);
        check(std::abs(hz - 1000.0) <= 4.0);
        check(song.redo() && song.redo() && clip(tones)->warp.semitones == 12 &&
              clip(tones)->warp.ratio == 2.0);
        controller.waitForWarpRenders();
        check(render(0.0, 5 * 48000));
        tone_window(hz, ratio);
        check(std::abs(hz - 2000.0) <= 4.0 && sounds_until(5 * 48000) + 1 >= 2 * tone_frames - 240);
        reached("clip warp: undo and redo take the warp back and forth");
    }

    // 6. Saved and loaded, the clips keep their warp and play it again.
    {
        check(controller.saveProjectFile(project));
        std::ifstream saved(project.toStdString());
        std::stringstream text;
        text << saved.rdbuf();
        check(text.str().find("clipwarp " + std::to_string(clicks) + " 1 100 1 0 0\n") !=
                  std::string::npos &&
              text.str().find("clipwarp " + std::to_string(tones) + " 0 0 2 12 0\n") !=
                  std::string::npos);
        check(controller.loadProjectFile(project));
        settle(50);
        check(clip(clicks) != nullptr && clip(clicks)->warp.follow_tempo &&
              clip(clicks)->warp.source_bpm == 100.0 && clip(tones) != nullptr &&
              clip(tones)->warp.ratio == 2.0 && clip(tones)->warp.semitones == 12);
        controller.waitForWarpRenders();
        // Saved with the clicks muted and the tone heard: the other way round
        // to listen to the clicks, then both for the export.
        song.toggleMute(click_track);
        song.toggleMute(tone_track);
        check(render(0.0, 12 * 24000) && clicks_on_beats(stereo.data(), 12 * 24000));
        song.toggleMute(tone_track);
        reached("clip warp: the warp is saved and loaded");
    }

    // 7. The export is what the engine plays, warped clips included.
    {
        const QString bounce = QString::fromStdString((folder / "clip-warp-bounce.wav").string());
        check(controller.exportAudioFile(bounce, "FLOAT32"));
        std::string error;
        const auto read = blokkily::read_wave(bounce.toStdString(), &error);
        const auto samples = controller.engine() != nullptr ? controller.engine()->song_samples() : 0;
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
        reached("clip warp: the export is what was played");
    }

    // 8. The screenshot: the panel open on the tone, whose new cents are
    // rendering right now.
    if (ctx.transport.playing()) controller.togglePlayback();
    {
        auto* panel = open_panel(tones);
        check(panel != nullptr && panel->isVisible());
        type_into("warpCents", "50", false);
        check(clip(tones) != nullptr && clip(tones)->warp.cents == 50.0);
        controller.flushRecompile();
        check(song.audioClip(tones).value("rendering").toBool());
        auto* veil = ctx.named(QString("renderingVeil%1").arg(tones));
        check(veil != nullptr && veil->isVisible());
        auto* state = ctx.named("warpState");
        check(state != nullptr && state->property("text").toString() == "RENDERING…");
        reached("clip warp: screenshot state");
        check(ctx.save_screenshot());
        reached("screenshot");
        controller.waitForWarpRenders();
        check(!song.audioClip(tones).value("rendering").toBool());
    }
    std::ostringstream details;
    details << " | clips=" << song.song().audio_clips.size()
            << " | rendered=" << controller.warpRendered()
            << " | warp=" << controller.warpStatus().toStdString();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("clip_warp", run_clip_warp);

}  // namespace
}  // namespace blokkily::verify
