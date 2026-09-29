// Audio clips in the application (item 2.2): the decoded audio the engine is
// given for the song's clips, importing a file without holding up the
// interface, and the overview a clip's waveform draws.

#include "app_controller.hpp"

#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/audio_file.hpp"
#include "blokkily/audio/mixer.hpp"

#include <QFileInfo>
#include <QMetaObject>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
// What the device is asked for when no engine exists yet; the same default
// rebuildEngine() uses.
constexpr double fallback_rate = 48000.0;

QString local_file(const QString& value) {
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : value;
}
} // namespace

// What an import worker hands back to the control thread.
struct AppController::ImportResult {
    std::filesystem::path file;
    int track = 0;
    double tick = 0.0;
    std::optional<blokkily::AudioFileInfo> info;
    blokkily::AudioAssetPtr asset;
    std::string error;
};

bool AppController::hasAudioClips() const {
    return song_ != nullptr && !song_->song().audio_clips.empty();
}

blokkily::AudioAssets AppController::clipAssets(double rate) {
    if (song_ == nullptr) return {};
    blokkily::ClipAssetReport report;
    auto assets = blokkily::load_clip_assets(song_->song(), *assets_, rate, &report);
    if (assets != clip_assets_) {
        clip_assets_ = assets;
        ++asset_revision_;
        emit assetsChanged();
    }
    const auto missing = report.missing_count();
    if (missing > 0) {
        status_ = QString("%1 audio file%2 missing · %3")
                      .arg(missing)
                      .arg(missing == 1 ? "" : "s")
                      .arg(QString::fromStdString(*std::find_if(
                          report.reasons.begin(), report.reasons.end(),
                          [](const std::string& reason) { return !reason.empty(); })));
        emit statusChanged();
    }
    song_->setMissingAudio(std::move(report.missing));
    return assets;
}

int AppController::addAudioTrack() {
    if (song_ == nullptr) return -1;
    return song_->addAudioTrack();
}

bool AppController::importAudioFile(const QString& path) {
    if (song_ == nullptr) return false;
    const int bar = transport_ != nullptr ? transport_->bar() : 0;
    return importAudio(path, song_->selectedTrack(), static_cast<double>(song_->barStart(bar)));
}

bool AppController::importAudio(const QString& path, int track, double tick) {
    if (song_ == nullptr || !std::isfinite(tick) || tick < 0.0) return false;
    const QString local = local_file(path);
    if (local.isEmpty()) return false;
    const std::filesystem::path file = std::filesystem::absolute(local.toStdString());
    const double rate = engine_ && engine_->sample_rate() > 0.0 ? engine_->sample_rate()
                                                                : fallback_rate;
    const int ticket = next_import_++;
    ++imports_pending_;
    import_status_ = QString("Importing %1…").arg(QFileInfo(local).fileName());
    emit importChanged();
    // The decode, and the resample to the engine's rate, run on a thread of
    // their own with a store of their own: a minute of audio takes longer
    // than a frame of the interface. Only the finished asset crosses back.
    const auto threshold = assets_->stream_threshold();
    imports_.emplace(ticket, std::thread([this, ticket, file, track, tick, rate, threshold] {
        auto result = std::make_shared<ImportResult>();
        result->file = file;
        result->track = track;
        result->tick = tick;
        result->info = blokkily::probe_audio_file(file, &result->error);
        if (result->info) {
            blokkily::AudioAssetCache own;
            // A long file stays on disk and streams (wave 4.2), under the
            // same threshold the project's own cache uses.
            own.set_stream_threshold(threshold);
            result->asset = own.load(file, rate, result->info, &result->error,
                                     blokkily::AudioAssetCache::Residency::stream_if_large);
        }
        QMetaObject::invokeMethod(
            this, [this, ticket, result] { finishImport(ticket, *result); },
            Qt::QueuedConnection);
    }));
    return true;
}

