// features/audio_input.feature (plan item 3.2, "record everything" part 3), run
// on its own by the bdd_record_audio gate (`--scenario record_audio`). The
// deterministic device is given two inputs and a 128-frame round trip. An
// audio track is set to IN 1-2 and armed from the chips on its mixer strip,
// beside a CLAP track armed for notes; the input is heard while the song is
// stopped. With the song recording, a key is performed and then a burst is
// played into the inputs: the burst is heard as it is played, and when the
// song stops it is a clip on the audio track that sounds exactly where it was
// heard - 128 frames before it reached the input - read off what the
// production callback renders. One undo takes back the notes and the clip
// together. The take is written in the session's temporary folder and moves
// into <project>.audio/ on the first save; the reloaded project plays it, and
// the export read back from its file holds it on the same sample.

#include "verify/harness.hpp"

#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <source_location>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t block = 1024;
constexpr std::uint32_t round_trip = 128;
// The burst: 32 blocks of a 440 Hz cosine played into input 1 (and at half
// the level into input 2) from the start of block 10 of the pass, which runs
// for 80 blocks - most of the first bar.
constexpr std::uint64_t burst_start = 10 * block;
constexpr std::uint64_t burst_frames = 32 * block;
constexpr int pass_blocks = 80;
constexpr float burst_level = 0.4F;

float burst(std::uint32_t channel, std::uint64_t song_sample) {
    if (song_sample < burst_start || song_sample >= burst_start + burst_frames) return 0.0F;
    const double t = static_cast<double>(song_sample - burst_start) / 48000.0;
    const float level = channel == 0 ? burst_level : burst_level / 2.0F;
    return level * static_cast<float>(std::cos(2.0 * std::numbers::pi * 440.0 * t));
}

