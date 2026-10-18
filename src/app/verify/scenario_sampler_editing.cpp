// features/sampler_editing.feature, run by bdd_sampler_editing: the sampler
// panel imports a multi-sampled instrument from named files, shows its zones
// as a key-by-velocity map, edits a zone's velocities, and moves its region
// and loop on the waveform; the sampler plays what the panel says.
#include "verify/harness.hpp"
#include "song_model.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>

#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_sampler_editing(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "sampler editing failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto click = [&](QQuickItem* item) {
        if (item) ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        layout();
    };
    check(ctx.window->setProperty("view", "ALL"));
    s.selectTrack(0);
    layout();
    // The sampler, chosen in the browser.
    const auto rows = c.browserPlugins();
    for (int row = 0; row < rows.size(); ++row)
        if (rows.at(row).toMap().value("name").toString() == "Sampler") click(ctx.named(QString("browserRow%1").arg(row)));
    check(s.song().tracks.at(0).instrument.format == "Sampler");

    // A small library, named as libraries name their files: two roots, two
    // dynamics each, every file a sine at its root's pitch.
    const QString folder = QFileInfo(ctx.parser.value("screenshot")).path() + "/sampler-library";
    QDir(folder).removeRecursively();
    QDir().mkpath(folder);
    QStringList files;
    for (const auto& [name, key, level] : {std::tuple{"Lead_C4_pp.wav", 60, 0.2},
                                           std::tuple{"Lead_C4_ff.wav", 60, 0.8},
                                           std::tuple{"Lead_C5_pp.wav", 72, 0.2},
                                           std::tuple{"Lead_C5_ff.wav", 72, 0.8}}) {
        const double hz = 440.0 * std::pow(2.0, (key - 69) / 12.0);
        std::vector<float> samples(48000);
        for (std::size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>(level * std::sin(2.0 * 3.141592653589793 * hz * i / 48000.0));
        blokkily::WaveWriter writer;
        std::string error;
        const QString path = folder + "/" + name;
        check(writer.open(path.toStdString(), 48000, blokkily::WaveFormat::float32, &error) &&
              writer.write(samples, samples, &error) && writer.close(&error));
        files << path;
    }
    check(VerifyContext::usable(ctx.named("samplerImportMulti"), 60, 16));
    check(VerifyContext::usable(ctx.named("samplerLoad"), 40, 16));
    check(c.importMultisample(files) == 4);
    layout();
    auto view = c.sampler();
    const auto map = view.value("map").toList();
    check(map.size() == 4);
    // Soft layer first, lowest root first: C4 pp answers 0..66 at 1..63.
    if (map.size() == 4) {
        const auto first = map[0].toMap();
        check(first.value("rootKey").toInt() == 60 && first.value("lowKey").toInt() == 0 &&
              first.value("highKey").toInt() == 66 && first.value("highVelocity").toInt() == 63);
        const auto last = map[3].toMap();
        check(last.value("rootKey").toInt() == 72 && last.value("lowKey").toInt() == 67 &&
              last.value("lowVelocity").toInt() == 64);
    }
    check(c.status().startsWith("4 samples mapped · 2 velocity layers"));
    // One step of history for the whole import.
    check(s.undo() && c.sampler().value("zones").toInt() <= 1);
    check(s.redo() && c.sampler().value("zones").toInt() == 4);
    ctx.reached("sampler editing: four named files become two roots in two velocity layers");

    // The map: a box per zone; clicking one edits it. EDIT opens it.
    layout();
    auto* edit = ctx.named("samplerEditToggle");
    check(VerifyContext::usable(edit, 30, 16));
    check(ctx.named("samplerZoneMap") == nullptr || !ctx.named("samplerZoneMap")->isVisible());
    click(edit);
    check(VerifyContext::usable(ctx.named("samplerZoneMap"), 100, 40));
    auto* box = ctx.named("samplerZoneBox3");
    check(VerifyContext::usable(box, 8, 8));
    click(box);
    view = c.sampler();
    check(view.value("zone").toInt() == 3 && view.value("sample").toString() == "Lead_C5_ff.wav");
    // The file's name has room to be read, not squeezed by its neighbours.
    check(VerifyContext::usable(ctx.named("samplerSample"), 80, 10));
    check(ctx.named("samplerLowVelocity") && ctx.named("samplerLowVelocity")->property("value").toInt() == 64);
    // Its velocities, edited.
    check(c.setSamplerVelocityRange(100, 127));
    check(c.sampler().value("lowVelocity").toInt() == 100);
    check(s.undo() && c.sampler().value("lowVelocity").toInt() == 64);
    ctx.reached("sampler editing: the map picks a zone and its velocities are edited");

    // The waveform: the file drawn, START and END moved, a loop set.
    layout();
    auto* wave = ctx.named("samplerWaveform");
    check(VerifyContext::usable(wave, 100, 40));
    check(c.samplerPeaks(64).size() == 128);
    auto* start = ctx.named("samplerStartMarker");
    check(start != nullptr);
    const double before = start ? start->x() : 0.0;
    check(c.setSamplerRegion(12000, 36000));
    layout();
    view = c.sampler();
    check(view.value("start").toDouble() == 12000 && view.value("end").toDouble() == 36000);
    check(start && start->x() > before + 10);
    check(c.setSamplerLoopPoints(20000, 30000));
    layout();
    view = c.sampler();
    check(view.value("loop").toString() == "forward" && view.value("loopStart").toDouble() == 20000);
    check(ctx.named("samplerLoopStartMarker") && ctx.named("samplerLoopStartMarker")->isVisible());
    ctx.reached("sampler editing: START, END and the loop move on the waveform");

    // Double-clicked twice in its middle, the waveform shows a quarter of
    // the file around it, drawn from that quarter's own frames, and a pixel
    // there is a quarter of the frames it was; right double clicks zoom out.
    {
        auto* zoom_area = ctx.named("samplerWaveformZoom");
        check(zoom_area != nullptr);
        const auto double_click = [&](Qt::MouseButton button) {
            if (zoom_area == nullptr) return;
            const QPointF middle{zoom_area->width() / 2, zoom_area->height() / 2};
            ctx.mouse_at(zoom_area, middle, QEvent::MouseButtonPress, button, button);
            ctx.mouse_at(zoom_area, middle, QEvent::MouseButtonRelease, button, Qt::NoButton);
            ctx.mouse_at(zoom_area, middle, QEvent::MouseButtonPress, button, button);
            ctx.mouse_at(zoom_area, middle, QEvent::MouseButtonDblClick, button, button);
            ctx.mouse_at(zoom_area, middle, QEvent::MouseButtonRelease, button, Qt::NoButton);
            layout();
        };
        const double frames = wave ? wave->property("frames").toDouble() : 0.0;
        const auto per_pixel = [&] {
            QVariant one, two;
            QMetaObject::invokeMethod(wave, "frameOf", Q_RETURN_ARG(QVariant, one), Q_ARG(QVariant, 40.0));
            QMetaObject::invokeMethod(wave, "frameOf", Q_RETURN_ARG(QVariant, two), Q_ARG(QVariant, 41.0));
            return two.toDouble() - one.toDouble();
        };
        const double whole = wave ? per_pixel() : 0.0;
        double_click(Qt::LeftButton);
        double_click(Qt::LeftButton);
        check(wave != nullptr && std::abs(wave->property("zoom").toDouble() - 4.0) < 1e-9);
        check(VerifyContext::reads_whole(ctx.named("samplerZoomReadout"), QString::fromUtf8("\xC3\x97" "4.0")));
        const double from = wave ? wave->property("viewStart").toDouble() : 0.0;
        check(std::abs(from - frames * 3.0 / 8.0) < frames / 200.0);
        check(wave && std::abs(per_pixel() * 4.0 - whole) < 1e-6);
        const auto zoomed = c.samplerPeaks(64, from, from + frames / 4.0);
        check(zoomed.size() == 128 && zoomed != c.samplerPeaks(64));
        // START (12000) and END (36000) stand where the zoomed view puts
        // those frames, in view or out of it.
        auto* end_marker = ctx.named("samplerEndMarker");
        const auto placed = [&](QQuickItem* marker, double frame) {
            const double want = 1.0 + (frame - from) / (frames / 4.0) * (wave->width() - 2.0);
            return marker != nullptr && std::abs(marker->x() + marker->width() / 2.0 - want) < 0.5;
        };
        check(wave && placed(start, 12000.0) && placed(end_marker, 36000.0));
        const QString shot = ctx.parser.value("screenshot");
        if (!shot.isEmpty()) check(ctx.save_screenshot_to(shot.left(shot.size() - 4) + "-zoomed.png"));
        double_click(Qt::RightButton);
        double_click(Qt::RightButton);
        check(wave && std::abs(wave->property("zoom").toDouble() - 1.0) < 1e-9);
        // The panel's rows keep their labels whole.
        for (const auto& [chip, label] : {std::pair{"samplerLoop_off", "OFF"}, std::pair{"samplerLoop_forward", "FWD"},
                                          std::pair{"samplerLoop_ping_pong", "P-P"}}) {
            auto* item = ctx.named(chip);
            QQuickItem* text = nullptr;
            if (item != nullptr)
                for (auto* child : item->childItems())
                    if (child->property("text").toString() == QString::fromLatin1(label)) text = child;
            check(VerifyContext::reads_whole(text, QString::fromLatin1(label)));
        }
    }
    ctx.reached("sampler editing: the waveform zooms in and out");

    // The program plays: a C5 struck sounds.
    s.selectTrack(0);
    (void)ctx.pump();
    check(c.auditionKey(72, true));
    (void)ctx.pump();
    const float heard = ctx.pump();
    c.releaseAudition();
    std::cerr << "C5 heard at " << heard << '\n';
    check(heard > 0.05F);
    ctx.reached("sampler editing: the imported program plays");
    // Open, the panel still leaves the browser room to choose an instrument,
    // and the rail scrolls, so what is at its foot is still in reach.
    check(VerifyContext::usable(ctx.named("pluginBrowser"), 150, 40));
    auto* rail = ctx.named("railScroll");
    auto* foot = ctx.named("exportMidiButton");
    check(rail && foot);
    if (rail && foot) {
        check(rail->property("contentHeight").toDouble() > rail->height());
        rail->setProperty("contentY", rail->property("contentHeight").toDouble() - rail->height());
        layout();
        const auto bottom = foot->mapToItem(ctx.window->contentItem(), QPointF(0, foot->height())).y();
        std::cerr << "rail foot at " << bottom << " of " << ctx.window->height() << '\n';
        check(bottom <= ctx.window->height() && VerifyContext::usable(foot, 60, 18));
        rail->setProperty("contentY", 0.0);
        layout();
    }
    layout();
    check(ctx.save_screenshot());
    ctx.finish("multi-sample import, the zone map, velocity layers and waveform editing");
}

[[maybe_unused]] const bool registered = register_scenario("sampler_editing", run_sampler_editing);

} // namespace
} // namespace blokkily::verify
