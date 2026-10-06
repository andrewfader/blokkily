#include "harness.hpp"
#include "blokkily/project/midi_file.hpp"
#include <QFileInfo>
#include <QDir>
#include <cstdio>
#include <source_location>

namespace blokkily::verify {
namespace {
void run_workflow(VerifyContext& ctx) {
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    const auto check = [&ctx](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) std::fprintf(stderr, "workflow check failed at line %u\n", where.line());
        ctx.check(ok);
    };
    const auto layout = [&] { (void)ctx.window->grabWindow(); VerifyContext::settle(40); };
    const auto click = [&](const char* name) {
        auto* item = ctx.named(name);
        check(VerifyContext::usable(item, 16, 10));
        if (item) ctx.click_at(item, {item->width()/2, item->height()/2}, Qt::LeftButton);
        layout();
    };
    const auto draw = [&](int step, int key) {
        auto* roll = ctx.named("rollInput");
        check(VerifyContext::usable(roll, 100, 40));
        if (roll) ctx.click_at(roll,
            {(step + 0.5) * roll->width() / pattern.stepCount(),
             (pattern.highKey() - key + 0.5) * roll->height() /
                 (pattern.highKey() - pattern.lowKey() + 1)}, Qt::LeftButton);
        layout();
    };
    check(controller.verifyClap(ctx.parser.value("clap-fixture")));
    auto configured = song.song();
    const auto synth = configured.tracks[0].instrument;
    configured.tracks.resize(2);
    configured.tracks[0].name = "KEYS";
    configured.tracks[1].name = "BASS";
    configured.tracks[1].instrument = synth;
    configured.patterns.clear(); configured.clips.clear(); configured.sections.clear();
    const auto section = configured.add_section("VERSE", 1920, 480);
    (void)configured.place_section(section, 0);
    (void)configured.place_section(section, 1920 * 63);
    song.replace(std::move(configured));
    check(ctx.window->setProperty("view", "ALL"));
    layout();
    pattern.selectChannelStep(0, 0);
    pattern.setStepKey(0, 60);
    pattern.selectChannelStep(1, 0);
    pattern.setStepKey(0, 36);
    layout();
    check(pattern.trackerChannels().size() == 2);
    check(VerifyContext::usable(ctx.named("trackerChannel0-0"), 100, 10));
    check(VerifyContext::usable(ctx.named("trackerChannel1-0"), 100, 10));
    click("trackerChannelName0");
    check(song.selectedTrack() == 0);
    ctx.type_key(Qt::Key_Tab, "\t"); layout();
    check(song.selectedTrack() == 1 && pattern.selectedStep() == 0);
    ctx.chord_key(Qt::Key_Backtab, Qt::ShiftModifier); layout();
    check(song.selectedTrack() == 0);
    ctx.reached("workflow: tracker channels and keyboard navigation");

    // Regression: the roll must edit a voice, never replace its whole step.
    pattern.scrollRoll(0);
    pattern.clearStep(0);
    draw(4, 60);
    draw(4, 64);
    const auto voices = [&](int step) {
        return pattern.steps().at(step).toMap().value("voiceKeys").toList();
    };
    check(voices(4).contains(60) && voices(4).contains(64));
    check(song.song().arrange(0, 0).notes.size() == 4); // two voices, two clips
    ctx.reached("workflow: adding a piano roll voice preserves the existing note");

