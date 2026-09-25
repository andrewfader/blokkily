// features/metronome.feature, run on its own by the bdd_metronome gate
// (`--scenario metronome`). Everything a producer touches is the rendered
// window: the transport's CLICK chip, its count-in chip and level slider, the
// play and record buttons, the mute on a mixer strip, and the export's click
// option. What is heard is read from the production render callback through
// the deterministic device, on the real CLAP fixture (which sounds 0.25 DC
// while a note is held). At 120 BPM and 48 kHz a beat is 24000 samples, and
// the click starts at its loudest: a downbeat at 0 dB is 1.0, another beat
// 0.5, so at the default -6 dB they open at 0.501 and 0.251.

#include "verify/harness.hpp"

#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace blokkily::verify {
namespace {

constexpr std::size_t beat_samples = 24000;
// A click at -6 dB opens at these.
const float downbeat_level = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
const float beat_level = downbeat_level * 0.5F;

// One 4/4 pattern with a note on the second eighth of the bar, so every click
// and every note are apart; played four times from bar 1 on one track.
blokkily::Song clean_song() {
    blokkily::Song song;
    blokkily::Pattern verse(1920, 480);
    blokkily::Trigger trigger;
    trigger.start = 240;
    trigger.duration = 96;
    trigger.musical_data = blokkily::Note{60, 0.9F, 0.0F};
    (void)verse.add(trigger);
    song.patterns = {{"VERSE", std::move(verse)}};
    song.tracks = {blokkily::Track{}};
    song.tracks[0].name = "LEAD";
    song.tracks[0].mix.pan = -1.0;
    song.clips = {{0, 0, 0, 4}};
    return song;
}

const Trigger* trigger_on(const Pattern& pattern, int step) {
    for (const auto& trigger : pattern.events())
        if (trigger.start / PatternModel::ticks_per_step == step) return &trigger;
    return nullptr;
}

// Whether `samples` open a click of `level` at each of `beats` (offsets): the
// signal steps up by the click's first sample there, out of a signal that was
// steady (silence, or a held note's DC) on the two samples before.
bool clicks_at(const std::vector<float>& samples, const std::vector<std::size_t>& beats,
               const std::vector<float>& levels) {
    for (std::size_t index = 0; index < beats.size(); ++index) {
        const auto at = beats[index];
        const float before = at > 0 && at <= samples.size() ? samples[at - 1] : 0.0F;
        const bool steady = at < 2 || (at <= samples.size() && samples[at - 2] == before);
        const bool ok = at < samples.size() && steady &&
                        std::abs(samples[at] - before - levels[index]) <= 1e-4F;
        if (!ok) {
            std::cerr << "no click of " << levels[index] << " at " << at << " of "
                      << samples.size() << ':';
            for (std::size_t near = at > 2 ? at - 2 : 0; near < at + 3 && near < samples.size();
                 ++near)
                std::cerr << ' ' << samples[near];
            std::cerr << '\n';
            return false;
        }
    }
    return true;
}

void run_metronome(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& transport = ctx.transport;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto settle = [](int milliseconds) { VerifyContext::settle(milliseconds); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto text_of = [&ctx](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        return item == nullptr ? QString() : item->property("text").toString();
    };
    const auto press = [&ctx](const char* name, Qt::MouseButton button = Qt::LeftButton) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, button);
        return true;
    };
    // The left channel of the production callback, 1024 frames a block;
    // `each` runs before block n is pumped.
    std::vector<float> stereo(2048, 0.0F);
    const auto listen = [&](std::size_t blocks, const std::function<void(std::size_t)>& each) {
        std::vector<float> left;
        for (std::size_t block = 0; block < blocks; ++block) {
            if (each) each(block);
            controller.flushRecompile();
            std::fill(stereo.begin(), stereo.end(), 0.0F);
            if (ctx.output == nullptr || !ctx.output->pump(stereo)) break;
            left.insert(left.end(), stereo.begin(), stereo.begin() + 1024);
        }
        return left;
    };

