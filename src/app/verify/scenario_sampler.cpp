// features/sampler.feature (item 2.3), run on its own by the bdd_sampler gate
// (`--scenario sampler`). The sampler is chosen in the rendered browser,
// played through the production render callback, edited from its rendered
// panel while the song runs — the same engine plays on, with no rebuild and
// no recompile — undone and redone, turned into a kit by chopping a loop into
// pads that sound at their steps, saved with its samples beside the project,
// loaded, and bounced; the bounce is read back and compared with what the
// callback renders. The audio fixtures come from blokkily_make_audio_fixtures
// (BLOKKILY_AUDIO_FIXTURES), never from files installed on the machine.

#include "verify/harness.hpp"

#include "../../tests/support/audio_probe.hpp"

#include "blokkily/audio/wave_file.hpp"
#include "blokkily/instruments/sampler_program.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

constexpr double rate = 48000.0;
constexpr std::size_t block = 1024;
// 120 BPM and 480 ticks a beat at 48 kHz: one tick is 50 samples, a beat is
// 24000 and a bar 96000.
constexpr std::uint64_t beat = 24000;
constexpr std::uint64_t bar = 96000;
// tones8: hit i is 30 ms of 200 * (i + 1) Hz, one every 6000 frames.
constexpr std::size_t hit_length = 1440;

struct Heard {
    std::vector<float> left;
    std::size_t jumps = 0;   // blocks after which the playhead was not where it should be
    std::span<const float> window(std::size_t from, std::size_t length) const {
        if (from >= left.size()) return {};
        return std::span<const float>(left).subspan(from, std::min(length, left.size() - from));
    }
};

double dominant(std::span<const float> samples, double low = 100.0, double high = 2000.0,
                double step = 2.0) {
    return probe::dominant_frequency(samples, rate, low, high, step);
}

bool near(double value, double wanted, double tolerance) {
    return std::abs(value - wanted) <= tolerance;
}

