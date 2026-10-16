// features/audio_systems.feature, run by bdd_audio_system: the AUDIO picker
// lists the systems this build can open; choosing one is remembered and an
// unknown one refused. The gate's own device is deterministic and stays so.
#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_audio_system(VerifyContext& ctx) {
    auto& c = ctx.controller;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "audio system failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    layout();
    auto* picker = ctx.named("audioSystem");
    check(VerifyContext::usable(picker, 60, 18));
    const auto choices = picker ? picker->property("choices").toStringList() : QStringList{};
    std::cerr << "audio systems: " << choices.join(", ").toStdString() << '\n';
    check(!choices.isEmpty() && choices.front() == "AUTO");
    check(choices.size() == c.audioSystems().size());
    // The user's settings file, as it is before: a gate must not write it.
    const QString settings = QSettings().fileName();
    const QFileInfo before(settings);
    const bool existed = before.exists();
    const auto stamp = existed ? before.lastModified() : QDateTime{};
    check(!c.setAudioSystem("no-such-system"));
    check(c.status().contains("not available"));
    const auto systems = c.audioSystems();
    if (systems.size() > 1) {
        const auto first = systems[1].toMap();
        check(c.setAudioSystem(first.value("name").toString()));
        layout();
        check(c.audioSystem() == first.value("name").toString());
        check(picker && picker->property("value").toString() == first.value("display").toString());
    }
    // The gate's own device is deterministic (or not yet open) and stays so:
    // choosing a system never put a real device under a gate.
    std::cerr << "device: " << c.audioDevice().toStdString() << '\n';
    check(c.audioDevice() == "Deterministic pump");
    check(ctx.output != nullptr && !c.status().contains("could not open"));
    const QFileInfo after(settings);
    check(after.exists() == existed && (!existed || after.lastModified() == stamp));
    ctx.reached("audio system: the picker lists the systems and remembers the choice");
    check(ctx.save_screenshot());
    ctx.finish("the audio system picker");
}

[[maybe_unused]] const bool registered = register_scenario("audio_system", run_audio_system);

} // namespace
} // namespace blokkily::verify