    auto* roll = ctx.named("rollInput");
    const auto point = [&](double step, int key) {
        return QPointF(step * roll->width() / pattern.stepCount(),
                       (pattern.highKey() - key + 0.5) * roll->height() /
                           (pattern.highKey() - pattern.lowKey() + 1));
    };
    const auto erase = [&](double step, int key) {
        ctx.click_at(roll, point(step, key), Qt::RightButton); layout();
    };
    erase(4.3, 62);
    check(voices(4).size() == 2);
    erase(4.3, 64);
    check(voices(4).contains(60) && !voices(4).contains(64));
    check(song.undo()); layout();
    check(voices(4).size() == 2);
    const auto drag = [&](double from, int key, double to, int newKey) {
        ctx.mouse_at(roll, point(from, key), QEvent::MouseButtonPress,
                     Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(roll, point(to, newKey), QEvent::MouseMove,
                     Qt::NoButton, Qt::LeftButton);
        ctx.mouse_at(roll, point(to, newKey), QEvent::MouseButtonRelease,
                     Qt::LeftButton, Qt::NoButton);
        layout();
    };
    drag(4.8, 60, 7.0, 60);
    auto lengths = pattern.steps().at(4).toMap().value("voiceLengths").toList();
    check(lengths.size() == 2 && lengths[0].toInt() == 360 && lengths[1].toInt() == 96);
    draw(8, 67);
    song.clearHistory();
    // Grabbing the middle of a long note keeps the offset from its start.
    drag(5.3, 60, 9.3, 62);
    check(voices(4).size() == 1 && voices(4).contains(64) && pattern.stepKey(4) == 64);
    check(voices(8).contains(62) && voices(8).contains(67));
    check(song.undo()); layout();
    check(voices(4).size() == 2 && voices(8).size() == 1 && !song.canUndo());
    check(song.redo()); layout();
    check(voices(8).size() == 2);
    // Clicking the body of a long note is only selection.
    song.clearHistory();
    ctx.click_at(roll, point(9.3, 62), Qt::LeftButton); layout();
    check(!song.canUndo() && voices(8).size() == 2);
    // A drop onto the same pitch is rejected rather than erasing either note.
    draw(10, 64);
    drag(4.3, 64, 10.3, 64);
    check(voices(4).contains(64) && voices(10).contains(64));
    ctx.reached("workflow: erase, resize and move preserve other piano roll voices");
    check(ctx.window->setProperty("view", "PIANO")); layout();
    check(ctx.save_screenshot_to(QFileInfo(ctx.parser.value("screenshot")).absolutePath()
                                 + "/piano-roll-notes.png"));
    check(ctx.window->setProperty("view", "ALL")); layout();
    pattern.clearStep(4); pattern.clearStep(8); pattern.clearStep(10);

    song.clearHistory();
    auto* engine = controller.engine();
    const int low = pattern.lowKey();
    click("rollUp");
    check(pattern.lowKey() == low + 12 && !song.canUndo() && controller.engine() == engine);
    for (int i = 0; i < 11; ++i) click("rollDown");
    check(pattern.lowKey() == 0);
    draw(1, 0);
    for (int i = 0; i < 11; ++i) click("rollUp");
    check(pattern.highKey() == 127);
    draw(2, 127);
    check(pattern.stepKey(1) == 0 && pattern.stepKey(2) == 127);
    ctx.reached("workflow: piano roll reaches both ends of MIDI pitch range");

    auto* first = ctx.named("clip0-0");
    auto* scroll = ctx.named("arrangeScroll");
    check(first && first->width() >= 40 && scroll);
    const double width = first ? first->width() : 0;
    click("arrangeZoomIn");
    first = ctx.named("clip0-0");
    check(first && first->width() > width);
    if (scroll) {
        scroll->setProperty("contentX", scroll->property("contentWidth").toDouble() - scroll->width());
        layout();
        auto* last = ctx.named("clip0-63");
        check(VerifyContext::usable(last, 40, 15));
        if (last) {
            const auto location = last->mapToItem(scroll, QPointF(last->width()/2, 0));
            check(location.x() >= 64 && location.x() < scroll->width());
        }
        scroll->setProperty("contentX", 0);
    }
    ctx.reached("workflow: long arrangements remain editable at usable bar widths");

    QDir{}.mkpath(QFileInfo(ctx.parser.value("screenshot")).absolutePath());
    const auto path = QFileInfo(ctx.parser.value("screenshot")).absolutePath() + "/workflow.mid";
    check(controller.exportMidiFile(path));
    const auto file = read_midi_file(path.toStdString());
    check(file && file->tracks.size() == 3);
    if (file) {
        const auto notes = song.song().arrange(0, 0).notes;
        check(file->tracks[1].notes.size() == notes.size());
        for (std::size_t i = 0; i < notes.size() && i < file->tracks[1].notes.size(); ++i)
            check(notes[i].start == file->tracks[1].notes[i].start &&
                  notes[i].duration == file->tracks[1].notes[i].duration &&
                  notes[i].key == file->tracks[1].notes[i].key);
    }
    const auto length = song.song().length();
    const auto sections = song.song().sections.size();
    song.clearHistory();
    check(controller.importMidiFile(path));
    check(song.song().length() > length && song.song().sections.size() > sections && song.canUndo());
    check(song.undo());
    check(song.song().length() == length && song.song().sections.size() == sections && !song.canUndo());
    check(!controller.importMidiFile(path + ".missing") && !song.canUndo());
    check(VerifyContext::usable(ctx.named("importMidiButton"), 60, 16));
    check(VerifyContext::usable(ctx.named("exportMidiButton"), 60, 16));
    ctx.reached("workflow: MIDI file round trip and atomic undoable import");
    song.selectSection(0);
    pattern.scrollRoll(-60);
    layout();
    // features/sequencer_workflow.feature: Playback follows the whole pattern.
    auto playback = song.song();
    playback.patterns.clear(); playback.clips.clear(); playback.sections.clear();
    const auto playingSection = playback.add_section("PLAYBACK", 3840, 480);
    (void)playback.place_section(playingSection, 1920);
    for (auto& clip : playback.clips) clip.repeats = 2;
    song.replace(std::move(playback));
    song.selectSection(0);
    pattern.selectChannelStep(0, 20);
    pattern.setStepKey(20, 60);
    pattern.selectStep(0); // Playback must not move the edit cursor.
    controller.seekToTick(1920 + 20 * 120);
    click("playButton");
    check(ctx.transport.playing());
    check(ctx.pump() >= 0);
    VerifyContext::settle(80);
    layout();
    auto* playingRow = ctx.named("trackerRow20");
    auto* barRow = ctx.named("trackerRow4");
    check(playingRow && playingRow->property("playhead").toBool());
    check(barRow && !barRow->property("playhead").toBool());
    auto* tracker = ctx.named("trackerScroll");
    if (playingRow && tracker) {
        const auto at = playingRow->mapToItem(tracker, QPointF(0, 0));
        check(at.y() >= 0 && at.y() + playingRow->height() <= tracker->height());
    }
    auto* head = ctx.named("rollPlayhead");
    auto* input = ctx.named("rollInput");
    check(VerifyContext::usable(head, 2, 40));
    if (head && input) {
        const double column = (head->x() - input->x()) / (input->width() / 32);
        check(column >= 20 && column < 21);
    }
    check(pattern.selectedStep() == 0);
    ctx.reached("workflow: playback follows the whole pattern");
    check(ctx.save_screenshot());
    controller.seekToTick(1920 + 3840 + 6 * 120);
    check(ctx.pump() >= 0);
    VerifyContext::settle(80);
    layout();
    check(ctx.named("trackerRow6")->property("playhead").toBool());
    controller.seekToTick(0); // The open pattern has no clip here.
    layout();
    check(head && !head->isVisible());
    check(!ctx.named("trackerRow0")->property("playhead").toBool());
    click("playButton");
    check(!ctx.transport.playing());
    ctx.reached("workflow: playback repeats and gaps follow the arrangement");
    ctx.reached("screenshot");
    ctx.finish(" | tracker, pitch range, timeline, MIDI");
}
[[maybe_unused]] const bool registered = register_scenario("workflow", run_workflow);
}
}