void run_sampler(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    std::ostringstream details;

    // The production callback, `frames` of it, a block at a time; while the
    // song plays each block must leave the playhead exactly a block later.
    const auto listen = [&](std::size_t frames) {
        Heard heard;
        std::vector<float> stereo(block * 2);
        for (std::size_t done = 0; done < frames; done += block) {
            // As the harness's pump does: an edit's recompile, if it asked
            // for one, has been made by the time the device runs.
            controller.flushRecompile();
            auto* engine = controller.engine();
            const auto before = engine != nullptr ? engine->sample_position() : 0;
            std::fill(stereo.begin(), stereo.end(), 0.0F);
            if (ctx.output == nullptr || !ctx.output->pump(stereo)) {
                heard.jumps += 1000;
                return heard;
            }
            if (engine != nullptr && engine->is_playing() &&
                engine->sample_position() != (before + block) % std::max<std::uint64_t>(
                                                                    1, engine->song_samples()))
                ++heard.jumps;
            const auto take = std::min(block, frames - done);
            heard.left.insert(heard.left.end(), stereo.begin(),
                              stereo.begin() + static_cast<std::ptrdiff_t>(take));
        }
        return heard;
    };
    const auto stop = [&] {
        if (controller.engine() != nullptr && controller.engine()->is_playing())
            controller.togglePlayback();
        // Whatever still rings (a release, a one-shot pad) is let finish.
        (void)listen(block * 12);
    };
    const auto play_from = [&](std::uint64_t tick) {
        stop();
        controller.seekToTick(static_cast<double>(tick));
        controller.togglePlayback();
    };

    const QString fixture_dir = qEnvironmentVariable("BLOKKILY_AUDIO_FIXTURES");
    const QString keys_file = fixture_dir + "/smpl_loop_44k1_pcm16.wav";
    check(QFileInfo::exists(keys_file));
    // The loop the pads are chopped from lives beside the project, so the
    // saved project names it relative to its own folder.
    const QString project = parser.value("project");
    check(!project.isEmpty());
    const QString project_dir = QFileInfo(project).absolutePath();
    // Where the whole project folder is moved before it is opened again.
    const QString moved_dir = project_dir + "-moved";
    QDir(project_dir).removeRecursively();
    QDir(moved_dir).removeRecursively();
    const QString pads_file = project_dir + "/samples/tones8_48k_pcm16.wav";
    QDir{}.mkpath(project_dir + "/samples");
    check(QFile::copy(fixture_dir + "/tones8_48k_pcm16.wav", pads_file));

    // A song made for the check: key 69 on every beat of bar 1 on KEYS, and
    // on PADS in bar 2 key 36 on the downbeat and key 40 a beat later.
    {
        blokkily::Song fresh;
        fresh.patterns = {{"KEYS", blokkily::Pattern(1920, 480)},
                          {"PADS", blokkily::Pattern(1920, 480)}};
        const auto note = [](blokkily::Pattern& target, blokkily::Tick start,
                             blokkily::Tick length, int key) {
            blokkily::Trigger trigger;
            trigger.start = start;
            trigger.duration = length;
            trigger.musical_data = blokkily::Note{static_cast<std::int16_t>(key), 1.0F, 0.0F};
            (void)target.add(trigger);
        };
        for (blokkily::Tick at = 0; at < 1920; at += 480)
            note(fresh.patterns[0].pattern, at, 400, 69);
        note(fresh.patterns[1].pattern, 0, 120, 36);
        note(fresh.patterns[1].pattern, 480, 120, 40);
        fresh.tracks = {{"KEYS", {}, {}}, {"PADS", {}, {}}};
        fresh.clips = {{0, 0, 0, 1}, {1, 1, 1920, 1}};
        song.replace(std::move(fresh));
        pattern.refresh();
    }
    check(controller.engine() == nullptr);
    check(window->setProperty("view", "ALL"));
    lay_out();
    reached("sampler: a song with no instrument");

    // ------------------------------------------------------------------
    // Scenario: The sampler is chosen in the browser like any instrument.
    const auto choose = [&](const QString& name) {
        const auto rows = controller.browserPlugins();
        for (int row = 0; row < rows.size(); ++row)
            if (rows.at(row).toMap().value("name").toString() == name) {
                auto* item = ctx.named(QString("browserRow%1").arg(row));
                if (item == nullptr) return false;
                ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
                lay_out();
                return true;
            }
        return false;
    };
    song.selectTrack(0);
    lay_out();
    check(ctx.named("samplerPanel") == nullptr ||
          !ctx.named("samplerPanel")->isVisible());
    check(choose("Sampler"));
    check(song.song().tracks.at(0).instrument.format == "Sampler" &&
          song.song().tracks.at(0).instrument.identifier == "keyed");
    check(controller.engine() != nullptr);
    auto* panel = ctx.named("samplerPanel");
    check(panel != nullptr && panel->isVisible());
    check(VerifyContext::usable(panel, 180, 220));
    // A sampler has no window of its own: its panel is its editor, so the
    // plugin EDITOR bar (item 2.6) gives it the room instead.
    check(ctx.named("editorBar") == nullptr || !ctx.named("editorBar")->isVisible());
    check(controller.activeInstrument() == "SMP · KEYS");
    reached("sampler: chosen in the browser");

    // A sample is loaded (what the LOAD button's file dialog hands over): its
    // root key and loop come from the file.
    check(controller.loadSamplerSample(keys_file));
    lay_out();
    auto view = controller.sampler();
    check(view.value("sample").toString() == "smpl_loop_44k1_pcm16.wav" &&
          view.value("rootKey").toInt() == 57 && view.value("loop").toString() == "forward" &&
          view.value("zones").toInt() == 1 && !view.value("missing").toBool());
    auto* root_key = ctx.named("samplerRootKey");
    check(root_key != nullptr && root_key->property("value").toInt() == 57 &&
          root_key->property("displayText").toString() == "A3");
    check(VerifyContext::usable(root_key, 40, 20));
    reached("sampler: a sample brings its root key and loop");

    // Scenario: A loaded sample plays at the pitch of the key.
    play_from(0);
    const auto opening = listen(6 * block);
    const double opening_hz = dominant(opening.window(2048, 4096));
    check(near(opening_hz, 880.0, 4.0) && opening.jumps == 0);
    details << " | key69=" << opening_hz;
    reached("sampler: key 69 on a file rooted at 57 plays 880 Hz");

    // ------------------------------------------------------------------
    // Scenario: A root-key edit reaches the playing sampler without a
    // rebuild. The rendered stepper is pressed twelve times while the song
    // runs.
    auto* const engine_before = controller.engine();
    const int rebuilds_before = controller.rebuildCount();
    const int recompiles_before = controller.recompileCount();
    const bool could_undo = song.canUndo();
    QObject* up = root_key != nullptr ? root_key->property("up").value<QObject*>() : nullptr;
    auto* up_button = up != nullptr ? up->property("indicator").value<QQuickItem*>() : nullptr;
    check(VerifyContext::usable(up_button, 10, 18));
    for (int press = 0; press < 12 && up_button != nullptr; ++press)
        ctx.click_at(up_button, {up_button->width() / 2, up_button->height() / 2},
                     Qt::LeftButton);
    lay_out();
    view = controller.sampler();
    check(view.value("rootKey").toInt() == 69 && root_key != nullptr &&
          root_key->property("value").toInt() == 69);
    // Heard from where the song was, through the next beat after the edit.
    // `until` is how far a beat of the song lies from the playhead.
    const auto until = [&](std::uint64_t sample) -> std::size_t {
        const auto now = controller.engine() != nullptr ? controller.engine()->sample_position()
                                                        : sample;
        return sample > now ? static_cast<std::size_t>(sample - now) : 0;
    };
    const std::size_t next_note = until(beat);
    const auto after_edit = listen(next_note + 8 * block);
    const double held_hz = dominant(after_edit.window(0, 4096));
    const double edited_hz = dominant(after_edit.window(next_note + 2048, 4096));
    const bool same_engine = controller.engine() == engine_before &&
                             controller.rebuildCount() == rebuilds_before &&
                             controller.recompileCount() == recompiles_before;
    check(same_engine && after_edit.jumps == 0);
    // The note already sounding keeps its sound; the next one has the edit.
    check(near(held_hz, 880.0, 4.0) && near(edited_hz, 440.0, 4.0));
    if (!same_engine || after_edit.jumps != 0 || !near(edited_hz, 440.0, 4.0))
        std::cerr << "root-key edit: engine " << (controller.engine() == engine_before)
                  << " rebuilds " << rebuilds_before << "->" << controller.rebuildCount()
                  << " recompiles " << recompiles_before << "->" << controller.recompileCount()
                  << " jumps " << after_edit.jumps << " held " << held_hz << " next "
                  << edited_hz << '\n';
    details << " | edited=" << edited_hz;
    reached("sampler: a root-key edit is heard without a rebuild or recompile");

    // Scenario: Twelve presses are one step of history. Undo gives the
    // running sampler its earlier program back, still in the same engine.
    check(song.canUndo() && could_undo);
    check(song.undo());
    lay_out();
    view = controller.sampler();
    check(view.value("rootKey").toInt() == 57 && root_key->property("value").toInt() == 57);
    // The note on the next beat plays the program the undo gave back.
    const std::size_t undone_note = until(2 * beat);
    const auto undone = listen(undone_note + 8 * block);
    const double undone_hz = dominant(undone.window(undone_note + 2048, 4096));
    check(near(undone_hz, 880.0, 4.0) && undone.jumps == 0 &&
          controller.engine() == engine_before && controller.rebuildCount() == rebuilds_before);
    check(song.redo());
    lay_out();
    check(controller.sampler().value("rootKey").toInt() == 69);
    const std::size_t redone_note = until(3 * beat);
    const auto redone = listen(redone_note + 8 * block);
    const double redone_hz = dominant(redone.window(redone_note + 2048, 4096));
    check(near(redone_hz, 440.0, 4.0) && controller.engine() == engine_before);
    details << " | undone=" << undone_hz << " redone=" << redone_hz;
    reached("sampler: undo and redo reach the running sampler");
    stop();

    // ------------------------------------------------------------------
    // Scenario: A loop is chopped into pads from the panel.
    song.selectTrack(1);
    lay_out();
    check(!panel->isVisible());
    check(choose("Drum Sampler"));
    check(song.song().tracks.at(1).instrument.identifier == "kit" && panel->isVisible());
    check(controller.loadSamplerSample(pads_file));
    lay_out();
    view = controller.sampler();
    check(view.value("mode").toString() == "kit" && view.value("zones").toInt() == 1 &&
          view.value("lowKey").toInt() == 36 && view.value("oneShot").toBool());
    auto* slices = ctx.named("samplerSliceCount");
    auto* chop = ctx.named("samplerChop");
    check(slices != nullptr && slices->property("value").toInt() == 8);
    check(VerifyContext::usable(chop, 30, 18) && VerifyContext::usable(slices, 40, 20));
    if (chop != nullptr) ctx.click_at(chop, {chop->width() / 2, chop->height() / 2}, Qt::LeftButton);
    lay_out();
    view = controller.sampler();
    check(view.value("zones").toInt() == 8 && view.value("mode").toString() == "kit");
    // The panel steps through the pads: the second answers C#2 alone.
    if (auto* next = ctx.named("samplerZoneNext"))
        ctx.click_at(next, {next->width() / 2, next->height() / 2}, Qt::LeftButton);
    lay_out();
    view = controller.sampler();
    check(view.value("zone").toInt() == 1 && view.value("lowKey").toInt() == 37 &&
          view.value("highKey").toInt() == 37);
    check(ctx.named("samplerZone") != nullptr &&
          ctx.named("samplerZone")->property("text").toString() == "2/8");
    reached("sampler: chopped into eight pads from the panel");

    // Scenario: The pads sound their slices at their steps.
    const auto pads_at = [&](const char* when) {
        play_from(1920);
        const auto heard = listen(beat + 2 * hit_length + block);
        stop();
        const double low = dominant(heard.window(0, hit_length), 100.0, 1800.0, 10.0);
        const double high = dominant(heard.window(beat, hit_length), 100.0, 1800.0, 10.0);
        const bool right = near(low, 200.0, 20.0) && near(high, 1000.0, 20.0) &&
                           heard.jumps == 0 && probe::rms(heard.window(0, hit_length)) > 0.01 &&
                           probe::rms(heard.window(beat, hit_length)) > 0.01;
        details << " | pads(" << when << ")=" << low << "," << high;
        return right;
    };
    check(pads_at("chopped"));
    reached("sampler: pad 36 plays 200 Hz on the downbeat and pad 40 1000 Hz a beat later");

    // ------------------------------------------------------------------
    // Scenario: A session with samplers is saved and loaded.
    check(controller.saveProjectFile(project));
    const auto saved_pads = blokkily::parse_sampler(song.song().tracks.at(1).instrument.state);
    check(saved_pads && saved_pads->zones.size() == 8 &&
          saved_pads->zones.front().sample == "samples/tones8_48k_pcm16.wav");
    const auto saved_keys = blokkily::parse_sampler(song.song().tracks.at(0).instrument.state);
    check(saved_keys && saved_keys->zones.size() == 1 && saved_keys->zones.front().root_key == 69);
    song.undo();   // the file, not the song in memory, has to bring it back
    // The folder is moved, samples and all, and opened from where it is now:
    // the loop is found beside the project, not where it was saved.
    check(QDir{}.rename(project_dir, moved_dir));
    check(controller.loadProjectFile(moved_dir + "/" + QFileInfo(project).fileName()));
    lay_out();
    check(controller.engine() != nullptr && song.song().tracks.size() == 2);
    song.selectTrack(0);
    lay_out();
    check(controller.sampler().value("rootKey").toInt() == 69 &&
          controller.sampler().value("missingSamples").toStringList().isEmpty());
    play_from(0);
    const auto reloaded = listen(8 * block);
    const double reloaded_hz = dominant(reloaded.window(2048, 4096));
    check(near(reloaded_hz, 440.0, 4.0));
    stop();
    song.selectTrack(1);
    lay_out();
    check(controller.sampler().value("zones").toInt() == 8 &&
          controller.sampler().value("missingSamples").toStringList().isEmpty());
    check(pads_at("loaded"));
    details << " | reloaded=" << reloaded_hz;
    reached("sampler: save, move the folder and load, samples found beside the project");

    // ------------------------------------------------------------------
    // Scenario: A bounce is the mix the callback plays, read back.
    const QString bounce = parser.value("export");
    check(!bounce.isEmpty() && controller.exportAudioFile(bounce, "FLOAT32"));
    std::string error;
    const auto written = blokkily::read_wave(bounce.toStdString(), &error);
    auto* engine = controller.engine();
    const std::uint64_t song_frames = engine != nullptr ? engine->song_samples() : 0;
    check(written && song_frames == 2 * bar && written->channels == 2 &&
          written->sample_rate == 48000 && written->frames > song_frames);
    if (written && song_frames == 2 * bar) {
        play_from(0);
        const auto played = listen(song_frames);
        stop();
        std::size_t differing = 0;
        float worst = 0.0F;
        for (std::size_t frame = 0; frame < song_frames && frame < played.left.size(); ++frame) {
            const float delta = std::abs(written->interleaved[frame * 2] - played.left[frame]);
            worst = std::max(worst, delta);
            if (delta > 1e-6F) ++differing;
        }
        check(played.left.size() == song_frames && differing == 0);
        std::vector<float> left(written->frames);
        for (std::size_t frame = 0; frame < left.size(); ++frame)
            left[frame] = written->interleaved[frame * 2];
        const std::span<const float> file(left);
        const double keys_hz = dominant(file.subspan(2048, 4096));
        const double pad_low = dominant(file.subspan(bar, hit_length), 100.0, 1800.0, 10.0);
        const double pad_high =
            dominant(file.subspan(bar + beat, hit_length), 100.0, 1800.0, 10.0);
        check(near(keys_hz, 440.0, 4.0) && near(pad_low, 200.0, 20.0) &&
              near(pad_high, 1000.0, 20.0));
        details << " | bounce differing=" << differing << " worst=" << worst;
    }
    reached("sampler: the bounce read back matches the render");

    // Left on the kit, its second pad in the panel, for the screenshot.
    song.selectTrack(1);
    (void)controller.selectSamplerZone(1);
    controller.seekToTick(0);
    lay_out();
    panel = ctx.named("samplerPanel");
    check(panel != nullptr && panel->isVisible() && VerifyContext::usable(panel, 180, 220));
    // The browser under the panel is squeezed, not crushed.
    // Two rows of it at least, so another instrument can still be chosen.
    check(VerifyContext::usable(ctx.named("pluginBrowser"), 150, 80));
    if (panel != nullptr)
        details << " | panel=" << panel->width() << "x" << panel->height();
    if (auto* browser = ctx.named("pluginBrowser"))
        details << " | browser=" << browser->width() << "x" << browser->height();
    reached("sampler: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("sampler", run_sampler);

}  // namespace
}  // namespace blokkily::verify
