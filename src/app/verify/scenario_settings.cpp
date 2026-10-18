// features/audio_settings.feature, run by bdd_settings: the SETTINGS view's
// audio page lists the system, the devices, the rates and the blocks; picking
// another rate and block reopens the device at them, and the song is
// rendered at the new rate through the production callback. What is not
// offered is refused. A gate's settings stay in memory.
#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_settings(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& pattern = ctx.pattern;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "settings failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto click = [&](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr || !item->isVisible()) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        layout();
        return true;
    };
    // Opens a picker's menu and presses the entry that reads `text`.
    const auto pick = [&](const char* picker, const QString& text) {
        auto* owner = ctx.named(QString::fromLatin1(picker));
        if (owner == nullptr || !owner->isVisible()) return false;
        ctx.click_at(owner, {owner->width() / 2, owner->height() / 2}, Qt::LeftButton);
        layout();
        auto* menu = owner->findChild<QObject*>(QString::fromLatin1(picker) + "Menu");
        if (menu == nullptr || !menu->property("opened").toBool()) return false;
        const int count = menu->property("count").toInt();
        for (int row = 0; row < count; ++row) {
            QQuickItem* entry = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, entry), Q_ARG(int, row));
            if (entry == nullptr || entry->property("text").toString() != text) continue;
            ctx.click_at(entry, {entry->width() / 2, entry->height() / 2}, Qt::LeftButton);
            layout();
            return true;
        }
        QMetaObject::invokeMethod(menu, "close");
        layout();
        return false;
    };
    const auto choices = [&](const char* picker) {
        auto* item = ctx.named(QString::fromLatin1(picker));
        return item ? item->property("choices").toStringList() : QStringList{};
    };
    // The loudest the master gets over a few blocks of a held note.
    const auto note_level = [&] {
        c.seekToStep(0);
        c.togglePlayback();
        float loudest = 0.0F;
        for (int block = 0; block < 12; ++block) loudest = std::max(loudest, ctx.pump());
        c.togglePlayback();
        (void)ctx.pump();
        return loudest;
    };

    // The user's settings file, as it is before: a gate must not write it.
    const QString settings = QSettings().fileName();
    const QFileInfo before(settings);
    const bool existed = before.exists();
    const auto stamp = existed ? before.lastModified() : QDateTime{};

    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    pattern.setStepKey(0, 60);
    c.flushRecompile();
    layout();
    const double rate_before = c.engine() ? c.engine()->sample_rate() : 0.0;
    check(rate_before == 48000.0);

    // The gear sits beside HELP and GUIDE without pushing the editors'
    // column past its room, even with both bars docked at 1280 px.
    auto* column = ctx.named("editorColumn");
    auto* area = ctx.named("editorArea");
    auto* guide = ctx.named("guideButton");
    const double area_right = area ? area->mapToScene({area->width(), 0}).x() : 0.0;
    const double guide_right = guide ? guide->mapToScene({guide->width(), 0}).x() : 1e9;
    std::cerr << "GUIDE ends at " << guide_right << " of " << area_right << '\n';
    check(VerifyContext::usable(ctx.named("viewSETTINGS"), 24, 18));
    check(column && area && guide_right <= area_right - 10 && column->width() <= area->width());
    // SETTINGS, beside HELP, opens in the editors' place.
    check(click("viewSETTINGS"));
    check(ctx.window->property("view").toString() == "SETTINGS");
    auto* panel = ctx.named("settingsPanel");
    check(VerifyContext::usable(panel, 500, 320));
    for (const char* name : {"audioSystem", "audioOutputDevice", "audioInputDevice",
                             "audioSampleRate", "audioBufferFrames"})
        check(VerifyContext::usable(ctx.named(QString::fromLatin1(name)), 120, 20));
    check(choices("audioSystem").size() == c.audioSystems().size() && choices("audioSystem").front() == "AUTO");
    // DEFAULT, then the devices the system offers: a gate's pump.
    check(choices("audioOutputDevice") == QStringList({"DEFAULT", "Deterministic pump"}));
    check(choices("audioSampleRate").contains("44100 Hz") && choices("audioSampleRate").contains("48000 Hz"));
    check(choices("audioBufferFrames").contains("256 frames"));
    // The editors give the view their room; the rail and the mixer stay.
    auto* editors = ctx.named("stepEditors");
    check(editors != nullptr && !editors->isVisible());
    check(VerifyContext::usable(ctx.named("instrumentPanel"), 150, 200) &&
          VerifyContext::usable(ctx.named("mixerPanel"), 150, 200));
    for (const char* name : {"midiClockPort", "followClock", "autoHideRail", "autoHideMixer", "resetLayout"})
        check(VerifyContext::usable(ctx.named(QString::fromLatin1(name)), 30, 18));
    std::cerr << "running: " << c.audioDevice().toStdString() << '\n';
    check(ctx.named("audioDeviceStatus") &&
          ctx.named("audioDeviceStatus")->property("text").toString() == c.audioDevice());
    // A second press goes back to the editors it was opened from.
    check(click("viewSETTINGS"));
    check(ctx.window->property("view").toString() == "ALL" && editors && editors->isVisible());
    check(click("viewSETTINGS"));
    check(ctx.window->property("view").toString() == "SETTINGS");
    ctx.reached("settings: the SETTINGS view lays out the audio, MIDI and window settings");

    // 44.1 kHz and a 256-frame block, picked from their menus.
    check(pick("audioSampleRate", "44100 Hz"));
    check(pick("audioBufferFrames", "256 frames"));
    c.flushRecompile();
    layout();
    check(c.audioSampleRate() == 44100 && c.audioBufferFrames() == 256);
    check(c.engine() != nullptr && c.engine()->sample_rate() == 44100.0);
    std::cerr << "running: " << c.audioDevice().toStdString() << '\n';
    check(c.audioDevice() == "Deterministic pump");
    check(ctx.output && ctx.output->device_info().sample_rate == 44100 &&
          ctx.output->device_info().buffer_frames == 256);
    check(ctx.named("audioSampleRate")->property("value").toString() == "44100 Hz");
    check(ctx.named("audioBufferFrames")->property("value").toString() == "256 frames");
    const float level = note_level();
    std::cerr << "note at 44.1 kHz: " << level << '\n';
    check(level > 0.01F);
    ctx.reached("settings: another rate and block reopen the device, and the song plays at them");

    // What is not offered is refused, and nothing changes.
    check(!c.setAudioSampleRate(12345) && c.status().contains("not offered"));
    check(!c.setAudioBufferFrames(300) && c.status().contains("not offered"));
    check(!c.setAudioOutputDevice("No such interface") && c.status().contains("No output device"));
    check(c.audioSampleRate() == 44100 && c.audioBufferFrames() == 256 && c.audioOutputDevice().isEmpty());
    check(c.engine() != nullptr && c.engine()->sample_rate() == 44100.0);
    // The pump picked by name is the device already playing.
    check(pick("audioOutputDevice", "Deterministic pump"));
    check(c.audioOutputDevice() == "Deterministic pump" && note_level() > 0.01F);
    const QFileInfo after(settings);
    check(after.exists() == existed && (!existed || after.lastModified() == stamp));
    ctx.reached("settings: what is not offered is refused; the user's settings are untouched");

    check(ctx.save_screenshot());
    ctx.finish("the SETTINGS view's audio device, rate and block");
}

[[maybe_unused]] const bool registered = register_scenario("settings", run_settings);

} // namespace
} // namespace blokkily::verify
