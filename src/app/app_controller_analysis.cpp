// Render analysis (features/render_analysis.feature): the song bounced as an
// export is, read back from the file, and measured.

#include "app_controller.hpp"

#include "blokkily/audio/analysis.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QDir>
#include <QTemporaryDir>
#include <QUrl>

#include <cmath>

namespace {

// A level that has nothing to measure is null, which JSON can carry and an
// agent can read as "silent"; the rest to the hundredth, as meters show them.
QVariant level(double value, double places = 100.0) {
    if (!std::isfinite(value)) return {};
    return std::round(value * places) / places;
}

QVariantMap describe(const blokkily::AudioAnalysis& analysis) {
    QVariantList bands;
    for (const auto& band : analysis.bands)
        bands.append(QVariantMap{{"name", QString::fromStdString(band.name)},
                                 {"lowHz", band.low_hz},
                                 {"highHz", band.high_hz},
                                 {"levelDb", level(band.level_db)},
                                 {"share", level(band.share, 1000.0)}});
    return {{"seconds", level(analysis.seconds, 1000.0)},
            {"sampleRate", analysis.sample_rate},
            {"channels", analysis.channels},
            {"integratedLufs", level(analysis.integrated_lufs)},
            {"momentaryMaxLufs", level(analysis.momentary_max_lufs)},
            {"shortTermMaxLufs", level(analysis.short_term_max_lufs)},
            {"shortTermMaxAtSeconds", level(analysis.short_term_max_at)},
            {"loudnessRangeLu", level(analysis.loudness_range_lu)},
            {"samplePeakDbfs", level(analysis.sample_peak_dbfs)},
            {"truePeakDbtp", level(analysis.true_peak_dbtp)},
            {"rmsDbfs", level(analysis.rms_dbfs)},
            {"crestDb", level(analysis.crest_db)},
            {"clippedSamples", static_cast<qulonglong>(analysis.clipped_samples)},
            {"correlation", level(analysis.correlation, 1000.0)},
            {"spectralCentroidHz", level(analysis.spectral_centroid_hz, 1.0)},
            {"bands", bands}};
}

QVariantMap failed(const QString& why) { return {{"error", why}}; }

QVariantMap analyze_file(const QString& path) {
    std::string error;
    const auto wave = blokkily::read_wave(path.toStdString(), &error);
    if (!wave) return failed(QStringLiteral("Cannot read %1: %2").arg(path, QString::fromStdString(error)));
    return describe(blokkily::analyze_audio(wave->interleaved, wave->channels, wave->sample_rate));
}

}  // namespace

QVariantMap AppController::analyzeRender(const QString& kind, int bus, int fromBar, int toBar) {
    if (!engine_ || song_ == nullptr) return failed("Load an instrument before analyzing a render");
    blokkily::BounceOptions options;
    QString source = QStringLiteral("master");
    const auto& song = song_->song();
    if (kind == "track" || kind == "return") {
        const bool track = kind == "track";
        const auto count = track ? song.tracks.size() : song.returns.size();
        if (bus < 0 || static_cast<std::size_t>(bus) >= count)
            return failed(QStringLiteral("There is no %1 %2").arg(kind).arg(bus));
        options.source = blokkily::OutputTap{track ? blokkily::BusKind::track : blokkily::BusKind::ret,
                                             static_cast<std::uint32_t>(bus)};
        source = QStringLiteral("%1 %2").arg(kind).arg(bus);
    } else if (kind != "master") {
        return failed(QStringLiteral("Analyze \"master\", a \"track\" or a \"return\", not \"%1\"").arg(kind));
    }
    QString out_of_song;
    if (!barRange(fromBar, toBar, options, &out_of_song)) return failed(out_of_song);
    const QTemporaryDir directory;
    if (!directory.isValid()) return failed("No temporary folder to render into");
    const auto file = QDir(directory.path()).filePath("analysis.wav");
    const LoadProgress::Scope analyzing(progress_, QStringLiteral("ANALYZING %1").arg(source.toUpper()));
    std::string error;
    if (!bounceThroughEngine(file.toStdString(), blokkily::WaveFormat::float32, options, &error))
        return failed(QStringLiteral("The render failed: %1").arg(QString::fromStdString(error)));
    auto analysis = analyze_file(file);
    analysis.insert("source", source);
    return analysis;
}

QVariantList AppController::analyzeEveryBus(int fromBar, int toBar) {
    if (!engine_ || song_ == nullptr) return {failed("Load an instrument before analyzing a render")};
    blokkily::BounceOptions options;
    QString out_of_song;
    if (!barRange(fromBar, toBar, options, &out_of_song)) return {failed(out_of_song)};
    const QTemporaryDir directory;
    if (!directory.isValid()) return {failed("No temporary folder to render into")};
    const auto& song = song_->song();
    // The master and every bus, written by the one render.
    std::vector<blokkily::TapFile> taps;
    QStringList sources;
    const auto add = [&](blokkily::BusKind kind, std::size_t bus, const QString& name) {
        taps.push_back({{kind, static_cast<std::uint32_t>(bus)},
                        QDir(directory.path()).filePath(QStringLiteral("%1.wav").arg(taps.size())).toStdString(),
                        {}});
        sources << name;
    };
    add(blokkily::BusKind::master, 0, QStringLiteral("master"));
    for (std::size_t track = 0; track < song.tracks.size(); ++track)
        add(blokkily::BusKind::track, track, QStringLiteral("track %1").arg(track));
    for (std::size_t ret = 0; ret < song.returns.size(); ++ret)
        add(blokkily::BusKind::ret, ret, QStringLiteral("return %1").arg(ret));
    const LoadProgress::Scope analyzing(progress_, QStringLiteral("ANALYZING EVERY BUS"));
    std::string error;
    bool rendered = false;
    withEngineStopped([&](std::uint64_t tail) {
        options.progress = bounceProgress(engine_->sample_rate());
        options.cancelled = render_cancel_;
        rendered = blokkily::bounce_taps(*engine_, taps, blokkily::WaveFormat::float32, tail,
                                         options, &error);
    });
    if (!rendered) return {failed(QStringLiteral("The render failed: %1").arg(QString::fromStdString(error)))};
    QVariantList analyses;
    for (std::size_t index = 0; index < taps.size(); ++index) {
        auto analysis = analyze_file(QString::fromStdString(taps[index].file.string()));
        analysis.insert("source", sources.at(static_cast<int>(index)));
        if (index > 0 && index <= song.tracks.size())
            analysis.insert("name", QString::fromStdString(song.tracks[index - 1].name));
        else if (index > song.tracks.size())
            analysis.insert("name", QString::fromStdString(song.returns[index - 1 - song.tracks.size()].name));
        analyses.append(analysis);
    }
    return analyses;
}

QVariantMap AppController::analyzeAudioFile(const QString& path) const {
    const QUrl url(path);
    auto analysis = analyze_file(url.isLocalFile() ? url.toLocalFile() : path);
    analysis.insert("source", path);
    return analysis;
}