    // ------------------------------------------------------------------ setup
    song.replace(clean_song());
    check(controller.verifyClap(parser.value("clap-fixture")));
    check(controller.engine() != nullptr && song.song().tracks.at(0).instrument.format == "CLAP");
    song.setTrackPan(0, -1.0);
    song.setTrackGain(0, 0.0);
    controller.setTempo(120.0);
    song.selectTrack(0);
    song.selectPattern(0);
    check(window->setProperty("view", "ALL"));
    controller.seekToBar(0);
    lay_out();
    check(!song.metronomeOn() && song.countInBars() == 0 && song.metronomeLevelDb() == -6.0);
    check(VerifyContext::usable(ctx.named("clickButton"), 40, 20) &&
          VerifyContext::usable(ctx.named("countInButton"), 40, 20) &&
          VerifyContext::usable(ctx.named("clickLevel"), 60, 12) &&
          VerifyContext::usable(ctx.named("exportClickButton"), 60, 14));
    check(text_of("countInButton") == "NO CI" && text_of("clickLevelReadout") == "-6.0");
    // Regression: the controls first pushed the end of the transport (the
    // event count and RESCAN PLUGINS) off the right of a 1280 px window. The
    // whole bar must still fit, its last control usable, and the metronome
    // must not squeeze the readouts beside it.
    {
        auto* rescan = ctx.named("rescanButton");
        const double right_edge =
            rescan != nullptr ? rescan->mapToScene({rescan->width(), 0}).x() : 1e9;
        const bool fits = VerifyContext::usable(rescan, 80, 20) && right_edge <= window->width();
        check(fits && VerifyContext::usable(ctx.named("tempoBox"), 80, 28) &&
              VerifyContext::usable(ctx.named("viewKEYS"), 30, 20));
        if (!fits)
            std::cerr << "REGRESSION: the transport runs off the window, RESCAN ends at "
                      << right_edge << " of " << window->width() << '\n';
    }
    reached("metronome: the transport offers CLICK, a count-in and a level, each usable");

    // ------------------------------------------- CLICK and the level slider
    check(press("clickButton"));
    lay_out();
    check(song.metronomeOn() && controller.engine() != nullptr &&
          controller.engine()->metronome_enabled());
    check(ctx.named("clickButton") != nullptr && ctx.named("clickButton")->property("on").toBool());
    // A press at the right end of the slider is its top, +6 dB.
    if (auto* slider = ctx.named("clickLevel"))
        ctx.click_at(slider, {slider->width() - 2.0, slider->height() / 2}, Qt::LeftButton);
    lay_out();
    const double pressed_level = song.metronomeLevelDb();
    check(pressed_level > 5.0 && text_of("clickLevelReadout").startsWith("+"));
    song.setMetronomeLevelDb(-6.0);
    lay_out();
    check(text_of("clickLevelReadout") == "-6.0");
    reached("metronome: CLICK and the level slider set the song's click");

    // -------------------------------- the click heard on the beats, muted or not
    controller.seekToBar(0);
    check(press("playButton"));
    check(transport.playing() && !controller.countingIn());
    const auto heard = listen(190, {});   // two bars and a little
    std::vector<std::size_t> beats;
    std::vector<float> levels;
    for (std::size_t beat = 0; beat < 8; ++beat) {
        beats.push_back(beat * beat_samples);
        levels.push_back(beat % 4 == 0 ? downbeat_level : beat_level);
    }
    const bool on_beats = clicks_at(heard, beats, levels);
    check(on_beats);
    // The note between the clicks is the song, at 0.25.
    check(heard.size() > 12100 && std::abs(heard[12100] - 0.25F) < 1e-4F);
    // The mute on the strip takes the song away, not the click.
    check(press("mute0"));
    lay_out();
    check(song.song().tracks.at(0).mix.mute);
    const auto muted = listen(190, {});
    const auto at_bar = static_cast<std::size_t>(controller.engine()->sample_position());
    (void)at_bar;
    // The block boundary lands the next bar's downbeat somewhere in this
    // window: find it from where the song is.
    bool muted_click = false;
    bool muted_note = false;
    for (std::size_t index = 1; index + 1 < muted.size(); ++index) {
        if (muted[index - 1] == 0.0F && std::abs(muted[index] - downbeat_level) < 1e-4F)
            muted_click = true;
        if (std::abs(muted[index] - 0.25F) < 1e-4F) muted_note = true;
    }
    check(muted_click && !muted_note);
    check(press("mute0"));
    check(press("playButton"));
    check(!transport.playing());
    (void)listen(2, {});
    if (!on_beats || !muted_click || muted_note)
        std::cerr << "click: first samples of beats "
                  << (heard.size() > beat_samples ? heard[beat_samples] : -1.0F)
                  << ", muted click " << muted_click << " muted note " << muted_note << '\n';
    reached("metronome: the click is heard on every beat, the downbeat louder, whatever the mute");