void run_record_audio(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok,
                              std::source_location where = std::source_location::current()) {
        if (!ok) std::cerr << "record audio: check failed at line " << where.line() << '\n';
        ctx.check(ok);
    };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto settle = [](int milliseconds) { VerifyContext::settle(milliseconds); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto text_of = [&ctx](const QString& name) {
        auto* item = ctx.named(name);
        return item == nullptr ? QString() : item->property("text").toString();
    };
    const auto lit = [&ctx](const QString& name) {
        auto* chip = ctx.named(name);
        return chip != nullptr && chip->property("on").toBool();
    };
    const auto click_named = [&ctx, &lay_out](const QString& name, Qt::MouseButton button) {
        auto* chip = ctx.named(name);
        if (chip != nullptr) ctx.click_at(chip, {chip->width() / 2, chip->height() / 2}, button);
        lay_out();
        return chip != nullptr;
    };
    auto* const output = ctx.output;
    check(output != nullptr);
    if (output == nullptr) {
        ctx.finish(" | no deterministic device");
        return;
    }

    // One block of the production callback with `inject` played into the
    // inputs, as the two halves of the bus.
    struct Block {
        std::vector<float> left, right;
        std::uint64_t first = 0;   // the song sample the block started at
    };
    const auto pump_in = [&](const std::function<float(std::uint32_t, std::uint64_t)>& inject) {
        controller.flushRecompile();
        Block out;
        auto* engine = controller.engine();
        out.first = engine == nullptr ? 0 : engine->sample_position();
        std::vector<float> stereo(2 * block, 0.0F);
        std::vector<float> injected;
        if (inject) {
            injected.resize(output->input_channels() * block);
            for (std::uint32_t channel = 0; channel < output->input_channels(); ++channel)
                for (std::size_t frame = 0; frame < block; ++frame)
                    injected[channel * block + frame] = inject(channel, out.first + frame);
        }
        if (!output->pump(stereo, injected)) return out;
        out.left.assign(stereo.begin(), stereo.begin() + block);
        out.right.assign(stereo.begin() + block, stereo.end());
        return out;
    };
    const auto peak = [](const std::vector<float>& samples) {
        float loudest = -1.0F;
        for (const float sample : samples) loudest = std::max(loudest, std::abs(sample));
        return loudest;
    };
    // Plays the song from the start with nothing at the inputs and returns the
    // first song sample the right side carries anything.
    const auto onset_on_right = [&](std::size_t blocks) -> std::int64_t {
        controller.rewindPlayback();
        if (controller.engine() != nullptr && !controller.engine()->is_playing())
            controller.togglePlayback();
        std::int64_t found = -1;
        for (std::size_t index = 0; index < blocks && found < 0; ++index) {
            const auto played = pump_in({});
            for (std::size_t frame = 0; frame < played.right.size(); ++frame)
                if (std::abs(played.right[frame]) > 1e-3F) {
                    found = static_cast<std::int64_t>(played.first + frame);
                    break;
                }
        }
        if (controller.engine() != nullptr && controller.engine()->is_playing())
            controller.togglePlayback();
        return found;
    };

    // The device: two inputs and a round trip of 128 frames.
    output->model_input(2, round_trip, false);
    // The CLAP instrument on track 0, hard left; an audio track, hard right.
    check(controller.verifyClap(parser.value("clap-fixture")));
    controller.setTempo(120.0);
    // Only the CLAP track: the session's other default tracks go.
    while (song.trackCount() > 1) check(song.deleteTrack(song.trackCount() - 1));
    const int audio = controller.addAudioTrack();
    check(audio == 1);
    song.setTrackPan(0, -1.0);
    song.setTrackPan(1, 1.0);
    song.setTrackGain(0, 0.0);
    song.setTrackGain(1, 0.0);
    controller.updateAudioInputs();
    check(song.audioInputChannels() == 2);
    check(window->setProperty("view", "ALL"));
    settle(20);
    lay_out();
    check(controller.engine() != nullptr && controller.engine()->track_count() == 2);
    reached("record audio: a CLAP track, an audio track, and two device inputs");

    // The strip's input and monitor chips are there and big enough to use.
    check(VerifyContext::usable(ctx.named("audioInput1"), 60, 18) &&
          VerifyContext::usable(ctx.named("monitor1"), 60, 18));
    check(VerifyContext::usable(ctx.named("arm1"), 18, 18) &&
          VerifyContext::usable(ctx.named("mixerStrip1"), 150, 170));
    check(text_of("audioInput1") == "MIDI" && text_of("monitor1") == "MON AUTO");
    const bool could_undo = song.canUndo();
    // Clicked to IN 1-2, right-clicked back to MIDI, and clicked again.
    check(click_named("audioInput1", Qt::LeftButton) && text_of("audioInput1") == "IN 1-2");
    check(click_named("audioInput1", Qt::RightButton) && text_of("audioInput1") == "MIDI");
    check(click_named("audioInput1", Qt::LeftButton) && text_of("audioInput1") == "IN 1-2");
    // The monitor steps AUTO, MON, MON OFF and round.
    check(click_named("monitor1", Qt::LeftButton) && text_of("monitor1") == "MON");
    check(click_named("monitor1", Qt::LeftButton) && text_of("monitor1") == "MON OFF");
    check(click_named("monitor1", Qt::LeftButton) && text_of("monitor1") == "MON AUTO");
    // Both armed from their R: track 0 for notes, track 1 for its inputs.
    check(click_named("arm0", Qt::LeftButton) && click_named("arm1", Qt::LeftButton));
    check(lit("arm0") && lit("arm1") && lit("audioInput1") && lit("monitor1"));
    const auto& input = song.song().tracks.at(1).input;
    check(input.armed && input.source == TrackInput::Source::audio &&
          input.audio_first_channel == 0 && input.audio_channels == 2);
    // An audio track takes no notes: the keyboard plays track 0 alone.
    check(controller.midiInput().routes()[0] == track_bit(0));
    // None of it is history.
    check(song.canUndo() == could_undo && !song.canRedo());
    const auto route = controller.engine()->audio_input(1);
    check(route.channels == 2 && route.first_channel == 0 && route.capture && route.monitor);
    reached("record audio: IN 1-2 chosen, monitored and armed from the strip");

    // Stopped, the armed input is heard: input 1 and 2 on the right.
    const auto steady = [](std::uint32_t channel, std::uint64_t) {
        return channel == 0 ? 0.25F : -0.125F;
    };
    (void)pump_in(steady);
    const auto stopped = pump_in(steady);
    check(!controller.engine()->is_playing());
    // The pair is the track's left and right; panned hard right, the bus's
    // right side carries input 2 at unity and its left side nothing.
    const float right_heard = stopped.right.empty() ? 1.0F : stopped.right.back();
    const float left_heard = stopped.left.empty() ? 1.0F : stopped.left.back();
    check(std::abs(right_heard - (-0.125F)) < 1e-5F && std::abs(left_heard) < 1e-5F);
    reached("record audio: the armed input is heard while the song is stopped");

    // Record: from the start of the song, a performed key on track 0, then
    // the burst into the inputs.
    const auto pattern_events = [&song] {
        return song.song().patterns.at(0).pattern.events().size();
    };
    const auto events_before = pattern_events();
    controller.toggleRecord();
    controller.rewindPlayback();
    controller.togglePlayback();
    check(controller.recordingLive());
    for (int index = 0; index < 3; ++index) (void)pump_in(burst);
    check(controller.performKey(60, true));
    for (int index = 0; index < 3; ++index) (void)pump_in(burst);
    controller.releasePerformed();
    float monitored = 0.0F;
    for (int index = 6; index < pass_blocks; ++index) {
        const auto played = pump_in(burst);
        check(played.first == static_cast<std::uint64_t>(index) * block);
        monitored = std::max(monitored, peak(played.right));
    }
    // Heard as it was played: the right side carries input 2 at its level.
    check(std::abs(monitored - burst_level / 2.0F) < 1e-3F);
    settle(20);
    controller.togglePlayback();
    controller.toggleRecord();
    settle(40);
    lay_out();
    reached("record audio: a key and a burst played while recording");

    // The take is a clip on the audio track, in the session's temporary
    // folder, placed a round trip earlier than the input received it.
    const auto& clips = song.song().audio_clips;
    check(clips.size() == 1);
    const AudioClip recorded = clips.empty() ? AudioClip{} : clips.front();
    const auto file = recorded.file < song.song().audio_files.size()
                          ? song.song().audio_files[recorded.file] : AudioFileRef{};
    const fs::path temporary = controller.recordingDirectory();
    check(recorded.track == 1 && file.channels == 2 && file.sample_rate == 48000);
    check(file.path.parent_path() == temporary && fs::exists(file.path));
    check(fs::path(QDir::tempPath().toStdString()) == temporary.parent_path());
    check(controller.takeCompensation() == round_trip);
    check(recorded.start == 0 && recorded.offset_frames == round_trip);
    check(pattern_events() > events_before);
    check(controller.audioTakeStatus() == "1 audio take recorded" &&
          controller.droppedInputFrames() == 0);
    // Heard back from the production callback: the burst sounds 128 frames
    // before it reached the input, to the sample.
    const auto expected = static_cast<std::int64_t>(burst_start) - round_trip;
    const auto onset = onset_on_right(14);
    check(onset == expected);
    lay_out();
    auto* clip_item = ctx.named(QString("audioClip%1").arg(recorded.id));
    check(VerifyContext::usable(clip_item, 20, 12));
    reached("record audio: the take is a clip that sounds where it was heard");

    // One undo takes back the notes and the clip together; redo brings both.
    check(song.undo());
    check(song.song().audio_clips.empty() && pattern_events() == events_before);
    check(song.redo());
    check(song.song().audio_clips.size() == 1 && pattern_events() > events_before);
    reached("record audio: one undo removes the notes and the clip");

    // Saved, the take moves into <project>.audio/; history follows it.
    const QString project = parser.value("project");
    const fs::path project_file = project.toStdString();
    const fs::path audio_folder =
        project_file.parent_path() / (project_file.stem().string() + ".audio");
    std::error_code ignored;
    fs::create_directories(project_file.parent_path(), ignored);
    fs::remove_all(audio_folder, ignored);
    check(!project.isEmpty() && controller.saveProjectFile(project));
    const auto saved_path = song.song().audio_files.empty() ? fs::path{}
                                                            : song.song().audio_files.front().path;
    check(saved_path.parent_path() == audio_folder && fs::exists(saved_path));
    check(!fs::exists(file.path) && !fs::exists(temporary));
    check(song.undo() && song.redo());
    check(!song.song().audio_files.empty() &&
          song.song().audio_files.front().path == saved_path);
    check(controller.recordingDirectory() == audio_folder);
    // Loaded again, the clip plays its file from the project's folder.
    check(controller.loadProjectFile(project));
    settle(40);
    check(song.song().audio_clips.size() == 1 && !song.song().audio_files.empty() &&
          song.song().audio_files.front().path == saved_path);
    check(song.missingAudioFiles() == 0);
    check(onset_on_right(14) == expected);
    reached("record audio: the take moves into the project's audio folder on save");

    // The export, read back: the take on the same sample, and nothing of
    // the inputs, which were being monitored while it rendered.
    const QString exported = parser.value("export");
    check(!exported.isEmpty() && controller.exportAudioFile(exported));
    const auto wave = read_wave(exported.toStdString());
    check(wave.has_value() && wave->channels == 2);
    std::int64_t exported_onset = -1;
    if (wave)
        for (std::uint64_t frame = 0; frame < wave->frames; ++frame)
            if (std::abs(wave->interleaved[frame * 2 + 1]) > 1e-3F) {
                exported_onset = static_cast<std::int64_t>(frame);
                break;
            }
    check(exported_onset == expected);
    reached("record audio: the export holds the take on the same sample");

    // Left with the armed audio track selected, its clip in the lane.
    song.selectTrack(1);
    controller.rewindPlayback();
    settle(40);
    lay_out();
    reached("record audio: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | take onset " << onset << " (want " << expected << ") | monitored "
            << monitored << " | export onset " << exported_onset;
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("record_audio", run_record_audio);

}  // namespace
}  // namespace blokkily::verify
