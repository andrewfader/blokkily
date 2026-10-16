// Stems and the scope video (features/export_formats.feature): every bus as a
// file of its own, and an oscilloscope video of the tracks over the master,
// both rendered by the engine the speakers hear.

#include "app_controller.hpp"
#include "scope_video.hpp"
#include "song_model.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>

#include <cmath>

namespace {

QString local_file(const QString& value) {
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : value;
}

blokkily::WaveFormat wave_depth(const QString& depth) {
    return depth == "PCM16"  ? blokkily::WaveFormat::pcm16
         : depth == "PCM24"  ? blokkily::WaveFormat::pcm24
                             : blokkily::WaveFormat::float32;
}

// A stereo bounce read back as the mono signal a scope lane draws.
std::optional<std::vector<float>> mono_of(const std::filesystem::path& file, std::string* error) {
    auto wave = blokkily::read_wave(file, error);
    if (!wave) return std::nullopt;
    std::vector<float> mono(static_cast<std::size_t>(wave->frames));
    for (std::size_t frame = 0; frame < mono.size(); ++frame)
        mono[frame] = 0.5F * (wave->interleaved[frame * 2] + wave->interleaved[frame * 2 + 1]);
    return mono;
}

} // namespace

bool AppController::exportTrackFiles(const QString& directory, const QString& format,
                                     const QString& depth, bool dither) {
    if (!engine_ || !song_) {
        export_status_ = "Load an instrument before exporting";
        emit exportStatusChanged();
        return false;
    }
    const auto folder = local_file(directory);
    const auto container = blokkily::container_from_name(format.toStdString());
    const LoadProgress::Scope exporting(
        progress_, QStringLiteral("EXPORTING STEMS TO %1").arg(QFileInfo(folder).fileName().toUpper()));
    blokkily::BounceOptions options;
    options.container = container;
    options.dither = dither;
    std::string error;
    std::optional<std::vector<blokkily::StemFile>> stems;
    withEngineStopped([&](std::uint64_t tail) {
        options.progress = bounceProgress(engine_->sample_rate());
        stems = blokkily::bounce_stems(*engine_, song_->song(), folder.toStdString(),
                                       wave_depth(depth), tail, options, &error);
    });
    if (!stems) {
        export_status_ = QString("Stem export failed · %1").arg(QString::fromStdString(error));
        emit exportStatusChanged();
        return false;
    }
    bool clipped = false;
    for (const auto& stem : *stems) clipped = clipped || stem.report.clipped;
    export_status_ = QString("%1 stems · %2 · %3%4")
                         .arg(stems->size())
                         .arg(blokkily::container_name(container))
                         .arg(QFileInfo(folder).fileName())
                         .arg(clipped ? " · CLIPPED" : "");
    emit exportStatusChanged();
    return true;
}

bool AppController::scopeVideoAvailable() const { return !blokkily::find_ffmpeg().isEmpty(); }

bool AppController::exportScopeVideo(const QString& path) {
    const auto fail = [this](const QString& why) {
        export_status_ = QString("Scope video failed · %1").arg(why);
        emit exportStatusChanged();
        return false;
    };
    if (!engine_ || !song_) return fail("load an instrument first");
    const auto ffmpeg = blokkily::find_ffmpeg();
    if (ffmpeg.isEmpty()) return fail("ffmpeg was not found on PATH");
    const auto output = local_file(path);
    QTemporaryDir work;
    if (!work.isValid()) return fail("no temporary folder");
    const LoadProgress::Scope exporting(
        progress_, QStringLiteral("RENDERING %1").arg(QFileInfo(output).fileName().toUpper()));

    // The lanes and the sound are bounces of the one engine, so the picture
    // is of exactly what is heard, sample for sample aligned.
    const auto song = song_->song();
    const auto master_file = std::filesystem::path(work.filePath("master.wav").toStdString());
    std::string error;
    std::optional<std::vector<blokkily::StemFile>> stems;
    std::optional<blokkily::BounceReport> master;
    withEngineStopped([&](std::uint64_t tail) {
        blokkily::BounceOptions options;
        options.progress = [this](std::uint64_t written, std::uint64_t total) {
            progress_.report(0.5 * static_cast<double>(written) / static_cast<double>(std::max<std::uint64_t>(1, total)),
                             QStringLiteral("BOUNCING TRACKS"));
        };
        stems = blokkily::bounce_stems(*engine_, song, work.path().toStdString(),
                                       blokkily::WaveFormat::float32, tail, options, &error);
        if (!stems) return;
        options.progress = {};
        master = blokkily::bounce_song(*engine_, master_file, blokkily::WaveFormat::float32, tail,
                                       options, &error);
    });
    if (!stems || !master) return fail(QString::fromStdString(error));

    std::vector<blokkily::ScopeLane> lanes;
    for (const auto& stem : *stems) {
        // A return's lane is only worth a place when something feeds it.
        auto samples = mono_of(stem.file, &error);
        if (!samples) return fail(QString::fromStdString(error));
        if (stem.source.kind != blokkily::BusKind::track && stem.report.peak <= 0.0F) continue;
        lanes.push_back({QString::fromStdString(stem.name),
                         blokkily::scope_lane_color(lanes.size()), std::move(*samples)});
        std::filesystem::remove(stem.file);
    }
    auto master_samples = mono_of(master_file, &error);
    if (!master_samples) return fail(QString::fromStdString(error));
    blokkily::ScopeLane master_lane{QStringLiteral("Master"), QColor(0xe8, 0xe9, 0xed),
                                    std::move(*master_samples)};

    const auto lane_count = lanes.size();
    const auto report = blokkily::render_scope_video(
        ffmpeg, output.toStdString(), master_file, engine_->sample_rate(), std::move(lanes),
        std::move(master_lane), {},
        [this](int frame, int frames) {
            progress_.report(0.5 + 0.5 * frame / std::max(1, frames),
                             QStringLiteral("FRAME %1 OF %2").arg(frame).arg(frames));
        },
        &error);
    if (!report) return fail(QString::fromStdString(error));
    export_status_ = QString("%1 · %2 s · %3 lanes · peak %4 dB%5")
                         .arg(QFileInfo(output).fileName())
                         .arg(report->seconds, 0, 'f', 1)
                         .arg(lane_count)
                         .arg(blokkily::linear_to_db(master->peak), 0, 'f', 1)
                         .arg(master->clipped ? " · CLIPPED" : "");
    emit exportStatusChanged();
    return true;
}