    // --------------------------------------------- a count-in into a take
    check(press("countInButton"));
    check(press("countInButton"));
    check(press("countInButton"));
    lay_out();
    check(song.countInBars() == 3 && text_of("countInButton") == "CI 3");
    check(press("countInButton", Qt::RightButton));
    lay_out();
    check(song.countInBars() == 2 && text_of("countInButton") == "CI 2");
    check(press("recordButton"));
    check(controller.recordArmed());
    controller.seekToBar(1);
    check(transport.position() == "2.1.1");
    auto* engine = controller.engine();
    check(engine != nullptr && engine->sample_position() == 96000);
    check(press("playButton"));
    // Two bars of count-in: 192000 samples, 187.5 blocks.
    bool waited = true;
    bool showed = false;
    const auto counted = listen(210, [&](std::size_t block) {
        if (block == 0) return;
        if (block <= 187) {
            waited = waited && engine->sample_position() == 96000 && engine->counting_in();
            if (block == 20) {
                controller.pollCountIn();
                lay_out();
                showed = controller.countingIn() && transport.position() == "2.1.1" &&
                         ctx.named("countInButton")->property("on").toBool();
            }
        }
        // A key played in the count-in's second bar and held into the song.
        if (block == 150) check(controller.performKey(64, true));
    });
    controller.releasePerformed();
    (void)listen(2, {});
    check(waited && showed);
    // Eight clicks, then the song from bar 2 (its note on the second eighth).
    std::vector<std::size_t> count_beats;
    std::vector<float> count_levels;
    for (std::size_t beat = 0; beat < 8; ++beat) {
        count_beats.push_back(beat * beat_samples);
        count_levels.push_back(beat % 4 == 0 ? downbeat_level : beat_level);
    }
    const bool counted_in = clicks_at(counted, count_beats, count_levels);
    check(counted_in);
    // The song's first beat after the count-in is a downbeat click at 192000.
    check(clicks_at(counted, {192000}, {downbeat_level}));
    // The key sounds from the block it was played in, through the song start.
    check(counted.size() > 192100 && counted[150 * 1024 + 100] > 0.2F &&
          counted[192000 + 2000] > 0.2F);
    check(!engine->counting_in() && engine->sample_position() > 96000);
    check(press("playButton"));
    settle(40);
    check(press("recordButton"));
    check(!controller.recordArmed());
    lay_out();
    const auto& verse = song.song().patterns.at(0).pattern;
    const auto* taken = trigger_on(verse, 0);
    const bool landed = taken != nullptr && taken->start == 0 && taken->micro_offset == 0 &&
                        std::holds_alternative<Note>(taken->musical_data) &&
                        std::get<Note>(taken->musical_data).key == 64;
    check(landed);
    check(ctx.rendered_step(0));
    if (!counted_in || !landed || !waited || !showed)
        std::cerr << "count-in: clicks " << counted_in << " waited " << waited << " showed "
                  << showed << " take on step 0 " << landed << '\n';
    reached("metronome: a two-bar count-in clicks 8 beats while the playhead waits on bar 2");
    reached("metronome: a key held into the song start is recorded on its first step");

    // Undo takes back the take, never the click's settings.
    song.setMetronomeLevelDb(-9.0);
    check(song.undo());
    check(trigger_on(song.song().patterns.at(0).pattern, 0) == nullptr);
    check(song.metronomeOn() && song.countInBars() == 2 && song.metronomeLevelDb() == -9.0);
    check(song.redo() && trigger_on(song.song().patterns.at(0).pattern, 0) != nullptr);
    song.setMetronomeLevelDb(-6.0);
    reached("metronome: undo takes back the take and leaves the click as set");

