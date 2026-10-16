// features/scene_tempo.feature, run by bdd_scene_tempo: a scene's own tempo,
// set in the launcher's scene row, plays when the scene is launched from its
// chip - heard as the bars of the launched loop arriving at that tempo
// through the production callback - while the song's tempo map is never
// edited, an export renders the song at its own tempo, and STOP ALL brings
// the song's tempo back. The tempo is saved with the scene.
#include "verify/harness.hpp"
#include "song_model.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QFileInfo>

#include <cmath>
#include <iostream>
#include <source_location>
#include <vector>

namespace blokkily::verify {
namespace {

// A key held for the first half of a 4/4 bar, then silence: each bar is
// heard as one onset.
Pattern half_bar() {
    Pattern pattern(1920, 480);
    Trigger note;
    note.start = 0;
    note.duration = 960;
    note.musical_data = Note{60, 1.0F, 0.0F};
    (void)pattern.add(note);
    return pattern;
}

void run_scene_tempo(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "scene tempo failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto click = [&](const QString& name, Qt::MouseButton button = Qt::LeftButton) {
        auto* item = ctx.named(name);
        check(VerifyContext::usable(item, 16, 16));
        if (item) ctx.click_at(item, {item->width() / 2, item->height() / 2}, button);
        layout();
    };

    // The CLAP synth on one track; its pattern on the arrangement for four
    // bars, and as the cell of SCENE 1.
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    {
        auto configured = s.song();
        configured.tracks.resize(1);
        configured.patterns = {{"PULSE", half_bar()}};
        configured.sections.clear();
        configured.clips = {{0, 0, 0, 4}};
        configured.launcher = SceneMatrix{};
        configured.modulators.clear();
        configured.adopt_sections();
        s.replace(std::move(configured));
        c.flushRecompile();
    }
    const double own_bpm = s.song().tempo.points.front().bpm;
    check(own_bpm == 120.0);
    click("viewLAUNCH");
    click("addScene");
    check(s.setLauncherCell(0, 0, s.partOf(0, 0)));
    layout();

    // The scene's row: right-click its chip. SONG until a tempo is given;
    // + starts from the tempo playing.
    click("launchScene0", Qt::RightButton);
    auto* row = ctx.named("sceneInspector");
    check(VerifyContext::usable(row, 200, 18));
    auto* shown = ctx.named("sceneTempo");
    check(shown && shown->property("text").toString() == "SONG");
    click("sceneTempoUp");
    check(s.song().launcher.scenes[0].tempo_bpm == 121.0);
    click("sceneTempoDown");
    check(s.song().launcher.scenes[0].tempo_bpm == 120.0);
    check(s.setSceneTempo(0, 150.0));
    layout();
    check(shown && VerifyContext::reads_whole(shown, "150.0"));
    auto* chip = ctx.named("launchScene0");
    check(chip && chip->property("text").toString().endsWith("· 150.0"));
    ctx.reached("scene tempo: the scene row sets the tempo, shown on its chip");

    // The song at its own tempo, exported before the scene plays.
    const auto own_length = c.engine() ? c.engine()->song_samples() : 0;
    const QString base = QFileInfo(ctx.parser.value("project")).path() + "/scene-tempo";
    check(c.exportAudioFile(base + "-song.wav", "FLOAT32"));
    const auto song_wave = read_wave((base + "-song.wav").toStdString());
    check(song_wave.has_value());
    const bool was_dirty = s.dirty();

    // Launched from its chip: the scene plays at 150 BPM.
    click("launchScene0");
    check(c.sceneTempo() == 150.0 && ctx.transport.playing());
    check(std::abs(ctx.transport.bpm() - 150.0) < 1e-9);
    const auto scene_length = c.engine() ? c.engine()->song_samples() : 0;
    std::cerr << "song " << own_length << " samples at its own tempo, " << scene_length
              << " at the scene's\n";
    check(std::llabs(static_cast<long long>(scene_length) -
                     std::llround(static_cast<double>(own_length) * own_bpm / 150.0)) <= 2);
    // Heard: one onset a bar, 76800 samples (150 blocks of 512) apart, where
    // the song's own 120 BPM would put 96000.
    std::vector<int> onsets;
    bool sounding = true;
    for (int block = 0; block < 700; ++block) {
        const float peak = ctx.pump();
        const bool now = peak > 1e-3F;
        if (now && !sounding) onsets.push_back(block);
        sounding = now;
    }
    std::cerr << "onsets at blocks";
    for (const auto at : onsets) std::cerr << ' ' << at;
    std::cerr << '\n';
    check(onsets.size() >= 3);
    for (std::size_t i = 1; i < onsets.size(); ++i)
        check(std::abs(onsets[i] - onsets[i - 1] - 150) <= 1);
    // The song's own tempo map is untouched, and launching is no edit.
    check(s.song().tempo.points.size() == 1 && s.song().tempo.points.front().bpm == own_bpm);
    check(s.dirty() == was_dirty);
    ctx.reached("scene tempo: launched, the scene is heard at its tempo");

    // An export while it plays is the song at its own tempo.
    check(c.exportAudioFile(base + "-during.wav", "FLOAT32"));
    const auto during = read_wave((base + "-during.wav").toStdString());
    check(during && song_wave && during->interleaved == song_wave->interleaved);
    check(c.sceneTempo() == 150.0 && c.engine() && c.engine()->song_samples() == scene_length);
    ctx.reached("scene tempo: an export renders the song at its own tempo");

    // STOP ALL: the song's tempo again.
    click("stopAllLaunched");
    check(c.sceneTempo() == 0.0 && std::abs(ctx.transport.bpm() - own_bpm) < 1e-9);
    check(c.engine() && c.engine()->song_samples() == own_length);
    ctx.reached("scene tempo: STOP ALL brings the song's tempo back");

    // Saved with the scene; SONG takes it off as one undo step.
    check(c.saveProject(ctx.parser.value("project")));
    check(c.loadProject(ctx.parser.value("project")));
    layout();
    check(s.song().launcher.scenes.size() == 1 && s.song().launcher.scenes[0].tempo_bpm == 150.0);
    {
        auto broken = s.song();
        broken.launcher.scenes[0].tempo_bpm = 5.0;
        check(!broken.consistent());
    }
    click("viewLAUNCH");
    click("launchScene0", Qt::RightButton);
    click("sceneTempoSong");
    check(s.song().launcher.scenes[0].tempo_bpm == 0.0);
    check(s.undo() && s.song().launcher.scenes[0].tempo_bpm == 150.0);
    layout();
    ctx.reached("scene tempo: saved and loaded; SONG and undo");
    if (ctx.transport.playing()) c.togglePlayback();
    layout();
    check(ctx.save_screenshot());
    ctx.finish("a scene launched at its own tempo, heard at it, the song's left alone");
}

[[maybe_unused]] const bool registered = register_scenario("scene_tempo", run_scene_tempo);

} // namespace
} // namespace blokkily::verify