void AppController::finishImport(int ticket, const ImportResult& result) {
    if (auto worker = imports_.find(ticket); worker != imports_.end()) {
        if (worker->second.joinable()) worker->second.join();
        imports_.erase(worker);
    }
    --imports_pending_;
    const QString name = QString::fromStdString(result.file.filename().string());
    if (!result.info || !result.asset || song_ == nullptr) {
        import_status_ = QString("Import failed · %1 · %2")
                             .arg(name, QString::fromStdString(result.error));
        emit importChanged();
        return;
    }
    // The store now holds the decode, so the recompile the new clip asks for
    // finds it without reading the file again.
    assets_->adopt(result.file, *result.info, result.asset);
    const int track = result.track >= 0 && result.track < song_->trackCount()
                          ? result.track : song_->selectedTrack();
    const blokkily::AudioFileRef ref{result.file, result.info->frames, result.info->sample_rate,
                                     result.info->channels};
    // Placed now, as one step of history: an undo takes back the whole import.
    last_imported_clip_ = static_cast<qint64>(
        song_->addAudioClip(ref, track, static_cast<blokkily::Tick>(std::llround(result.tick))));
    import_status_ = QString("%1 · %2 s").arg(name).arg(
        static_cast<double>(result.info->frames) / result.info->sample_rate, 0, 'f', 2);
    emit importChanged();
}

QVariantList AppController::clipPeaks(qint64 id, int buckets) const {
    QVariantList out;
    if (song_ == nullptr || buckets <= 0) return out;
    const auto& song = song_->song();
    const auto found = std::find_if(song.audio_clips.begin(), song.audio_clips.end(),
                                    [id](const blokkily::AudioClip& clip) {
                                        return static_cast<qint64>(clip.id) == id;
                                    });
    if (found == song.audio_clips.end() || found->file >= clip_assets_.size() ||
        !clip_assets_[found->file] || found->file >= song.audio_files.size())
        return out;
    const auto& clip = *found;
    const auto& asset = *clip_assets_[clip.file];
    const auto file_rate = song.audio_files[clip.file].sample_rate;
    if (asset.peaks.empty() || file_rate == 0) return out;
    // The clip counts frames at its file's rate; the overview is at the rate
    // the asset was decoded for.
    const double ratio = static_cast<double>(asset.rate) / file_rate;
    const double first = static_cast<double>(clip.offset_frames) * ratio;
    const double length = static_cast<double>(clip.length_frames) * ratio;
    // Drawn against the file's loudest sample rather than full scale, so a
    // quiet recording still fills its clip; the clip's gain and fades then
    // shrink it, so turning a clip down is seen as well as heard.
    float loudest = 0.0F;
    for (const auto& [low, high] : asset.peaks)
        loudest = std::max({loudest, std::abs(low), std::abs(high)});
    const double gain = blokkily::db_to_linear(clip.gain_db) / std::max(loudest, 1e-3F);
    const double fade_in = static_cast<double>(clip.fade_in_frames) * ratio;
    const double fade_out = static_cast<double>(clip.fade_out_frames) * ratio;
    out.reserve(buckets * 2);
    for (int bucket = 0; bucket < buckets; ++bucket) {
        const double from = first + length * bucket / buckets;
        const double to = first + length * (bucket + 1) / buckets;
        auto block = static_cast<std::size_t>(from) / blokkily::audio_peak_block;
        const auto last = std::min(asset.peaks.size() - 1,
                                   static_cast<std::size_t>(std::max(from, to - 1.0)) /
                                       blokkily::audio_peak_block);
        float low = 0.0F;
        float high = 0.0F;
        for (; block <= last && block < asset.peaks.size(); ++block) {
            low = std::min(low, asset.peaks[block].first);
            high = std::max(high, asset.peaks[block].second);
        }
        // The envelope at the bucket's middle: what the fades let through.
        const double middle = (from + to) / 2.0 - first;
        double envelope = gain;
        if (fade_in > 0.0 && middle < fade_in) envelope *= middle / fade_in;
        if (fade_out > 0.0 && middle > length - fade_out)
            envelope *= std::max(0.0, (length - middle) / fade_out);
        out.push_back(low * envelope);
        out.push_back(high * envelope);
    }
    return out;
}