    // ------------------------------------------------ the export's option
    const QString export_path = parser.value("export");
    check(!export_path.isEmpty());
    const auto read_left = [](const QString& path) {
        std::vector<float> left;
        std::string error;
        const auto wave = blokkily::read_wave(path.toStdString(), &error);
        if (!wave || wave->channels != 2) return left;
        left.resize(static_cast<std::size_t>(wave->frames));
        for (std::size_t frame = 0; frame < left.size(); ++frame)
            left[frame] = wave->interleaved[frame * 2];
        return left;
    };
    check(!window->property("exportClick").toBool());
    check(controller.exportAudioFile(export_path, "FLOAT32",
                                     window->property("exportClick").toBool()));
    const auto plain = read_left(export_path);
    check(press("exportClickButton"));
    lay_out();
    check(window->property("exportClick").toBool() &&
          ctx.named("exportClickButton") != nullptr &&
          ctx.named("exportClickButton")->property("on").toBool());
    check(controller.exportAudioFile(export_path, "FLOAT32",
                                     window->property("exportClick").toBool()));
    const auto clicked = read_left(export_path);
    // The default bounce holds no click: on every beat the signal is steady
    // (a click is a ringing burst). The other bounce is the same song with
    // the click added: their difference is the click, on every beat.
    bool plain_quiet = plain.size() == clicked.size() && plain.size() > 200000;
    std::vector<float> difference(plain.size(), 0.0F);
    for (std::size_t frame = 0; plain_quiet && frame < plain.size(); ++frame)
        difference[frame] = clicked[frame] - plain[frame];
    for (const auto beat : beats)
        plain_quiet = plain_quiet && plain[beat] == plain[beat + 1] && plain[beat] == plain[beat + 7];
    const bool click_there = plain_quiet && clicks_at(difference, beats, levels);
    check(plain_quiet && click_there);
    check(plain.size() == clicked.size() && plain.size() > 12100 && plain[12100] == clicked[12100] &&
          std::abs(plain[12100] - 0.25F) < 1e-4F);
    check(controller.engine() != nullptr && controller.engine()->metronome_enabled());
    if (!plain_quiet || !click_there)
        std::cerr << "export: plain quiet " << plain_quiet << " click there " << click_there
                  << '\n';
    check(press("exportClickButton"));
    reached("metronome: the bounce leaves the click out unless + CLICK is lit");

    // ----------------------------------------------------- save and load
    const QString project_path = parser.value("project");
    check(!project_path.isEmpty() && controller.saveProjectFile(project_path));
    QFile saved(project_path);
    check(saved.open(QIODevice::ReadOnly) &&
          QString::fromUtf8(saved.readAll()).contains("metronome on -6 2\n"));
    saved.close();
    song.setMetronomeOn(false);
    song.setCountInBars(0);
    song.setMetronomeLevelDb(-20.0);
    check(!controller.engine()->metronome_enabled());
    check(controller.loadProjectFile(project_path));
    lay_out();
    check(song.metronomeOn() && song.countInBars() == 2 && song.metronomeLevelDb() == -6.0);
    check(controller.engine() != nullptr && controller.engine()->metronome_enabled());
    check(text_of("countInButton") == "CI 2" && text_of("clickLevelReadout") == "-6.0");
    reached("metronome: the click and count-in are saved and loaded with the project");

    // --------------------------------------------------- screenshot
    // Recording armed and counting in: CLICK lit, the count-in chip lit red.
    song.selectPattern(0);
    check(press("recordButton"));
    controller.seekToBar(1);
    check(press("playButton"));
    (void)listen(8, {});
    controller.pollCountIn();
    settle(40);
    lay_out();
    check(controller.countingIn() && ctx.named("countInButton")->property("on").toBool());
    reached("metronome: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    check(press("playButton"));
    check(press("recordButton"));
    (void)listen(2, {});
    std::ostringstream details;
    details << " | clicks on beats=" << on_beats << " | count-in=" << counted_in
            << " | take on step 0=" << landed << " | export plain/click=" << plain_quiet << '/'
            << click_there << " | slider top=" << pressed_level;
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("metronome", run_metronome);

}  // namespace
}  // namespace blokkily::verify
